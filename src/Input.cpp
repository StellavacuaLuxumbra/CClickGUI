// CClickGUI 交互层：命中点击分发、面板拖拽、滚动（滚轮 + 滚动条拖拽）、
// 滑条拖拽取值、原地搜索的键盘输入（焦点借取 / 归还）。

#include "Internal.h"
#include "Util.h"

namespace cgui {
namespace detail {

using namespace util;
using namespace draw;
using namespace render;

// ----------------------------------------------------------------------------
// 设置值步进（右键 / 无滑条时）
// ----------------------------------------------------------------------------
void changeSetting(Setting* s, int dir) {
	if (!s) return;
	if (s->type == SettingType::Bool) s->setBool(!s->asBool());
	else if (s->type == SettingType::Int) { int v = s->asInt() + (int)s->step * dir; v = std::max((int)s->minVal, std::min((int)s->maxVal, v)); s->setInt(v); }
	else if (s->type == SettingType::Float) { float v = s->asFloat() + s->step * dir; v = std::max(s->minVal, std::min(s->maxVal, v)); s->setFloat(v); }
	else if (s->type == SettingType::Select) { s->cycleSelect(dir); }
	autoSaveIfEnabled();
}

// ----------------------------------------------------------------------------
// 滑条取值：x 坐标 → 区间值（按 step 量化）
// ----------------------------------------------------------------------------
static void setSliderValueFromX(Setting* s, int x, const c_rect& track) {
	if (!s || track.r <= track.l) return;
	float frac = std::clamp((float)(x - track.l) / (float)(track.r - track.l), 0.0f, 1.0f);
	float raw = s->minVal + frac * (s->maxVal - s->minVal);
	if (s->step > 0.0f) raw = std::floor(raw / s->step + 0.5f) * s->step;
	if (raw < s->minVal) raw = s->minVal;
	if (raw > s->maxVal) raw = s->maxVal;
	if (s->type == SettingType::Int) s->setInt((int)(raw + (raw >= 0 ? 0.5f : -0.5f)));
	else s->setFloat(raw);
	autoSaveIfEnabled();
}

// ----------------------------------------------------------------------------
// 搜索模式：借取键盘焦点（点击搜索框时），退出时归还给先前前台窗口
// ----------------------------------------------------------------------------
static HWND s_prevForeground = nullptr;
static wchar_t s_pendingSurrogate = 0;

void enterSearchMode() {
	GuiContext& c = ctx();
	if (!c.hwnd || c.searchActive) return;
	c.searchActive = true;
	c.lastCursorBlink = nowMs();
	c.cursorVisible = true;
	DWORD cur = GetCurrentThreadId();
	s_prevForeground = GetForegroundWindow();
	DWORD prev = s_prevForeground ? GetWindowThreadProcessId(s_prevForeground, nullptr) : 0;
	if (prev && prev != cur) AttachThreadInput(cur, prev, TRUE);
	LONG_PTR ex = GetWindowLongPtrW(c.hwnd, GWL_EXSTYLE);
	SetWindowLongPtrW(c.hwnd, GWL_EXSTYLE, ex & ~WS_EX_NOACTIVATE);
	SetForegroundWindow(c.hwnd);
	SetFocus(c.hwnd);
	if (prev && prev != cur) AttachThreadInput(cur, prev, FALSE);
}

void exitSearchMode() {
	GuiContext& c = ctx();
	if (!c.searchActive) return;
	c.searchActive = false;
	s_pendingSurrogate = 0;
	if (c.hwnd) {
		DWORD cur = GetCurrentThreadId();
		DWORD prev = s_prevForeground ? GetWindowThreadProcessId(s_prevForeground, nullptr) : 0;
		if (prev && prev != cur) AttachThreadInput(cur, prev, TRUE);
		LONG_PTR ex = GetWindowLongPtrW(c.hwnd, GWL_EXSTYLE);
		SetWindowLongPtrW(c.hwnd, GWL_EXSTYLE, ex | WS_EX_NOACTIVATE);
		if (s_prevForeground && IsWindow(s_prevForeground)) {
			SetForegroundWindow(s_prevForeground);
			SetFocus(s_prevForeground);
		} else {
			SetFocus(nullptr);
		}
		if (prev && prev != cur) AttachThreadInput(cur, prev, FALSE);
	}
	s_prevForeground = nullptr;
}

static void searchAppendChar(wchar_t ch) {
	wchar_t pair[3];
	std::string utf8;
	if (ch >= 0xD800 && ch <= 0xDBFF) { s_pendingSurrogate = ch; return; }
	if (ch >= 0xDC00 && ch <= 0xDFFF) {
		if (s_pendingSurrogate) { pair[0] = s_pendingSurrogate; pair[1] = ch; pair[2] = 0; s_pendingSurrogate = 0; }
		else return;
	} else { pair[0] = ch; pair[1] = 0; }
	int n = WideCharToMultiByte(CP_UTF8, 0, pair, -1, nullptr, 0, nullptr, nullptr);
	if (n <= 1) return;
	std::vector<char> buf(n);
	WideCharToMultiByte(CP_UTF8, 0, pair, -1, buf.data(), n, nullptr, nullptr);
	searchText.append(buf.data(), n - 1);
	if (searchText.size() > 128) searchText.resize(128);
}

// ----------------------------------------------------------------------------
// 点击处理（dir: 1=左键, -1=右键）。逆序遍历命中区域。
// ----------------------------------------------------------------------------
bool handleClick(int x, int y, int dir) {
	GuiContext& c = ctx();
	std::lock_guard<std::recursive_mutex> lock(c.mtx);

	// 点击搜索框之外 → 先退出搜索输入模式（保留过滤文本）
	if (c.searchActive && !c.searchBoxRect.contains(x, y)) exitSearchMode();

	for (auto it = c.hits.rbegin(); it != c.hits.rend(); ++it) {
		if (!it->rect.contains(x, y)) continue;
		switch (it->type) {
		case GuiContext::Hit::SelectDropdown:
			if (it->feat && it->set) {
				if (c.selectDropdown.active && c.selectDropdown.set == it->set) {
					c.selectDropdown.active = false;
					c.selectDropdown.feat = nullptr;
					c.selectDropdown.set = nullptr;
				} else {
					c.selectDropdown.active = true;
					c.selectDropdown.feat = it->feat;
					c.selectDropdown.set = it->set;
					c.selectDropdown.hoveredOption = -1;
				}
			}
			return true;
		case GuiContext::Hit::SelectOption:
			if (it->set) {
				it->set->selectIndex(it->optionIndex);
				c.selectDropdown.active = false;
				c.selectDropdown.feat = nullptr;
				c.selectDropdown.set = nullptr;
				autoSaveIfEnabled();
			}
			return true;
		case GuiContext::Hit::ToggleFeat:
			if (it->feat) {
				if (dir > 0) {
					it->feat->toggle();
					if (!searchText.empty()) {
						// 搜索模式下点击功能 → 跳转到其所在分类
						auto cats = Registry::instance().categories();
						for (size_t i = 0; i < cats.size(); ++i) {
							if (cats[i] == it->feat->category) { c.selCat = (int)i; break; }
						}
						exitSearchMode();
						searchText.clear();
					}
				} else {
					it->feat->expanded = !it->feat->expanded;
				}
			}
			return true;
		case GuiContext::Hit::ExpandFeat:
			if (it->feat) it->feat->expanded = !it->feat->expanded;
			return true;
		case GuiContext::Hit::ToggleSetting:
			changeSetting(it->set, 1);
			return true;
		case GuiContext::Hit::StepSetting:
			// String：左键弹编辑框；数值：左键弹编辑框 / 右键步进
			if (dir > 0) { c.editReq.feat = it->feat; c.editReq.set = it->set; c.editReq.pending = true; c.editReq.isColor = false; }
			else changeSetting(it->set, -1);
			return true;
		case GuiContext::Hit::DragSlider: {
			if (it->set) {
				c_rect track;
				track.l = it->rect.l + 12; track.r = it->rect.r - 12;
				track.t = it->rect.t; track.b = it->rect.b;
				if (dir > 0) {
					setSliderValueFromX(it->set, x, track);
					c.sliderDrag.active = true;
					c.sliderDrag.feat = it->feat;
					c.sliderDrag.set = it->set;
					c.sliderDrag.track = track;
					SetCapture(c.hwnd);
				} else {
					c.editReq.feat = it->feat; c.editReq.set = it->set; c.editReq.pending = true; c.editReq.isColor = false;
				}
			}
			return true;
		}
		case GuiContext::Hit::PickColor:
			if (it->feat && it->set) { c.editReq.feat = it->feat; c.editReq.set = it->set; c.editReq.pending = true; c.editReq.isColor = true; }
			return true;
		case GuiContext::Hit::SelectCat:
			exitSearchMode();
			searchText.clear();
			c.selCat = it->dir; c.scrollY = 0;
			return true;
		case GuiContext::Hit::CloseGui:
			exitSearchMode();
			searchText.clear();
			GetGuiFeature().setEnabled(false);
			return true;
		case GuiContext::Hit::CloseToast:
			for (size_t i = 0; i < c.toasts.size(); ++i) if (c.toasts[i].id == it->toastId) { c.toasts.erase(c.toasts.begin() + i); break; }
			return true;
		case GuiContext::Hit::CaptureShortcut:
			if (it->feat) {
				if (dir < 0) {
					// 右键清除快捷键
					auto* target = it->feat;
					if (auto* vkS = target->findSetting("HotkeyVK")) vkS->setInt(0);
					if (auto a = target->findSetting("HotkeyAlt")) a->setBool(false);
					if (auto a = target->findSetting("HotkeyCtrl")) a->setBool(false);
					if (auto a = target->findSetting("HotkeyShift")) a->setBool(false);
					if (auto* txtS = target->findSetting("ShortcutText")) txtS->setString("无");
					Notify("快捷键已清除", target->name.c_str(), 1500, Theme::Dark, Icon::Info);
					RefreshHotkeyForFeature(target);
					autoSaveIfEnabled();
				} else {
					ShowHotkeyCaptureDialog(it->feat);
				}
			}
			return true;
		case GuiContext::Hit::DragPanel:
			c.draggingPanel = true; c.dragStartX = x; c.dragStartY = y; SetCapture(c.hwnd);
			return true;
		case GuiContext::Hit::RestartFeat:
			if (it->feat) it->feat->restart();
			return true;
		case GuiContext::Hit::PageSelect:
			if (it->feat) it->feat->currentPage = it->pageName;
			return true;
		case GuiContext::Hit::ScrollBar: {
			float scale = c.style.guiScale * c.systemScale;
			int gw = (int)(std::min(680, std::max(420, c.w - 120)) * scale);
			int gh = (int)(std::min(600, std::max(360, c.h - 120)) * scale);
			int baseX = (c.w - gw) / 2;
			int baseY = (c.h - gh) / 2;
			int gxx = baseX + c.panelOffsetX;
			int gyy = baseY + c.panelOffsetY;
			int catTop = gyy + (int)(74 * scale);
			int listX = gxx + (int)(186 * scale);
			int viewTop = catTop;
			int viewBottom = gyy + gh - (int)(10 * scale);
			int listW = gw - (int)(214 * scale);
			int clipH = viewBottom - viewTop;

			// 测量当前列表总高度（渲染一遍取行高，帧缓冲随后会被重绘覆盖）
			int totalH = 0;
			std::string currentSearch = searchText;
			auto cats = Registry::instance().categories();
			if (c.selCat >= 0 && c.selCat < (int)cats.size()) {
				c_rect savedClip = c.clipRect;
				c.clipRect.set(0, 0, 0, 0);
				for (auto* f : Registry::instance().allFeatures()) {
					if (!f || !f->visibleInClickGui) continue;
					if (!currentSearch.empty()) {
						if (!featureMatchesSearch(f, currentSearch)) continue;
					} else {
						if (f->category != cats[c.selCat]) continue;
					}
					totalH += renderFeatureRow(f, listX, viewTop + totalH, listW, 0, getTheme(Theme::Purple, c.style.guiAlpha), currentSearch);
				}
				c.clipRect = savedClip;
			}

			if (totalH > clipH) {
				int barH = std::max(30, clipH * clipH / totalH);
				int barPos = (clipH - barH) * c.scrollY / (totalH - clipH);
				c_rect bar(listX + listW + 4, viewTop + barPos, c.style.scrollBarWidth, barH);
				if (bar.contains(x, y)) {
					c.draggingScrollBar = true;
					c.scrollDragStartY = y;
					c.scrollDragStartScroll = c.scrollY;
					c.scrollBarInfo = { viewTop, clipH, totalH, barH };
				} else {
					int newScroll = (totalH - clipH) * (y - viewTop - barH / 2) / (clipH - barH);
					newScroll = std::max(0, std::min(totalH - clipH, newScroll));
					c.scrollY = newScroll;
				}
			}
			return true;
		}
		case GuiContext::Hit::SearchBoxClick:
			enterSearchMode();
			return true;
		default: break;
		}
	}
	if (c.selectDropdown.active) {
		c.selectDropdown.active = false;
		c.selectDropdown.feat = nullptr;
		c.selectDropdown.set = nullptr;
	}
	return false;
}

bool isInteractive(int x, int y) {
	if (ctx().guiVisible) return true;
	for (auto& h : ctx().hits) if (h.rect.contains(x, y)) return true;
	return false;
}

// ----------------------------------------------------------------------------
// 覆盖层窗口过程
// ----------------------------------------------------------------------------
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	GuiContext& c = ctx();
	switch (msg) {
		case WM_MOUSEACTIVATE: {
			if (!c.guiVisible) return MA_NOACTIVATE;
			// 点击搜索框才允许激活（借键盘焦点），其余点击不抢焦点
			DWORD pos = GetMessagePos();
			POINT pt = { GET_X_LPARAM(pos), GET_Y_LPARAM(pos) };
			ScreenToClient(hwnd, &pt);
			if (c.searchBoxRect.contains(pt.x, pt.y)) return MA_ACTIVATE;
			return MA_NOACTIVATE;
		}
		case WM_LBUTTONDOWN: handleClick(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 1); updateLayered(); return 0;
		case WM_RBUTTONDOWN: handleClick(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), -1); updateLayered(); return 0;
		case WM_LBUTTONDBLCLK: {
			int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
			for (auto it = c.hits.rbegin(); it != c.hits.rend(); ++it) {
				if (!it->rect.contains(x, y)) continue;
				if ((it->type == GuiContext::Hit::StepSetting || it->type == GuiContext::Hit::PickColor || it->type == GuiContext::Hit::DragSlider) && it->feat && it->set) {
					c.editReq.feat = it->feat; c.editReq.set = it->set; c.editReq.pending = true;
					c.editReq.isColor = (it->type == GuiContext::Hit::PickColor);
					return 0;
				}
			}
			break;
		}
		case WM_MOUSEMOVE: {
			int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
			if (c.draggingPanel) {
				c.panelOffsetX += x - c.dragStartX;
				c.panelOffsetY += y - c.dragStartY;
				c.dragStartX = x;
				c.dragStartY = y;
				updateLayered();
			} else if (c.draggingScrollBar) {
				int deltaY = y - c.scrollDragStartY;
				int denom = c.scrollBarInfo.clipH - c.scrollBarInfo.barH;
				int newScroll = c.scrollDragStartScroll;
				if (denom > 0) newScroll = c.scrollDragStartScroll + (c.scrollBarInfo.totalH - c.scrollBarInfo.clipH) * deltaY / denom;
				newScroll = std::max(0, std::min(c.scrollBarInfo.totalH - c.scrollBarInfo.clipH, newScroll));
				c.scrollY = newScroll;
				updateLayered();
			} else if (c.sliderDrag.active && c.sliderDrag.set) {
				setSliderValueFromX(c.sliderDrag.set, x, c.sliderDrag.track);
				updateLayered();
			}
			return 0;
		}
		case WM_LBUTTONUP:
		case WM_RBUTTONUP:
			if (c.draggingScrollBar) c.draggingScrollBar = false;
			if (c.sliderDrag.active) c.sliderDrag.active = false;
			c.draggingPanel = false;
			ReleaseCapture();
			return 0;
		case WM_CAPTURECHANGED:
			if (lp != (LPARAM)hwnd) {
				c.draggingPanel = false;
				c.draggingScrollBar = false;
				c.sliderDrag.active = false;
			}
			return 0;
		case WM_MOUSEWHEEL: {
			int delta = GET_WHEEL_DELTA_WPARAM(wp);
			float scale = c.style.guiScale * c.systemScale;
			int gw = (int)(std::min(680, std::max(420, c.w - 120)) * scale);
			int gh = (int)(std::min(600, std::max(360, c.h - 120)) * scale);
			int baseX = (c.w - gw) / 2;
			int baseY = (c.h - gh) / 2;
			int gxx = baseX + c.panelOffsetX;
			int gyy = baseY + c.panelOffsetY;
			int catTop = gyy + (int)(74 * scale);
			int listX = gxx + (int)(186 * scale);
			int viewBottom = gyy + gh - (int)(10 * scale);
			int listW = gw - (int)(214 * scale);
			int clipH = viewBottom - catTop;

			int totalH = 0;
			std::string currentSearch = searchText;
			auto cats = Registry::instance().categories();
			if (c.selCat >= 0 && c.selCat < (int)cats.size()) {
				c_rect savedClip = c.clipRect;
				c.clipRect.set(0, 0, 0, 0);
				for (auto* f : Registry::instance().allFeatures()) {
					if (!f || !f->visibleInClickGui) continue;
					if (!currentSearch.empty()) {
						if (!featureMatchesSearch(f, currentSearch)) continue;
					} else {
						if (f->category != cats[c.selCat]) continue;
					}
					totalH += renderFeatureRow(f, listX, catTop + totalH, listW, 0, getTheme(Theme::Purple, c.style.guiAlpha), currentSearch);
				}
				c.clipRect = savedClip;
			}

			if (totalH > clipH) {
				int scrollStep = std::max(20, clipH / 5);
				if (delta > 0) c.scrollY -= scrollStep;
				else c.scrollY += scrollStep;
				c.scrollY = std::max(0, std::min(totalH - clipH, c.scrollY));
				updateLayered();
			}
			return 0;
		}
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			if (c.searchActive) {
				if (wp == VK_ESCAPE) {
					if (!searchText.empty()) { searchText.clear(); updateLayered(); }
					else exitSearchMode();
					return 0;
				}
				if (wp == VK_RETURN) { exitSearchMode(); return 0; }
				return 0;
			}
			if (c.guiVisible) return 0;
			break;
		case WM_CHAR:
		case WM_SYSCHAR:
			if (c.searchActive) {
				wchar_t ch = (wchar_t)wp;
				if (ch == L'\r' || ch == 27) return 0;
				if (ch == L'\b') {
					if (!searchText.empty()) {
						// 回退一个 UTF-8 字符（回溯跳过全部后续字节 0x80..0xBF）
						size_t cut = searchText.size() - 1;
						while (cut > 0 && ((unsigned char)searchText[cut] & 0xC0) == 0x80) --cut;
						searchText.resize(cut);
						updateLayered();
					}
					return 0;
				}
				if (ch >= 32) { searchAppendChar(ch); updateLayered(); }
				return 0;
			}
			if (c.guiVisible) return 0;
			break;
		case WM_KILLFOCUS:
			exitSearchMode();
			return 0;
		case WM_SETFOCUS:
			if (c.searchActive) { s_pendingSurrogate = 0; }
			return 0;
		default: break;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace detail
} // namespace cgui
