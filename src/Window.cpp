// CClickGUI 窗口层：全屏分层覆盖窗口（逐像素 Alpha 点击穿透）、
// 帧缓冲预乘 → UpdateLayeredWindow 呈现、按需重绘、生命周期与主循环。

#include "Internal.h"
#include "Util.h"

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#endif

namespace cgui {
namespace detail {

// ----------------------------------------------------------------------------
// Alpha 预乘（UpdateLayeredWindow 要求预乘格式）
// ----------------------------------------------------------------------------
void premultiply() {
	GuiContext& c = ctx(); c.preFb.resize(c.fb.size());
	for (size_t i = 0; i < c.fb.size(); ++i) {
		unsigned int p = c.fb[i];
		unsigned int a = CG_A(p);
		unsigned int r = CG_R(p), g = CG_G(p), b = CG_B(p);
		r = r * a / 255; g = g * a / 255; b = b * a / 255;
		c.preFb[i] = CG_ARGB(a, r, g, b);
	}
}

// ----------------------------------------------------------------------------
// 帧呈现：脏检查 → renderAll → premultiply → UpdateLayeredWindow
// ----------------------------------------------------------------------------
void updateLayered() {
	GuiContext& c = ctx();
	if (!c.hwnd || c.w <= 0 || c.h <= 0) return;

	// 鼠标位置（悬停高亮依赖）
	POINT pt;
	GetCursorPos(&pt);
	ScreenToClient(c.hwnd, &pt);
	static int s_lastMouseX = -1, s_lastMouseY = -1;
	int mx = pt.x, my = pt.y;
	bool mouseMoved = (mx != s_lastMouseX || my != s_lastMouseY);
	s_lastMouseX = mx; s_lastMouseY = my;
	c.mouseX = mx; c.mouseY = my;

	bool hasToasts = false;
	{
		std::lock_guard<std::recursive_mutex> lock(c.mtx);
		hasToasts = !c.toasts.empty() || !c.queue.empty();
	}
	bool hasPendingAnim = (c.guiAnim > 0.001f && c.guiAnim < 0.999f) || (c.catSwitchT < 0.999f && c.catSwitchT > 0.001f);
	bool guiDrawRequested = c.guiVisible || hasPendingAnim;
	bool dirty = guiDrawRequested || hasToasts || c.editReq.pending || mouseMoved;
	if (!dirty) return;

	std::lock_guard<std::recursive_mutex> lock(c.mtx);

	render::renderAll();
	RunPostShaders(c, c.animDt / 1000.0f);   // 后处理特效（直通 Alpha 帧缓冲上执行）
	premultiply();

	// 帧内容与鼠标均未变化则跳过呈现
	static std::vector<unsigned int> s_lastFb;
	size_t requiredSize = (size_t)c.w * c.h;
	if (s_lastFb.size() != requiredSize) s_lastFb.assign(requiredSize, 0);
	bool fbIdentical = (requiredSize > 0 && requiredSize == s_lastFb.size()
		&& !memcmp(s_lastFb.data(), c.preFb.data(), requiredSize * sizeof(unsigned int)));
	if (fbIdentical && !mouseMoved) return;
	if (requiredSize == s_lastFb.size() && !c.preFb.empty()) {
		memcpy(s_lastFb.data(), c.preFb.data(), requiredSize * sizeof(unsigned int));
	}

	HDC screen = GetDC(nullptr);
	HDC mem = CreateCompatibleDC(screen);

	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = c.w;
	bmi.bmiHeader.biHeight = -c.h;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;

	void* bits = nullptr;
	HBITMAP dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);

	if (dib && bits && !c.preFb.empty()) {
		size_t copySize = std::min(c.preFb.size(), requiredSize) * sizeof(unsigned int);
		memcpy(bits, c.preFb.data(), copySize);
		HGDIOBJ old = SelectObject(mem, dib);

		POINT dst = { 0, 0 };
		SIZE size = { c.w, c.h };
		POINT src = { 0, 0 };
		BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

		UpdateLayeredWindow(c.hwnd, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);

		SelectObject(mem, old);
	}

