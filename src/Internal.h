#ifndef CCLICKGUI_INTERNAL_H
#define CCLICKGUI_INTERNAL_H

// CClickGUI 内部实现头：GuiContext 状态结构与 detail 命名空间声明。
// 仅供框架源码内部使用，使用者只需包含 <CClickGUI.h>。

#include "../include/CClickGUI.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif

#include <windows.h>
#include <windowsx.h>
#include <deque>
#include <cstring>

namespace cgui {

namespace detail {

// ============================================================================
// GuiContext — GUI 全部运行时状态（单例，ctx() 获取）
// ============================================================================
struct GuiContext {
	int w, h;
	bool init, guiVisible;
	int selCat, scrollY;                 // 当前分类索引 / 列表滚动偏移
	HWND hwnd;

	std::vector<unsigned int> fb, preFb; // ARGB 帧缓冲 / 预乘 Alpha 帧缓冲

	// ---- Toast 通知 ----
	struct Toast {
		std::string key, title, msg, prevTitle, prevMsg;
		int dur; Theme theme; Icon icon;
		unsigned long long start, lastUpdate;
		bool active; int id; c_rect rect;
		float progress, contentT;
		int prevBaseH, prevBaseW;
		int targetBaseW, targetBaseH;
		float visualY;
	};
	std::deque<Toast> queue;
	std::vector<Toast> toasts;
	int nextId;

	// ---- 命中区域（渲染时构建，点击时逆序遍历）----
	struct Hit {
		enum Type {
			ToggleFeat, ExpandFeat, ToggleSetting, StepSetting,
			CloseGui, CloseToast, SelectCat, CaptureShortcut,
			DragPanel, PickColor, RestartFeat, PageSelect,
			SearchBoxClick, ScrollBar, SelectDropdown, SelectOption, DragSlider
		} type;
		c_rect rect;
		Feature* feat; Setting* set;
		int dir; int toastId;
		std::string pageName; int optionIndex;
	};
	std::vector<Hit> hits;

	std::recursive_mutex mtx;
	UIStyle style;
	NotifyStyle notifyStyle;

	// ---- 设置值编辑请求（弹 Win32 对话框）----
	struct EditRequest { Feature* feat; Setting* set; bool pending; bool isColor; };
	EditRequest editReq;

	// ---- 面板拖拽 ----
	bool draggingPanel;
	int dragStartX, dragStartY, panelOffsetX, panelOffsetY;

	// ---- 滚动条拖拽 ----
	bool draggingScrollBar;
	int scrollDragStartY, scrollDragStartScroll;
	struct ScrollBarInfo { int viewTop, clipH, totalH, barH; } scrollBarInfo;

	// ---- 滑条拖拽（Int/Float 设置）----
	struct SliderDrag { bool active; Feature* feat; Setting* set; c_rect track; } sliderDrag;

	int mouseX, mouseY;
	std::string hoveredDesc;
	unsigned long long lastFrameTime;
	unsigned long long lastTickTime;
	c_rect clipRect;
	c_rect contentRect;                  // 本帧非透明内容包围盒（后处理特效作用区域）
	float systemScale;

	// ---- 原地搜索 ----
	bool searchActive;
	c_rect searchBoxRect;
	unsigned long long lastCursorBlink;
	bool cursorVisible;

	// ---- 动画 ----
	bool guiAnimTarget;
	float guiAnim;
	float catSwitchT;
	int prevSelCat;
	float animDt;

	// ---- Select 下拉框 ----
	struct SelectDropdownState {
		bool active;
		Feature* feat;
		Setting* set;
		c_rect triggerRect;
		int hoveredOption;
		SelectDropdownState() : active(false), feat(nullptr), set(nullptr), hoveredOption(-1) {}
	} selectDropdown;

