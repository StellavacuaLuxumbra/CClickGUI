// CClickGUI Toast 通知：入队（带 key 去重更新）、按文本长度自适应尺寸。

#include "Internal.h"
#include "Util.h"

namespace cgui {

void Notify(const char* t, const char* m, int ms, Theme th, Icon ic) {
	NotifyKeyed("", t, m, ms, th, ic);
}

void NotifyKeyed(const char* key, const char* t, const char* m, int ms, Theme th, Icon ic) {
	using namespace detail;
	std::string kk = key ? key : "";
	std::string tt = t ? t : "";
	std::string mm = m ? m : "";
	GuiContext& c = detail::ctx();
	std::lock_guard<std::recursive_mutex> lock(c.mtx);
	unsigned long long now = util::nowMs();

	// 按消息长度估算尺寸（多行换行测量）
	auto measureToastSize = [&](const std::string& title, const std::string& msg,
		int& outW, int& outH, bool& needsMultiLine) {
		float scale = c.notifyStyle.scale * c.systemScale;
		int fM = (int)(13 * scale);
		int minW = (int)(318 * scale);
		int maxW = (int)(640 * scale);
		std::wstring wmsg = util::toWide(msg);
		std::wstring wttl = util::toWide(title);
		int tryW = minW;
		int len16 = (int)wmsg.size();
		int titleLen16 = (int)wttl.size();
		if (len16 > 28 || titleLen16 > 22) tryW = (int)(420 * scale);
		if (len16 > 52 || titleLen16 > 36) tryW = (int)(540 * scale);
		if (len16 > 84 || titleLen16 > 56) tryW = maxW;
		tryW = std::min(maxW, std::max(minW, tryW));

		int textW = tryW - (int)(72 * scale);
		int msgLines = 1;
		bool multi = false;
		if (!wmsg.empty() && textW > 0) {
			HDC screen = GetDC(nullptr); HDC dc = CreateCompatibleDC(screen);
			HFONT font = CreateFontW(-fM, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				CLEARTYPE_QUALITY, DEFAULT_PITCH,
				util::fontNameW().c_str());
			HGDIOBJ oldFont = SelectObject(dc, font);
			RECT mtr = { 0, 0, textW, 100000 };
			int height = DrawTextW(dc, wmsg.c_str(), -1, &mtr,
				DT_WORDBREAK | DT_LEFT | DT_TOP | DT_NOPREFIX | DT_CALCRECT);
			SelectObject(dc, oldFont); DeleteObject(font);
			DeleteDC(dc); ReleaseDC(nullptr, screen);
			int lineH = std::max(1, fM + fM / 2);
			msgLines = std::max(1, (height + lineH / 2) / lineH);
			if (msgLines > 1) {
				multi = true;
				int bigW = std::min(maxW, (int)(580 * scale));
				if (tryW < bigW && msgLines > 2) tryW = bigW;
			}
		}
		int titleExtraH = 0;
		if (titleLen16 > 0 && (int)wttl.size() > 28) titleExtraH = (int)(10 * scale);
		int baseH = (int)(66 * scale);
		int tgtH = baseH;
		if (msgLines == 2)      tgtH = (int)(86 * scale) + titleExtraH;
		else if (msgLines == 3) tgtH = (int)(108 * scale) + titleExtraH;
		else if (msgLines >= 4) tgtH = (int)(108 * scale) + (msgLines - 3) * (int)(22 * scale) + titleExtraH;
		tgtH = std::max(baseH, tgtH);
		outW = tryW; outH = tgtH; needsMultiLine = multi;
	};
	int tgtW = 0, tgtH = 0; bool needML = false;
	measureToastSize(tt, mm, tgtW, tgtH, needML);
	(void)needML;

	// 同 key 的通知：内容变化时交叉淡入，否则仅续时
	auto tryUpdateToast = [&](detail::GuiContext::Toast& x) -> bool {
		bool match = !kk.empty() && !x.key.empty() && x.key == kk;
		if (!match) return false;
		bool contentChanged = (x.title != tt) || (x.msg != mm) || (x.theme != th) || (x.icon != ic);
		if (contentChanged) {
			if (x.contentT >= 0.999f || x.prevTitle.empty()) {
				x.prevTitle = x.title; x.prevMsg = x.msg;
				float scale = c.notifyStyle.scale * c.systemScale;
				x.prevBaseH = (x.targetBaseH > 0) ? x.targetBaseH : (int)(66 * scale);
				x.prevBaseW = (x.targetBaseW > 0) ? x.targetBaseW : (int)(318 * scale);
			}
			x.title = tt; x.msg = mm; x.theme = th; x.icon = ic;
			x.targetBaseW = tgtW; x.targetBaseH = tgtH;
			x.contentT = 0.0f;
		}
		x.dur = ms; x.start = now; x.active = true; x.lastUpdate = now;
		if (x.progress < 0.3f) x.progress = 0.3f;
		return true;
	};
	for (auto& x : c.toasts) { if (tryUpdateToast(x)) return; }
	for (auto& x : c.queue) { if (tryUpdateToast(x)) return; }

	detail::GuiContext::Toast toast;
	toast.key = kk; toast.title = tt; toast.msg = mm;
	toast.prevTitle = tt; toast.prevMsg = mm;
	toast.dur = ms; toast.theme = th; toast.icon = ic;
	toast.start = now; toast.lastUpdate = now;
	toast.active = false; toast.id = 0; toast.progress = 0.0f;
	toast.contentT = 1.0f;
	toast.prevBaseW = tgtW; toast.prevBaseH = tgtH;
	toast.targetBaseW = tgtW; toast.targetBaseH = tgtH;
	c.queue.push_back(toast);
	while (c.queue.size() > 64) c.queue.pop_front();
}

} // namespace cgui