	if (dib) DeleteObject(dib);
	DeleteDC(mem);
	ReleaseDC(nullptr, screen);
}

// ----------------------------------------------------------------------------
// 消息泵
// ----------------------------------------------------------------------------
void pumpMessages() {
	MSG msg;
	while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
}

// ----------------------------------------------------------------------------
// 创建全屏分层覆盖窗口（置顶、不抢焦点、不出现在任务栏）
// ----------------------------------------------------------------------------
bool ensureWindow() {
	GuiContext& c = ctx(); if (c.hwnd) return true;
	WNDCLASSEXW wc = {}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = OverlayWndProc; wc.hInstance = GetModuleHandleW(nullptr);
	wc.lpszClassName = L"CClickGuiOverlay"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassExW(&wc);

	RECT workArea;
	SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
	int w = workArea.right - workArea.left;
	int h = workArea.bottom - workArea.top;
	if (w <= 0 || h <= 0) { w = GetSystemMetrics(SM_CXSCREEN); h = GetSystemMetrics(SM_CYSCREEN); }
	if (w <= 0) w = 1920;
	if (h <= 0) h = 1080;

	c.w = w;
	c.h = h;
	c.fb.resize((size_t)w * h, 0);
	c.preFb.resize((size_t)w * h, 0);
	c.clipRect = c_rect(0, 0, w - 1, h - 1);

	// DPI 缩放
	{
		HDC hdc = GetDC(nullptr);
		if (hdc) {
			int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
			ReleaseDC(nullptr, hdc);
			if (dpi > 0) c.systemScale = dpi / 96.0f;
		}
	}

	c.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
		L"CClickGuiOverlay", L"CClickGUI", WS_POPUP,
		workArea.left, workArea.top, w, h,
		nullptr, nullptr, wc.hInstance, nullptr);
	if (!c.hwnd) return false;
	ShowWindow(c.hwnd, SW_SHOWNOACTIVATE);
	SetWindowPos(c.hwnd, HWND_TOPMOST, workArea.left, workArea.top, w, h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
	return true;
}

} // namespace detail

// ============================================================================
// 公开 API：生命周期
// ============================================================================
bool InitGui(int w, int h) {
	RegisterCoreFeatures();
	detail::StartHotkeyThread();
	auto& c = detail::ctx();
	std::lock_guard<std::recursive_mutex> lock(c.mtx);
	if (c.init) return true;

	if (w <= 0 || h <= 0) {
		RECT workArea;
		SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
		w = workArea.right - workArea.left;
		h = workArea.bottom - workArea.top;
		if (w <= 0) w = GetSystemMetrics(SM_CXSCREEN);
		if (h <= 0) h = GetSystemMetrics(SM_CYSCREEN);
		if (w <= 0) w = 1920;
		if (h <= 0) h = 1080;
	}

	c.w = w;
	c.h = h;
	c.fb.resize((size_t)w * h, 0);
	c.preFb.resize((size_t)w * h, 0);
	c.clipRect = c_rect(0, 0, w - 1, h - 1);
	{
		HDC hdc = GetDC(nullptr);
		if (hdc) {
			int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
			ReleaseDC(nullptr, hdc);
			if (dpi > 0) c.systemScale = dpi / 96.0f;
		}
	}

	detail::initialized() = true;
	c.init = true;
	return true;
}

void ShutdownGui() {
	detail::exitSearchMode();
	detail::StopHotkeyThread();
	auto& c = detail::ctx(); HWND hwnd = nullptr;
	{ std::lock_guard<std::recursive_mutex> lock(c.mtx); hwnd = c.hwnd; c.hwnd = nullptr; c.init = false; c.fb.clear(); c.preFb.clear(); }
	if (hwnd) DestroyWindow(hwnd);
	detail::initialized() = false;
}

void Tick() {
	auto& c = detail::ctx();
	unsigned long long now = detail::util::nowMs();
	if (now - c.lastTickTime < 16) return;   // ~60fps 限流
	c.lastTickTime = now;
	if (!c.init) return;
	detail::pumpMessages();
	{ std::lock_guard<std::recursive_mutex> lock(c.mtx); detail::ensureWindow(); }
	detail::processEditRequest();
	detail::updateLayered();

	// 统一驱动「无 worker 线程」视觉类 Feature 的 onTick（UI 线程节奏）
	try {
		for (Feature* f : Registry::instance().allFeatures()) {
			if (!f) continue;
			if (!f->enabled) continue;
			if (!f->onTick) continue;
			if (f->isThreadRunning()) continue;   // 有 worker 线程的自己会调
			try { f->onTick(*f); } catch (...) {}
		}
	} catch (...) {}
}

} // namespace cgui
