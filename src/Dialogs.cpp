// CClickGUI 对话框：数值/文本编辑、颜色选取（Win32 通用对话框）、
// 快捷键绑定捕获，以及每帧处理的编辑请求队列。

#include "Internal.h"
#include "Util.h"

namespace cgui {
namespace detail {

using namespace util;

// ----------------------------------------------------------------------------
// 颜色选取器
// ----------------------------------------------------------------------------
static UINT_PTR CALLBACK ColorPickerHookProc(HWND hdlg, UINT uiMsg, WPARAM wParam, LPARAM lParam) {
	if (uiMsg == WM_INITDIALOG) {
		RECT rc; GetWindowRect(hdlg, &rc);
		int w = rc.right - rc.left, h = rc.bottom - rc.top;
		int screenW = GetSystemMetrics(SM_CXSCREEN), screenH = GetSystemMetrics(SM_CYSCREEN);
		SetWindowPos(hdlg, NULL, (screenW - w) / 2, (screenH - h) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
	}
	return 0;
}

COLORREF ShowColorPicker(COLORREF init) {
	CHOOSECOLOR cc = {}; static COLORREF cust[16] = { 0 };
	cc.lStructSize = sizeof(cc); cc.hwndOwner = GetDesktopWindow(); cc.rgbResult = init; cc.lpCustColors = cust;
	cc.Flags = CC_RGBINIT | CC_FULLOPEN | CC_ENABLEHOOK; cc.lpfnHook = ColorPickerHookProc;
	if (ChooseColor(&cc)) return cc.rgbResult;
	return init;
}

// ----------------------------------------------------------------------------
// 文本输入对话框（居中置顶模态）
// ----------------------------------------------------------------------------
struct InputDlgData { std::string result; bool done; std::string def; };

static LRESULT CALLBACK InputDlgWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	static InputDlgData* data = nullptr;
	switch (msg) {
	case WM_CREATE: {
		CREATESTRUCT* cs = (CREATESTRUCT*)lp; data = (InputDlgData*)cs->lpCreateParams;
		CreateWindowW(L"STATIC", L"输入:", WS_CHILD | WS_VISIBLE, 10, 10, 260, 20, hwnd, (HMENU)101, cs->hInstance, nullptr);
		HWND hEdit = CreateWindowW(L"EDIT", toWide(data->def).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, 10, 35, 260, 24, hwnd, (HMENU)102, cs->hInstance, nullptr);
		CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, 100, 70, 80, 28, hwnd, (HMENU)IDOK, cs->hInstance, nullptr);
		SetFocus(hEdit); break;
	}
	case WM_COMMAND:
		if (LOWORD(wp) == IDOK) {
			// 以宽字符读取再转 UTF-8：修复中文输入经 ANSI 接口变乱码的问题
			HWND hEdit = GetDlgItem(hwnd, 102);
			int len = GetWindowTextLengthW(hEdit);
			std::vector<wchar_t> buf((size_t)len + 1, L'\0');
			GetWindowTextW(hEdit, buf.data(), len + 1);
			if (data) { data->result = toUtf8(buf.data()); data->done = true; }
			DestroyWindow(hwnd);
		}
		break;
	case WM_CLOSE:
		if (data) data->done = true;
		DestroyWindow(hwnd); break;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

static ATOM RegisterInputDlgClass() {
	WNDCLASSEXW wc = {}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = InputDlgWndProc; wc.hInstance = GetModuleHandleW(nullptr);
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = L"CClickGuiInputDlg";
	return RegisterClassExW(&wc);
}

std::string ShowInputDialog(const std::string& title, const std::string& def) {
	static ATOM atom = RegisterInputDlgClass(); (void)atom;
	InputDlgData data; data.def = def; data.done = false;
	HWND hDlg = CreateWindowExW(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, L"CClickGuiInputDlg", toWide(title).c_str(),
		WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 300, 140, nullptr, nullptr, GetModuleHandleW(nullptr), &data);
	if (!hDlg) return def;
	RECT rc; GetWindowRect(hDlg, &rc); int w = rc.right - rc.left, h = rc.bottom - rc.top;
	int screenW = GetSystemMetrics(SM_CXSCREEN), screenH = GetSystemMetrics(SM_CYSCREEN);
	SetWindowPos(hDlg, NULL, (screenW - w) / 2, (screenH - h) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
	MSG msg;
	while (!data.done && GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
	if (IsWindow(hDlg)) DestroyWindow(hDlg);
	return data.done ? data.result : def;
}

// ----------------------------------------------------------------------------
// 快捷键捕获对话框（按任意组合键绑定；右键功能行上的快捷键即可清除）
// ----------------------------------------------------------------------------
struct CaptureInfo { Feature* target; bool done; bool cancelled; };

static LRESULT CALLBACK HotkeyCaptureWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	static CaptureInfo* info = nullptr;
	switch (msg) {
	case WM_CREATE: {
		CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
		info = (CaptureInfo*)cs->lpCreateParams;
		SetWindowTextW(hwnd, toWide("绑定快捷键 - " + info->target->name).c_str());
		CreateWindowW(L"STATIC", L"按下组合键（ESC取消）", WS_CHILD | WS_VISIBLE, 20, 20, 200, 20, hwnd, (HMENU)101, cs->hInstance, NULL);
		SetFocus(hwnd);
		break;
	}
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN: {
		if (!info) break;
		int vk = (int)wParam;
		if (vk == VK_ESCAPE) { info->cancelled = true; info->done = true; DestroyWindow(hwnd); return 0; }
		bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
		bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
		bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
		if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
			vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
			vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) return 0;
		Setting* vkS = info->target->findSetting("HotkeyVK");
		if (!vkS) {
			info->target->addSetting(Setting::Int("HotkeyVK", "虚拟键码", vk, 0, 255, 1));
			info->target->addSetting(Setting::Bool("HotkeyAlt", "需要Alt", alt));
			info->target->addSetting(Setting::Bool("HotkeyCtrl", "需要Ctrl", ctrl));
			info->target->addSetting(Setting::Bool("HotkeyShift", "需要Shift", shift));
		} else {
			vkS->setInt(vk);
			if (auto a = info->target->findSetting("HotkeyAlt")) a->setBool(alt);
			if (auto a = info->target->findSetting("HotkeyCtrl")) a->setBool(ctrl);
			if (auto a = info->target->findSetting("HotkeyShift")) a->setBool(shift);
		}
		std::string txt;
		if (ctrl) txt += "Ctrl+";
		if (alt) txt += "Alt+";
		if (shift) txt += "Shift+";
		if (vk >= 'A' && vk <= 'Z') txt += (char)vk;
		else if (vk >= '0' && vk <= '9') txt += (char)vk;
		else if (vk >= VK_F1 && vk <= VK_F24) txt += "F" + std::to_string(vk - VK_F1 + 1);
		else switch (vk) {
		case VK_INSERT: txt += "Insert"; break; case VK_DELETE: txt += "Delete"; break;
		case VK_HOME: txt += "Home"; break; case VK_END: txt += "End"; break;
		case VK_PRIOR: txt += "PageUp"; break; case VK_NEXT: txt += "PageDown"; break;
		case VK_LEFT: txt += "←"; break; case VK_RIGHT: txt += "→"; break;
		case VK_UP: txt += "↑"; break; case VK_DOWN: txt += "↓"; break;
		case VK_SPACE: txt += "空格"; break; case VK_TAB: txt += "Tab"; break;
		case VK_RETURN: txt += "Enter"; break;
		default: txt += "未知(VK=" + std::to_string(vk) + ")"; break;
		}
		Setting* txtS = info->target->findSetting("ShortcutText");
		if (!txtS) info->target->addSetting(Setting::String("ShortcutText", "显示文本", txt));
		else txtS->setString(txt);
		Notify("快捷键已绑定", (info->target->name + " -> " + txt).c_str(), 2000, Theme::Success, Icon::Success);
		RefreshHotkeyForFeature(info->target);
		autoSaveIfEnabled();
		info->done = true; DestroyWindow(hwnd);
		return 0;
	}
	case WM_CLOSE:
		info->cancelled = true; info->done = true; DestroyWindow(hwnd); return 0;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowHotkeyCaptureDialog(Feature* target) {
	CaptureInfo info = { target, false, false };
	WNDCLASSEXW wc = { 0 };
	wc.cbSize = sizeof(WNDCLASSEXW);
	wc.lpfnWndProc = HotkeyCaptureWndProc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = L"CClickGuiHotkeyCapture";
	RegisterClassExW(&wc);

	HWND hDlg = CreateWindowExW(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, L"CClickGuiHotkeyCapture",
		L"", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT, 260, 120,
		NULL, NULL, GetModuleHandleW(NULL), &info);
	if (!hDlg) return;

	RECT rc; GetWindowRect(hDlg, &rc);
	int w = rc.right - rc.left, h = rc.bottom - rc.top;
	int screenW = GetSystemMetrics(SM_CXSCREEN), screenH = GetSystemMetrics(SM_CYSCREEN);
	SetWindowPos(hDlg, NULL, (screenW - w) / 2, (screenH - h) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
	SetFocus(hDlg);

	MSG msg;
	while (!info.done && GetMessageW(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	if (IsWindow(hDlg)) DestroyWindow(hDlg);
}

// ----------------------------------------------------------------------------
// 编辑请求（在 Tick 的 UI 线程弹出对话框，避免在点击回调里阻塞消息泵）
// ----------------------------------------------------------------------------
void processEditRequest() {
	GuiContext& c = ctx(); if (!c.editReq.pending) return;
	Feature* f = c.editReq.feat; Setting* s = c.editReq.set; c.editReq.pending = false; if (!f || !s) return;
	HWND hwnd = c.hwnd; if (hwnd) ShowWindow(hwnd, SW_HIDE);
	if (c.editReq.isColor) {
		c.editReq.isColor = false;
		int rgb = s->asInt();
		COLORREF init = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
		COLORREF res = ShowColorPicker(init);
		if (res != init) { int val = (GetRValue(res) << 16) | (GetGValue(res) << 8) | GetBValue(res); s->setInt(val); }
	} else if (s->type == SettingType::Select) {
		if (!s->options.empty()) {
			std::string def = s->text();
			std::string listTip = "可选值: ";
			for (size_t i = 0; i < s->options.size(); ++i) {
				if (i) listTip += "/";
				listTip += s->options[i];
			}
			std::string res = ShowInputDialog(listTip.c_str(), def);
			if (!res.empty()) {
				bool matched = false;
				for (size_t i = 0; i < s->options.size(); ++i) {
					if (s->options[i] == res) { s->selectIndex((int)i); matched = true; break; }
				}
				if (!matched) {
					for (size_t i = 0; i < s->options.size(); ++i) {
						if (_strnicmp(s->options[i].c_str(), res.c_str(), res.length()) == 0) {
							s->selectIndex((int)i); break;
						}
					}
				}
			}
		}
	} else {
		std::string def = s->text();
		std::string res = ShowInputDialog("编辑设置", def);
		if (!res.empty()) {
			if (s->type == SettingType::Int) { int v = std::atoi(res.c_str()); v = std::max((int)s->minVal, std::min((int)s->maxVal, v)); s->setInt(v); }
			else if (s->type == SettingType::Float) { float v = (float)std::atof(res.c_str()); v = std::max(s->minVal, std::min(s->maxVal, v)); s->setFloat(v); }
			else if (s->type == SettingType::String) s->setString(res);
		}
	}
	autoSaveIfEnabled();
	if (hwnd) { ShowWindow(hwnd, SW_SHOWNOACTIVATE); updateLayered(); }
}

} // namespace detail
} // namespace cgui