	GuiContext() : w(0), h(0), init(false), guiVisible(false), selCat(0), scrollY(0), hwnd(nullptr),
		nextId(1), draggingPanel(false), dragStartX(0), dragStartY(0), panelOffsetX(0), panelOffsetY(0),
		draggingScrollBar(false), scrollDragStartY(0), scrollDragStartScroll(0), scrollBarInfo{ 0,0,0,0 },
		mouseX(-1), mouseY(-1), lastFrameTime(0), lastTickTime(0), clipRect(0, 0, 0, 0), systemScale(1.0f),
		searchActive(false), lastCursorBlink(0), cursorVisible(true),
		guiAnimTarget(false), guiAnim(0.0f), catSwitchT(1.0f), prevSelCat(0), animDt(16.6f) {
		editReq.pending = false; editReq.isColor = false; editReq.feat = nullptr; editReq.set = nullptr;
		sliderDrag.active = false; sliderDrag.feat = nullptr; sliderDrag.set = nullptr;
	}
};

CGUI_API GuiContext& ctx();
CGUI_API bool& initialized();

struct ThemeColor { unsigned int bg, panel, text, sub, accent, border; };

// ---- util：纯工具（混色 / 编码 / 插值 / 时间）----
namespace util {
ThemeColor getTheme(Theme th, unsigned char alpha);
std::wstring toWide(const std::string& s);
}

// ---- draw：软件光栅绘图基元（SDF 抗锯齿）----
namespace draw {
void drawText(c_rect r, const std::string& text, unsigned int color, unsigned int align, int fontSize = 15, int weight = FW_NORMAL);
void putPixel(int x, int y, unsigned int col);
void fillRect(c_rect r, unsigned int col);
void drawRect(c_rect r, unsigned int col, int w = 1);
void drawRoundRect(c_rect r, int rad, unsigned int col);                       // 填充圆角矩形（SDF 抗锯齿）
void strokeRoundRect(c_rect r, int rad, float thickness, unsigned int col);    // 圆角描边（SDF 抗锯齿）
void drawSoftShadow(c_rect r, int rad, int blur, unsigned int col);            // 柔和投影（单次 SDF 衰减）
void drawRoundedIconBox(c_rect iconRc, int radius, unsigned int iconBg, const std::string& ch, int chFont, unsigned int chColor = CG_RGB(255, 255, 255));
void drawFadingText2(int x, int y, int w, int h,
	float ctEase, float progressEase, unsigned int baseTextColor,
	const std::string& prevTitle, const std::string& prevMsg,
	const std::string& title, const std::string& msg,
	int fontTitle, int fontMsg,
	bool expandedNow, bool expandedPrev,
	int linePadY = 0, int titlePadYTop = 0,
	bool multiLineMsg = false, int* outMsgLines = nullptr);
void drawToggle(c_rect r, float onFrac, unsigned int borderCol);
void addHit(int type, c_rect rect, Feature* f, Setting* s, int dir, int tid, const std::string& page);
inline void addHit(int type, c_rect rect, Feature* f = nullptr, Setting* s = nullptr, int dir = 0, int tid = 0) { addHit(type, rect, f, s, dir, tid, std::string()); }
}

// ---- anim：状态推进（无绘制）----
namespace anim {
void updateToasts();
void updateAnims();
}

// ---- render：渲染（调用 draw，构建命中区域）----
namespace render {
void renderToast();
bool featureMatchesSearch(Feature* f, const std::string& query);
void renderPageTabs(Feature* f, int x, int y, int w, const ThemeColor& tc);
int renderFeatureRow(Feature* f, int x, int y, int w, int indent, const ThemeColor& tc, const std::string& searchFilter);
void renderClickGui();
void renderSelectDropdownOverlay();
void renderAll();
}

// ---- 设置值变更 ----
void changeSetting(Setting* s, int dir);
void autoSaveIfEnabled();

// ---- Win32 对话框 ----
COLORREF ShowColorPicker(COLORREF init);
std::string ShowInputDialog(const std::string& title, const std::string& def);
void ShowHotkeyCaptureDialog(Feature* target);
void processEditRequest();

// ---- 命中处理 / 交互 ----
bool handleClick(int x, int y, int dir = 1);
bool isInteractive(int x, int y);
void enterSearchMode();
void exitSearchMode();

// ---- 全局热键线程 ----
void StartHotkeyThread();
void StopHotkeyThread();

// ---- 后处理特效管线（Effects.cpp）----
void RunPostShaders(GuiContext& c, float dtSec);

// ---- 窗口层 ----
void premultiply();
void updateLayered();
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
bool ensureWindow();
void pumpMessages();

} // namespace detail

} // namespace cgui

#endif // CCLICKGUI_INTERNAL_H
