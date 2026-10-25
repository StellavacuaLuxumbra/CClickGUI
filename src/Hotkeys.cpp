// CClickGUI 全局热键：独立线程上的 message-only 窗口 + RegisterHotKey。
// 规则：热键必须带至少一个修饰键（Alt/Ctrl/Shift），避免与普通输入冲突；
// WM_HOTKEY 触发时在该线程上直接 toggle 对应 Feature。

#include "Internal.h"
#include "Util.h"

#include <chrono>
#include <unordered_map>

namespace cgui {
namespace detail {

namespace {

struct HotkeyState {
	std::thread thread;
	std::atomic<bool> running{ false };
	std::atomic<HWND> wnd{ nullptr };
	std::atomic<DWORD> threadId{ 0 };
	std::unordered_map<int, Feature*> byId;
	std::unordered_map<Feature*, int> byFeat;
	int nextId = 1000;
	std::recursive_mutex mtx;
};
HotkeyState g_hk;

const UINT WM_HK_REFRESH_ONE = WM_APP + 1;   // wParam = Feature*
const UINT WM_HK_REFRESH_ALL = WM_APP + 2;

// ---- 在热键线程上执行：注销单个 Feature 的热键 ----
void unregisterOneLocked(Feature* f) {
	auto it = g_hk.byFeat.find(f);
	if (it == g_hk.byFeat.end()) return;
	int id = it->second;
	UnregisterHotKey(g_hk.wnd.load(), id);
	g_hk.byId.erase(id);
	g_hk.byFeat.erase(it);
}

// ---- 在热键线程上执行：为单个 Feature 注册热键 ----
void registerOneLocked(Feature* f) {
	if (!f) return;
	unregisterOneLocked(f);
	int vk = 0; bool alt = false, ctrl = false, shift = false;
	if (auto* s = f->findSetting("HotkeyVK")) vk = s->asInt();
	if (auto* s = f->findSetting("HotkeyAlt")) alt = s->asBool();
	if (auto* s = f->findSetting("HotkeyCtrl")) ctrl = s->asBool();
	if (auto* s = f->findSetting("HotkeyShift")) shift = s->asBool();
	if (vk == 0) return;
	if (!(alt || ctrl || shift)) return;   // 必须带修饰键

	UINT mod = MOD_NOREPEAT;
	if (alt) mod |= MOD_ALT;
	if (ctrl) mod |= MOD_CONTROL;
	if (shift) mod |= MOD_SHIFT;

	int id = g_hk.nextId++;
	HWND wnd = g_hk.wnd.load();
	if (wnd && RegisterHotKey(wnd, id, mod, (UINT)vk)) {
		g_hk.byId[id] = f;
		g_hk.byFeat[f] = id;
	}
}

LRESULT CALLBACK HotkeyWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_HOTKEY) {
		std::lock_guard<std::recursive_mutex> lock(g_hk.mtx);
		auto it = g_hk.byId.find((int)wp);
		if (it != g_hk.byId.end() && it->second) {
			Feature* f = it->second;
			try { f->toggle(); } catch (...) {}
		}
		return 0;
	}
	if (msg == WM_HK_REFRESH_ONE) {
		std::lock_guard<std::recursive_mutex> lock(g_hk.mtx);
		registerOneLocked((Feature*)wp);
		return 0;
	}
	if (msg == WM_HK_REFRESH_ALL) {
		std::lock_guard<std::recursive_mutex> lock(g_hk.mtx);
		for (auto& kv : g_hk.byId) UnregisterHotKey(hwnd, kv.first);
		g_hk.byId.clear(); g_hk.byFeat.clear();
		for (auto* f : Registry::instance().allFeatures()) registerOneLocked(f);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

void hotkeyThreadMain() {
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = HotkeyWndProc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.lpszClassName = L"CClickGuiHotkey";
	RegisterClassExW(&wc);
	HWND wnd = CreateWindowExW(0, L"CClickGuiHotkey", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
	g_hk.wnd.store(wnd);
	g_hk.threadId.store(GetCurrentThreadId());
	{
		std::lock_guard<std::recursive_mutex> lock(g_hk.mtx);
		for (auto* f : Registry::instance().allFeatures()) registerOneLocked(f);
	}
	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	if (wnd) {
		std::lock_guard<std::recursive_mutex> lock(g_hk.mtx);
		for (auto& kv : g_hk.byId) UnregisterHotKey(wnd, kv.first);
		g_hk.byId.clear(); g_hk.byFeat.clear();
		DestroyWindow(wnd);
		g_hk.wnd.store(nullptr);
	}
}

} // anonymous namespace

void StartHotkeyThread() {
	if (g_hk.running.load()) return;
	g_hk.running.store(true);
	g_hk.thread = std::thread(hotkeyThreadMain);
	// 等待 message-only 窗口就绪（注册热键依赖窗口句柄）
	for (int i = 0; i < 200 && !g_hk.wnd.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

void StopHotkeyThread() {
	if (!g_hk.running.load()) return;
	g_hk.running.store(false);
	DWORD tid = g_hk.threadId.load();
	if (tid) PostThreadMessageW(tid, WM_QUIT, 0, 0);
	if (g_hk.thread.joinable()) g_hk.thread.join();
	g_hk.wnd.store(nullptr);
	g_hk.threadId.store(0);
}

} // namespace detail

void RefreshHotkeyForFeature(Feature* f) {
	if (!f) return;
	HWND wnd = detail::g_hk.wnd.load();
	if (wnd) PostMessageW(wnd, detail::WM_HK_REFRESH_ONE, (WPARAM)f, 0);
}

void RefreshAllHotkeys() {
	HWND wnd = detail::g_hk.wnd.load();
	if (wnd) PostMessageW(wnd, detail::WM_HK_REFRESH_ALL, 0, 0);
}

} // namespace cgui
