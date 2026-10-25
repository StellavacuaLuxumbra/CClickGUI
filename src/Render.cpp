// CClickGUI 渲染核心：主面板（分类页签 + 功能列表 + 滚动条 + 搜索框）、
// 功能行（开关/滑条/下拉框/颜色/分页/快捷键）、Toast 通知。

#include "Internal.h"
#include "Util.h"

namespace cgui {
namespace detail {
namespace render {

using cgui::detail::ctx;
using namespace cgui::detail::util;
using namespace cgui::detail::draw;
using cgui::searchText;

// 包围盒合并（用于 contentRect：后处理特效作用区域）
static void unionRect(c_rect& a, const c_rect& b) {
	if (b.width() <= 0 || b.height() <= 0) return;
	if (a.width() <= 0 || a.height() <= 0) { a = b; return; }
	a.l = std::min(a.l, b.l); a.t = std::min(a.t, b.t);
	a.r = std::max(a.r, b.r); a.b = std::max(a.b, b.b);
}
static c_rect inflateRect(const c_rect& r, int n) { return c_rect(r.l - n, r.t - n, r.width() + n * 2, r.height() + n * 2); }

// ----------------------------------------------------------------------------
// Toast 通知（右上角）
// ----------------------------------------------------------------------------
void renderToast() {
	GuiContext& c = ctx(); NotifyStyle& ns = c.notifyStyle;
	float scale = ns.scale * c.systemScale;
	int minW = (int)(318 * scale), minH = (int)(66 * scale);
	int gap = (int)(8 * scale), right = (int)(18 * scale), top = (int)(18 * scale) + (int)(ns.offsetY * c.systemScale);
	int fontTitle = (int)(15 * scale), fontMsg = (int)(13 * scale);
	int iconSize = (int)(32 * scale);
	for (size_t i = 0; i < c.toasts.size(); ++i) {
		auto& t = c.toasts[i];
		float e = easeOutCubic(t.progress);
		if (e <= 0.01f) continue;
		float ctEase = easeInOutCubic(t.contentT);

		int tgtW = (t.targetBaseW > 0) ? t.targetBaseW : minW;
		int tgtH = (t.targetBaseH > 0) ? t.targetBaseH : minH;
		int prvW = (t.prevBaseW > 0) ? t.prevBaseW : tgtW;
		int prvH = (t.prevBaseH > 0) ? t.prevBaseH : tgtH;
		int curW = (int)((float)prvW + (float)(tgtW - prvW) * ctEase);
		int curH = (int)((float)prvH + (float)(tgtH - prvH) * ctEase);
		curW = std::max(minW, curW); curH = std::max(minH, curH);

		float slotY = (t.visualY >= 0.0f) ? t.visualY : ((float)top + (float)i * (float)(std::max(minH, curH) + gap));
		int slide = (int)((1.0f - e) * (float)(curH + 12));
		int x = std::max(6, (int)(c.w - curW - right + ns.offsetX * c.systemScale + (1.0f - e) * 40.0f));
		int y = (int)slotY + slide;
		unsigned int bgA = rgbaScaleAlpha(ns.bg, e);
		unsigned int acA = rgbaScaleAlpha(ns.accent, e);
		t.rect.set(x, y, curW, curH);
		drawSoftShadow(t.rect, (int)(16 * scale), 8, CG_ARGB(70, 6, 6, 10));
		drawRoundRect(t.rect, (int)(16 * scale), bgA);
		if (ns.showIcon) {
			c_rect iconRc(x + (int)(14 * scale), y + (curH - iconSize) / 2, iconSize, iconSize);
			drawRoundedIconBox(iconRc, (int)(16 * scale), acA, iconChar(t.icon), (int)(14 * scale));
		}
		if (ns.showText) {
			int textX = x + (int)(56 * scale);
			int textW = curW - (int)(72 * scale);
			int titlePad = std::max(0, (curH - ((int)(fontTitle * 1.6f) + fontMsg)) / 2);
			bool multiLine = (tgtH > minH + 2) ? true : false;
			drawFadingText2(textX, y + titlePad, textW, curH - titlePad,
				ctEase, e, ns.text,
				t.prevTitle, t.prevMsg, t.title, t.msg,
				fontTitle, fontMsg,
				true, !t.prevMsg.empty(), 0, 0,
				multiLine, nullptr);
		}
		if (e > 0.5f && ctEase > 0.3f) addHit(GuiContext::Hit::CloseToast, t.rect, nullptr, nullptr, 1, t.id);
		unionRect(c.contentRect, inflateRect(t.rect, 10));
	}
}

// ----------------------------------------------------------------------------
// 搜索过滤
// ----------------------------------------------------------------------------
bool featureMatchesSearch(Feature* f, const std::string& query) {
	if (query.empty()) return true;
	std::string nl = f->name; std::transform(nl.begin(), nl.end(), nl.begin(), ::tolower);
	std::string ql = query; std::transform(ql.begin(), ql.end(), ql.begin(), ::tolower);
	return nl.find(ql) != std::string::npos;
}

static bool isHiddenSetting(const Setting& st) {
	return !st.name.empty() && st.name[0] == '_';
}
static bool isHotkeySetting(const std::string& n) {
	return n == "HotkeyVK" || n == "HotkeyAlt" || n == "HotkeyCtrl" || n == "HotkeyShift" || n == "ShortcutText";
}
static bool isColorSetting(const Setting& st) {
	return (st.type == SettingType::Int) && (st.name.find("色") != std::string::npos || st.name.find("RGB") != std::string::npos || st.name.find("颜色") != std::string::npos);
}
// 数值设置渲染为可拖拽滑条
static bool isSliderSetting(const Setting& st) {
	return (st.type == SettingType::Int || st.type == SettingType::Float) && !isColorSetting(st);
}
// 设置行占用的布局高度（与 renderFeatureRow 内实际渲染保持一致）
static int settingRowUsed(const Setting& st) {
	return isSliderSetting(st) ? 44 : 34;
}

// ----------------------------------------------------------------------------
// 分页页签
// ----------------------------------------------------------------------------
void renderPageTabs(Feature* f, int x, int y, int w, const ThemeColor& tc) {
	auto pages = f->getPages(); if (pages.empty()) return;
	if (f->currentPage.empty() && !pages.empty()) f->currentPage = pages[0];
	int tabW = 60, tabH = 22, gap = 4;
	for (size_t i = 0; i < pages.size(); ++i) {
		c_rect tab(x + (int)i * (tabW + gap), y, tabW, tabH);
		unsigned int col = (pages[i] == f->currentPage) ? tc.accent : tc.panel;
		drawRoundRect(tab, 6, col);
		drawText(c_rect(tab.l + 4, tab.t, tab.width() - 8, tab.height()), pages[i], tc.text, ALIGN_HCENTER | ALIGN_VCENTER, 12);
		addHit(GuiContext::Hit::PageSelect, tab, f, nullptr, 1, 0, pages[i]);
	}
}

// ----------------------------------------------------------------------------
// 功能行（递归渲染子功能）
// 返回占用高度
// ----------------------------------------------------------------------------
int renderFeatureRow(Feature* f, int x, int y, int w, int indent, const ThemeColor& tc, const std::string& searchFilter) {
	if (!f) return 0;
	if (!searchFilter.empty() && !featureMatchesSearch(f, searchFilter)) return 0;
	c_rect row(x + indent, y, w - indent, 34);
	unsigned int baseColor = CG_ARGB(170, 35, 35, 42);
	if (row.contains(ctx().mouseX, ctx().mouseY)) { baseColor = CG_ARGB(225, 65, 65, 75); ctx().hoveredDesc = f->desc; }
	drawRoundRect(row, 8, baseColor);
	std::string label = (f->children.empty() && f->settings.empty()) ? "  " : (f->expanded ? "v " : "> "); label += f->name;
	drawText(c_rect(row.l + 12, row.t, row.width() - 58, row.height()), label, tc.text, ALIGN_LEFT | ALIGN_VCENTER, 14);

	drawToggle(c_rect(row.r - 52, row.t + 7, 44, 20), f->toggleAnim, tc.border);
	addHit(GuiContext::Hit::ToggleFeat, row, f);
	int used = 40;

	float expEase = easeOutCubic(f->expandAnim);
	if (f->expandAnim <= 0.001f) return used;

	c_rect savedClip = ctx().clipRect;
	int subAreaY = y + used;
	int targetSubH = 0;

	auto pages = f->getPages();
	if (!pages.empty()) targetSubH += 26;
	int shortcutCount = 0;
	int settingsUsed = 0;
	for (auto& st : f->settings) {
		if (isHiddenSetting(st)) continue;
		if (!st.page.empty() && st.page != f->currentPage) continue;
		if (isHotkeySetting(st.name)) shortcutCount = 1;
		else settingsUsed += settingRowUsed(st);
	}
	targetSubH += shortcutCount * 34 + settingsUsed;
	int childSubH = 0;
	for (auto* child : f->children) childSubH += 40 + (int)((child->expandAnim) * 200);
	targetSubH += childSubH;

	int clippedSubH = (int)((float)targetSubH * expEase);
	if (clippedSubH > 0) {
		c_rect subClip(row.l, subAreaY, row.width(), clippedSubH);
		c_rect newClip;
		newClip.l = std::max(savedClip.l, subClip.l);
		newClip.t = std::max(savedClip.t, subClip.t);
		newClip.r = std::min(savedClip.r, subClip.r);
		newClip.b = std::min(savedClip.b, subClip.b);
		if (newClip.l > newClip.r || newClip.t > newClip.b) return used + clippedSubH;
		ctx().clipRect = newClip;
	}

	unsigned int subAlpha = (unsigned char)(255.0f * expEase);
	ThemeColor tcSub = tc;
	if (expEase < 0.99f) {
		tcSub.text = rgbaScaleAlpha(tc.text, expEase);
		tcSub.sub = rgbaScaleAlpha(tc.sub, expEase);
	}

	if (!pages.empty()) { renderPageTabs(f, x + indent + 18, y + used, w - indent - 18, tcSub); used += 26; }

	bool shortcutRendered = false;
	for (auto& st : f->settings) {
		if (isHiddenSetting(st)) continue;
		if (!st.page.empty() && st.page != f->currentPage) continue;

		if (isHotkeySetting(st.name)) {
			if (!shortcutRendered) {
				shortcutRendered = true;
				Setting* txtSet = f->findSetting("ShortcutText");
				std::string shortcutText = txtSet ? txtSet->asString() : "无";
				c_rect sr(x + indent + 18, y + used, w - indent - 18, 30);
				unsigned int setBase = CG_ARGB(subAlpha, 26, 26, 32);
				if (sr.contains(ctx().mouseX, ctx().mouseY)) setBase = CG_ARGB(subAlpha, 55, 55, 65);
				drawRoundRect(sr, 6, setBase);
				drawText(c_rect(sr.l + 10, sr.t, sr.width() / 2, sr.height()), "快捷键", tcSub.sub, ALIGN_LEFT | ALIGN_VCENTER, 13);
				drawText(c_rect(sr.l + sr.width() / 2, sr.t, sr.width() / 2 - 14, sr.height()), shortcutText, tcSub.text, ALIGN_RIGHT | ALIGN_VCENTER, 13);
				addHit(GuiContext::Hit::CaptureShortcut, sr, f, nullptr, 1);
				addHit(GuiContext::Hit::CaptureShortcut, sr, f, nullptr, -1);
				used += 34;
			}
			continue;
		}

		bool isColor = isColorSetting(st);
		if (isColor) {
			c_rect sr(x + indent + 18, y + used, w - indent - 18, 30);
			unsigned int setBase = CG_ARGB(135, 26, 26, 32);
			if (expEase < 0.99f) setBase = CG_ARGB((unsigned char)(135.0f * expEase + 0.5f), 26, 26, 32);
			if (sr.contains(ctx().mouseX, ctx().mouseY)) setBase = expEase < 0.99f ? CG_ARGB((unsigned char)(190.0f * expEase), 55, 55, 65) : CG_ARGB(190, 55, 55, 65);
			drawRoundRect(sr, 6, setBase);
			drawText(c_rect(sr.l + 10, sr.t, sr.width() / 2, sr.height()), st.name, tcSub.sub, ALIGN_LEFT | ALIGN_VCENTER, 13);
			int rgb = st.asInt(); unsigned int color = CG_ARGB(255, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
			c_rect colorRect(sr.l + sr.width() / 2, sr.t + 4, 22, 22); fillRect(colorRect, color);
			strokeRoundRect(colorRect, 4, 1.0f, tc.border);
			addHit(GuiContext::Hit::PickColor, colorRect, f, &st);
			addHit(GuiContext::Hit::PickColor, sr, f, &st);
			used += 34;
		} else if (st.type == SettingType::Select) {
			c_rect sr(x + indent + 18, y + used, w - indent - 18, 30);
			unsigned int setBase = CG_ARGB(135, 26, 26, 32);
			if (expEase < 0.99f) setBase = CG_ARGB((unsigned char)(135.0f * expEase + 0.5f), 26, 26, 32);
			if (sr.contains(ctx().mouseX, ctx().mouseY)) setBase = expEase < 0.99f ? CG_ARGB((unsigned char)(190.0f * expEase), 55, 55, 65) : CG_ARGB(190, 55, 55, 65);
			drawRoundRect(sr, 6, setBase);
			drawText(c_rect(sr.l + 10, sr.t, sr.width() / 2, sr.height()), st.name, tcSub.sub, ALIGN_LEFT | ALIGN_VCENTER, 13);
			int selW = (int)(sr.width() * 0.42f);
			int selX = sr.r - selW - 8;
			c_rect selectBox(selX, sr.t + 4, selW, sr.height() - 8);
			unsigned int selBg = CG_ARGB(210, 50, 50, 60);
			if (selectBox.contains(ctx().mouseX, ctx().mouseY)) selBg = CG_ARGB(240, 80, 90, 110);
			bool isOpen = ctx().selectDropdown.active && ctx().selectDropdown.set == &st;
			if (isOpen) selBg = CG_ARGB(255, 100, 120, 160);
			drawRoundRect(selectBox, 5, selBg);
			strokeRoundRect(selectBox, 5, 1.0f, tc.border);
			std::string curVal = st.text();
			drawText(c_rect(selectBox.l + 6, selectBox.t, selectBox.width() - 22, selectBox.height()), curVal, tcSub.text, ALIGN_LEFT | ALIGN_VCENTER, 12);
			std::string arrow = isOpen ? "^" : "v";
			drawText(c_rect(selectBox.r - 18, selectBox.t, 16, selectBox.height()), arrow, tcSub.text, ALIGN_HCENTER | ALIGN_VCENTER, 11, FW_BOLD);
			addHit(GuiContext::Hit::SelectDropdown, selectBox, f, &st);
			addHit(GuiContext::Hit::SelectDropdown, sr, f, &st);
			if (isOpen) ctx().selectDropdown.triggerRect = selectBox;
			used += 34;
		} else if (isSliderSetting(st)) {
			// 现代滑条：名称左上、当前值右上、底部滑轨 + 填充 + 圆形手柄，整行可拖拽
			c_rect sr(x + indent + 18, y + used, w - indent - 18, 40);
			unsigned int setBase = CG_ARGB(135, 26, 26, 32);
			if (expEase < 0.99f) setBase = CG_ARGB((unsigned char)(135.0f * expEase + 0.5f), 26, 26, 32);
			if (sr.contains(ctx().mouseX, ctx().mouseY)) setBase = expEase < 0.99f ? CG_ARGB((unsigned char)(190.0f * expEase), 55, 55, 65) : CG_ARGB(190, 55, 55, 65);
			drawRoundRect(sr, 6, setBase);
			drawText(c_rect(sr.l + 10, sr.t + 2, sr.width() / 2, 18), st.name, tcSub.sub, ALIGN_LEFT | ALIGN_VCENTER, 13);
			drawText(c_rect(sr.l + sr.width() / 2 - 14, sr.t + 2, sr.width() / 2, 18), st.text(), tcSub.text, ALIGN_RIGHT | ALIGN_VCENTER, 13);
			float frac = (st.maxVal > st.minVal) ? (st.asFloat() - st.minVal) / (st.maxVal - st.minVal) : 0.0f;
			frac = std::clamp(frac, 0.0f, 1.0f);
			int tx0 = sr.l + 12, tx1 = sr.r - 12;
			int trackY = sr.t + 26, trackH = 8;
			c_rect track(tx0, trackY, tx1 - tx0 + 1, trackH);
			bool draggingThis = ctx().sliderDrag.active && ctx().sliderDrag.set == &st;
			drawRoundRect(track, trackH / 2, CG_ARGB(150, 45, 45, 55));
			int fillW = (int)((float)(tx1 - tx0 + 1) * easeOutCubic(frac) + 0.5f);
			if (fillW > trackH) drawRoundRect(c_rect(tx0, trackY, fillW, trackH), trackH / 2, tc.accent);
			int knobR = 6;
			int knobX = tx0 + (int)((float)(tx1 - tx0) * easeOutCubic(frac) + 0.5f);
			drawRoundRect(c_rect(knobX - knobR, trackY + trackH / 2 - knobR, knobR * 2, knobR * 2), knobR,
				draggingThis ? CG_RGB(255, 255, 255) : rgbaLerp(CG_ARGB(210, 210, 210, 220), tc.accent, 0.35f));
			addHit(GuiContext::Hit::DragSlider, sr, f, &st);
			used += 44;
		} else {
			// Bool 开关 / String 文本
			c_rect sr(x + indent + 18, y + used, w - indent - 18, 30);
			unsigned int setBase = CG_ARGB(135, 26, 26, 32);
			if (expEase < 0.99f) setBase = CG_ARGB((unsigned char)(135.0f * expEase + 0.5f), 26, 26, 32);
			if (sr.contains(ctx().mouseX, ctx().mouseY)) setBase = expEase < 0.99f ? CG_ARGB((unsigned char)(190.0f * expEase), 55, 55, 65) : CG_ARGB(190, 55, 55, 65);
			drawRoundRect(sr, 6, setBase);
			drawText(c_rect(sr.l + 10, sr.t, sr.width() / 2, sr.height()), st.name, tcSub.sub, ALIGN_LEFT | ALIGN_VCENTER, 13);
			drawText(c_rect(sr.l + sr.width() / 2, sr.t, sr.width() / 2 - 14, sr.height()), st.text(), tcSub.text, ALIGN_RIGHT | ALIGN_VCENTER, 13);
			addHit(st.type == SettingType::Bool ? GuiContext::Hit::ToggleSetting : GuiContext::Hit::StepSetting, sr, f, &st);
			used += 34;
		}
	}
	for (auto* child : f->children) used += renderFeatureRow(child, x, y + used, w, indent + 18, tcSub, searchFilter);
	ctx().clipRect = savedClip;
	return used;
}

// ----------------------------------------------------------------------------
// 主面板
// ----------------------------------------------------------------------------
void renderClickGui() {
	GuiContext& c = ctx();
	if (c.style.guiAlpha == 0) return;
	c.hoveredDesc.clear();
	float ease = easeOutCubic(c.guiAnim);
	if (ease <= 0.001f) return;
	float alphaMult = ease;
	float popScale = 0.88f + 0.12f * ease;

	float scale;
	int gw, gh, baseX, baseY;
	scale = c.style.guiScale * c.systemScale;
	gw = (int)((float)std::min(680, std::max(420, c.w - 120)) * scale * popScale);
	gh = (int)((float)std::min(600, std::max(360, c.h - 120)) * scale * popScale);
	baseX = (c.w - gw) / 2;
	baseY = (c.h - gh) / 2;
	int gx = baseX + (int)((float)c.panelOffsetX * popScale);
	int gy = baseY + (int)((float)c.panelOffsetY * popScale);
	c.clipRect.set(gx, gy, gw, gh);

	unsigned char effAlpha = (unsigned char)std::max(0, std::min(255, (int)((float)c.style.guiAlpha * alphaMult + 0.5f)));
	ThemeColor tc = getTheme(Theme::Purple, effAlpha);
	ThemeColor tcDim = themeScaleAlpha(tc, 0.78f);
	(void)tcDim;

	c_rect panel(gx, gy, gw, gh);
	// 柔和投影 + 面板主体
	drawSoftShadow(panel, (int)(18 * scale * popScale), (int)(14 * scale), rgbaScaleAlpha(CG_ARGB(120, 6, 6, 10), ease));
	drawRoundRect(panel, (int)(18 * scale * popScale), tc.bg);
	unionRect(c.contentRect, inflateRect(panel, (int)(26 * scale)));
	{
		// 标题栏（可拖拽）+ 关闭按钮
		c_rect titleBar(gx, gy, gw, (int)(38 * scale * popScale));
		drawText(c_rect(gx + (int)(22 * scale * popScale), gy + (int)(12 * scale * popScale), gw - (int)(80 * scale * popScale), (int)(34 * scale * popScale)),
			c.style.title, tc.text, ALIGN_LEFT | ALIGN_VCENTER, (int)(18 * scale * popScale), FW_BOLD);
		addHit(GuiContext::Hit::DragPanel, titleBar);
		c_rect closeBtn(gx + gw - (int)(44 * scale * popScale), gy + (int)(12 * scale * popScale), (int)(28 * scale * popScale), (int)(28 * scale * popScale));
		bool hoverClose = closeBtn.contains(c.mouseX, c.mouseY);
		drawRoundRect(closeBtn, (int)(14 * scale * popScale), hoverClose ? rgbaLerp(CG_ARGB(200, 80, 50, 62), CG_ARGB(235, 210, 90, 105), 0.65f) : CG_ARGB(200, 80, 50, 62));
		drawText(closeBtn, "x", CG_RGB(255, 255, 255), ALIGN_HCENTER | ALIGN_VCENTER, (int)(16 * scale * popScale));
		addHit(GuiContext::Hit::CloseGui, closeBtn);

		// 原地搜索框（点击后直接键入，实时过滤）
		c_rect searchBox(gx + gw - (int)(250 * scale * popScale), gy + (int)(42 * scale * popScale), (int)(160 * scale * popScale), (int)(24 * scale * popScale));
		c.searchBoxRect = searchBox;
		bool hoverSearch = searchBox.contains(c.mouseX, c.mouseY) || c.searchActive;
		drawRoundRect(searchBox, (int)(6 * scale * popScale), hoverSearch ? rgbaLerp(CG_ARGB(180, 40, 40, 50), tc.accent, 0.22f) : CG_ARGB(180, 40, 40, 50));
		std::string shownText = searchText.empty() ? "搜索..." : searchText;
		c_rect textRect(searchBox.l + (int)(6 * scale * popScale), searchBox.t, searchBox.width() - (int)(12 * scale * popScale), searchBox.height());
		drawText(textRect, shownText, searchText.empty() ? tc.sub : tc.text, ALIGN_LEFT | ALIGN_VCENTER, (int)(13 * scale * popScale));
		if (c.searchActive && c.cursorVisible) {
			// 文本末尾闪烁光标（宽度按字符数粗略估算，ASCII 半宽 7px、其他 13px）
			int tw = 0;
			for (unsigned char ch : searchText) tw += (ch < 0x80) ? 7 : 13;
			int caretX = searchBox.l + (int)(8 * scale * popScale) + tw;
			caretX = std::min(caretX, searchBox.r - 6);
			drawRect(c_rect(caretX, searchBox.t + 5, 2, searchBox.height() - 10), tc.text, 1);
		}
		addHit(GuiContext::Hit::SearchBoxClick, searchBox);

		auto cats = Registry::instance().categories();
		if (cats.empty()) { drawText(c_rect(gx + 20, gy + 70, gw - 40, 32), "未注册任何功能。", tc.sub, ALIGN_LEFT | ALIGN_VCENTER); return; }
		if (c.selCat < 0) c.selCat = 0;
		if (c.selCat >= (int)cats.size()) c.selCat = 0;
		int tabX = gx + (int)(18 * scale * popScale), tabW = (int)(150 * scale * popScale);
		int catTop = gy + (int)(74 * scale * popScale);
		int selY = catTop + c.selCat * (int)(38 * scale * popScale);
		int tabH = (int)(32 * scale * popScale);
		for (size_t i = 0; i < cats.size(); ++i) {
			c_rect tab(tabX, catTop + (int)i * (int)(38 * scale * popScale), tabW, tabH);
			bool hover = tab.contains(c.mouseX, c.mouseY);
			bool isSel = (i == (size_t)c.selCat);
			unsigned int baseCol = isSel ? tc.accent : tc.panel;
			if (hover && !isSel) baseCol = rgbaLerp(tc.panel, tc.accent, 0.32f);
			drawRoundRect(tab, (int)(8 * scale * popScale), baseCol);
			drawText(c_rect(tab.l + (int)(10 * scale * popScale), tab.t, tab.width() - (int)(20 * scale * popScale), tab.height()), cats[i], tc.text, ALIGN_LEFT | ALIGN_VCENTER, (int)(14 * scale * popScale));
			addHit(GuiContext::Hit::SelectCat, tab, nullptr, nullptr, (int)i, 0, "");
		}
		{
			// 选中分类指示条（滑动动画）
			float catEase = easeInOutCubic(c.catSwitchT);
			int srcY = (c.prevSelCat >= 0 && c.prevSelCat < (int)cats.size()) ? (catTop + c.prevSelCat * (int)(38 * scale * popScale)) : selY;
			int indY = srcY + (int)((float)(selY - srcY) * catEase);
			c_rect indRect(tabX - 2, indY, 4, tabH);
			unsigned int indCol = tc.accent;
			indCol = rgbaScaleAlpha(indCol, 0.95f);
			drawRoundRect(indRect, 2, indCol);
		}

		// 功能列表 + 裁剪滚动
		int listX = gx + (int)(186 * scale * popScale);
		int viewTop = catTop;
		int viewBottom = gy + gh - (int)(10 * scale * popScale);
		int listW = gw - (int)(214 * scale * popScale);
		int clipH = viewBottom - viewTop;

		// 第一遍：测量总高度
		int totalH = 0;
		std::string currentSearch = searchText;
		c_rect savedClip = c.clipRect;
		c.clipRect.set(0, 0, 0, 0);
		for (auto* f : Registry::instance().allFeatures()) {
			if (!f || !f->visibleInClickGui) continue;
			if (!currentSearch.empty()) {
				if (!featureMatchesSearch(f, currentSearch)) continue;
			} else {
				if (f->category != cats[c.selCat]) continue;
			}
			totalH += renderFeatureRow(f, listX, viewTop + totalH, listW, 0, tc, currentSearch);
		}
		c.clipRect = savedClip;

		if (totalH == 0) { drawText(c_rect(listX, viewTop, listW, 30), "暂无功能", tc.sub, ALIGN_LEFT | ALIGN_VCENTER); }
		if (c.scrollY > totalH - clipH) c.scrollY = std::max(0, totalH - clipH);
		if (c.scrollY < 0) c.scrollY = 0;

		// 第二遍：带偏移实际渲染
		c.clipRect.set(listX, viewTop, listW, clipH);
		int yy = viewTop - c.scrollY;
		for (auto* f : Registry::instance().allFeatures()) {
			if (!f || !f->visibleInClickGui) continue;
			if (!currentSearch.empty()) {
				if (!featureMatchesSearch(f, currentSearch)) continue;
			} else {
				if (f->category != cats[c.selCat]) continue;
			}
			int used = renderFeatureRow(f, listX, yy, listW, 0, tc, currentSearch);
			yy += used;
			if (yy > viewBottom) break;
		}
		c.clipRect.set(0, 0, c.w, c.h);

		// 滚动条
		if (totalH > clipH) {
			int barW = c.style.scrollBarWidth;
			int barH = std::max(30, clipH * clipH / totalH);
			int barPos = (totalH > clipH) ? ((clipH - barH) * c.scrollY / (totalH - clipH)) : 0;
			c_rect bar(listX + listW + 4, viewTop + barPos, barW, barH);
			drawRoundRect(bar, barW / 2, tc.accent);
			addHit(GuiContext::Hit::ScrollBar, bar);
		}

		// 悬停描述（底部一行）
		int descFnt = std::max(10, (int)(12 * popScale));
		if (!c.hoveredDesc.empty()) { drawText(c_rect(gx + 10, gy + gh - 24, gw - 20, 20), c.hoveredDesc, tc.sub, ALIGN_LEFT | ALIGN_VCENTER, descFnt); }
	}
}

// ----------------------------------------------------------------------------
// Select 下拉框浮层
// ----------------------------------------------------------------------------
void renderSelectDropdownOverlay() {
	GuiContext& c = ctx();
	if (!c.selectDropdown.active) return;
	Feature* f = c.selectDropdown.feat;
	Setting* s = c.selectDropdown.set;
	if (!f || !s || s->options.empty()) return;
	c_rect trigger = c.selectDropdown.triggerRect;
	if (trigger.width() <= 0) return;

	int optCount = (int)s->options.size();
	int optH = 28;
	int panelPad = 4;
	int panelW = trigger.width() + 8;
	int panelH = optCount * optH + panelPad * 2;
	int px = trigger.l - 4;
	int py = trigger.b + 2;
	if (py + panelH > c.h - 10) py = trigger.t - panelH - 2;
	py = std::max(10, std::min(c.h - panelH - 10, py));
	px = std::max(10, std::min(c.w - panelW - 10, px));

	c_rect savedClip = c.clipRect;
	c.clipRect.set(0, 0, c.w - 1, c.h - 1);

	ThemeColor tc = getTheme(Theme::Purple, c.style.guiAlpha);
	unsigned int panelBg = CG_ARGB(245, 36, 36, 44);
	unsigned int borderCol = tc.border;

	c_rect panelRect(px, py, panelW, panelH);
	drawSoftShadow(panelRect, 8, 8, CG_ARGB(90, 6, 6, 10));
	drawRoundRect(panelRect, 8, panelBg);
	strokeRoundRect(panelRect, 8, 1.0f, borderCol);
	unionRect(c.contentRect, inflateRect(panelRect, 10));

	int selIdx = s->selectIndex();
	for (int i = 0; i < optCount; ++i) {
		int oy = py + panelPad + i * optH;
		c_rect optRect(px + 4, oy, panelW - 8, optH - 2);
		bool hovered = optRect.contains(c.mouseX, c.mouseY);
		bool selected = (i == selIdx);
		unsigned int optBg = 0;
		if (selected) optBg = tc.accent;
		else if (hovered) optBg = CG_ARGB(200, 70, 80, 100);
		if (optBg) drawRoundRect(optRect, 5, optBg);
		unsigned int txtCol = (selected || hovered) ? CG_RGB(255, 255, 255) : tc.text;
		drawText(c_rect(optRect.l + 8, optRect.t, optRect.width() - 12, optRect.height()),
			s->options[i], txtCol, ALIGN_LEFT | ALIGN_VCENTER, 12);
		GuiContext::Hit h;
		h.type = GuiContext::Hit::SelectOption;
		h.rect = optRect;
		h.feat = f;
		h.set = s;
		h.dir = 0;
		h.toastId = 0;
		h.optionIndex = i;
		c.hits.push_back(h);
	}
	c.clipRect = savedClip;
}

// ----------------------------------------------------------------------------
// 每帧总渲染入口
// ----------------------------------------------------------------------------
void renderAll() {
	GuiContext& c = ctx(); if (!c.init || c.w <= 0 || c.h <= 0) return;
	size_t requiredSize = (size_t)c.w * c.h;
	if (c.fb.size() != requiredSize) c.fb.resize(requiredSize, 0);
	if (c.preFb.size() != requiredSize) c.preFb.resize(requiredSize, 0);
	if (c.clipRect.r == 0 && c.clipRect.b == 0) c.clipRect.set(0, 0, c.w - 1, c.h - 1);
	anim::updateAnims();
	anim::updateToasts();
	c.hits.clear();
	c.contentRect.set(0, 0, 0, 0);
	memset(c.fb.data(), 0, c.fb.size() * sizeof(unsigned int));

	bool shouldDrawGui = c.guiAnim > 0.005f;
	if (shouldDrawGui) renderClickGui();
	renderSelectDropdownOverlay();
	renderToast();
}

} // namespace render
} // namespace detail
} // namespace cgui
