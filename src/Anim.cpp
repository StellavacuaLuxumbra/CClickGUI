// CClickGUI 动画推进：面板开合、分类切换、行展开、开关滑块、Toast 进出场。

#include "Internal.h"
#include "Util.h"

namespace cgui {
namespace detail {
namespace anim {

using cgui::detail::ctx;
using namespace cgui::detail::util;

void updateToasts() {
	GuiContext& c = ctx(); unsigned long long now = nowMs();
	float dt = c.animDt > 0.1f ? c.animDt : 16.0f;
	NotifyStyle& ns = c.notifyStyle;
	float ENTER_SPD = (ns.enterSpeed > 0.0001f) ? ns.enterSpeed : 0.020f;
	float EXIT_SPD = (ns.exitSpeed > 0.0001f) ? ns.exitSpeed : 0.022f;
	float CONTENT_SPD = (ns.contentSpeed > 0.0001f) ? ns.contentSpeed : 0.025f;

	// 已过期 Toast 淡出移除 / 进行中 Toast 淡入
	for (size_t i = 0; i < c.toasts.size();) {
		auto& t = c.toasts[i];
		unsigned long long age = now - t.start;
		bool expired = !t.active || (age >= (unsigned long long)t.dur);
		if (expired) {
			float step = std::clamp(dt * EXIT_SPD, 0.0f, 1.0f);
			t.progress *= (1.0f - step);
			t.progress -= step * 0.04f;
			if (t.progress <= 0.005f) { c.toasts.erase(c.toasts.begin() + (long)i); continue; }
		} else {
			float step = std::clamp(dt * ENTER_SPD, 0.0f, 1.0f);
			t.progress = t.progress + (1.0f - t.progress) * step + 0.06f;
			if (t.progress > 1.0f) t.progress = 1.0f;
		}
		if (t.contentT < 1.0f) {
			float s = std::clamp(dt * CONTENT_SPD, 0.0f, 1.0f);
			t.contentT = t.contentT + (1.0f - t.contentT) * s + 0.06f;
			if (t.contentT > 1.0f) t.contentT = 1.0f;
		}
		++i;
	}

	// 队列 → 显示槽位
	int maxSlots = std::min(8, std::max(1, (c.h - 28) / (int)(74 * c.systemScale)));
	while ((int)c.toasts.size() < maxSlots && !c.queue.empty()) {
		GuiContext::Toast t = c.queue.front(); c.queue.pop_front(); t.start = now; t.active = true; t.id = c.nextId++;
		if (t.progress < 0.01f) t.progress = 0.0f;
		if (t.prevTitle.empty()) { t.prevTitle = t.title; t.prevMsg = t.msg; }
		if (t.contentT < 0.98f) t.contentT = 1.0f;
		t.lastUpdate = now;
		t.visualY = -1.0f;
		c.toasts.push_back(t);
	}

	// 槽位间平滑下移
	{
		float scale = ns.scale * c.systemScale;
		int minH = (int)(66 * scale);
		int gap = (int)(8 * scale), top = (int)(18 * scale) + (int)(ns.offsetY * c.systemScale);
		float Y_SPD = (ns.moveYSpeed > 0.0001f) ? ns.moveYSpeed : 0.040f;
		for (size_t i = 0; i < c.toasts.size(); ++i) {
			auto& t = c.toasts[i];
			int tgtH = (t.targetBaseH > 0) ? t.targetBaseH : minH;
			int prvH = (t.prevBaseH > 0) ? t.prevBaseH : tgtH;
			int curH = (int)((float)prvH + (float)(tgtH - prvH) * easeInOutCubic(t.contentT));
			curH = std::max(minH, curH);
			float targetY = (float)top + (float)i * (float)(std::max(minH, curH) + gap);
			if (t.visualY < 0.0f) t.visualY = targetY;
			float step = std::clamp(dt * Y_SPD, 0.0f, 1.0f);
			float diff = targetY - t.visualY;
			t.visualY = t.visualY + diff * step + diff * 0.10f;
		}
	}
}

void updateAnims() {
	GuiContext& c = ctx();
	unsigned long long now = nowMs();
	float dt = (float)(now - c.lastFrameTime < 500 ? now - c.lastFrameTime : 16ULL);
	if (dt < 0.1f) dt = 0.1f;
	if (dt > 32.0f) dt = 32.0f;
	c.animDt = dt;
	c.lastFrameTime = now;

	// 面板开合
	constexpr float GUI_SPEED = 0.010f;
	c.guiAnimTarget = c.guiVisible;
	float target = c.guiAnimTarget ? 1.0f : 0.0f;
	float diff = target - c.guiAnim;
	float step = diff * std::clamp(dt * GUI_SPEED, 0.0f, 1.0f);
	float bias = (diff > 0) ? 0.014f : -0.014f;
	float rawStep = std::clamp(step + bias, -0.22f, 0.22f);
	if (fabsf(diff) < 0.0015f) c.guiAnim = target;
	else c.guiAnim = std::clamp(c.guiAnim + rawStep, 0.0f, 1.0f);

	// 分类切换指示条
	if (c.prevSelCat != c.selCat) {
		c.prevSelCat = c.selCat;
		c.catSwitchT = 0.0f;
	}
	constexpr float CS_SPEED = 0.015f;
	if (c.catSwitchT < 1.0f) {
		float s = std::clamp(dt * CS_SPEED, 0.0f, 1.0f);
		float csRaw = std::clamp((1.0f - c.catSwitchT) * s + 0.028f, 0.0f, 0.30f);
		c.catSwitchT = std::min(1.0f, c.catSwitchT + csRaw);
		if (c.catSwitchT > 0.999f) c.catSwitchT = 1.0f;
	}

	// 搜索光标闪烁
	unsigned long long blinkNow = nowMs();
	if (blinkNow - c.lastCursorBlink > 500) { c.lastCursorBlink = blinkNow; c.cursorVisible = !c.cursorVisible; }

	// 行展开 / 开关滑块动画
	constexpr float EXPAND_SPEED = 0.0085f;
	constexpr float TOGGLE_SPEED = 0.028f;
	for (auto* f : Registry::instance().allFeatures()) {
		if (!f) continue;
		float tgt = f->expanded ? 1.0f : 0.0f;
		float diffE = tgt - f->expandAnim;
		if (fabsf(diffE) < 0.0015f) f->expandAnim = tgt;
		else {
			float eStep = std::clamp(diffE * std::clamp(dt * EXPAND_SPEED, 0.0f, 1.0f) + (diffE > 0 ? 0.010f : -0.010f), -0.20f, 0.20f);
			f->expandAnim += eStep;
		}
		f->expandAnim = std::clamp(f->expandAnim, 0.0f, 1.0f);

		float tgtT = f->enabled ? 1.0f : 0.0f;
		float diffT = tgtT - f->toggleAnim;
		if (fabsf(diffT) < 0.002f) f->toggleAnim = tgtT;
		else {
			float tStep = std::clamp(diffT * std::clamp(dt * TOGGLE_SPEED, 0.0f, 1.0f) + (diffT > 0 ? 0.045f : -0.045f), -0.32f, 0.32f);
			f->toggleAnim += tStep;
		}
		f->toggleAnim = std::clamp(f->toggleAnim, 0.0f, 1.0f);
	}
}

} // namespace anim
} // namespace detail
} // namespace cgui
