// CClickGUI 绘图基元：GDI 文字渲染到软件帧缓冲（灰度抗锯齿）+
// SDF（有符号距离场）抗锯齿的圆角矩形 / 描边 / 柔和投影。

#include "Internal.h"
#include "Util.h"

namespace cgui {
namespace detail {
namespace draw {

using cgui::detail::ctx;
using namespace cgui::detail::util;

// 按覆盖率写入像素（cov ∈ [0,1]，与帧缓冲做 Alpha 混合）
static inline void putPixelCov(int x, int y, float cov, unsigned int col) {
	if (cov <= 0.0f) return;
	GuiContext& c = ctx();
	if (x < 0 || y < 0 || x >= c.w || y >= c.h) return;
	if (!util::inClip(x, y)) return;
	unsigned int a = CG_A(col);
	if (cov < 1.0f) {
		int na = (int)((float)a * cov + 0.5f);
		if (na <= 0) return;
		col = (col & 0x00FFFFFFu) | ((unsigned int)na << 24);
	}
	size_t idx = (size_t)y * c.w + x;
	c.fb[idx] = util::alphaBlend(c.fb[idx], col);
}

// 圆角矩形的连续空间包围参数（像素 (x,y) 的采样中心为 x+0.5, y+0.5）
struct RoundRectSDF {
	float cx, cy, hx, hy, rad;
	RoundRectSDF(const c_rect& r, int radius) {
		cx = (r.l + r.r + 1) * 0.5f;
		cy = (r.t + r.b + 1) * 0.5f;
		hx = (r.r - r.l + 1) * 0.5f;
		hy = (r.b - r.t + 1) * 0.5f;
		rad = std::min((float)radius, std::min(hx, hy));
	}
	// 像素中心到形状边缘的有符号距离（负 = 内部）；经典 rounded-box SDF
	[[nodiscard]] float dist(int x, int y) const {
		float qx = std::abs((float)x + 0.5f - cx) - (hx - rad);
		float qy = std::abs((float)y + 0.5f - cy) - (hy - rad);
		float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
		return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - rad;
	}
};

// 把文字经 GDI DrawTextW 画到临时 DIB，再按文字覆盖率混合到帧缓冲（支持任意颜色/对齐/省略号）
void drawText(c_rect r, const std::string& text, unsigned int color, unsigned int align, int fontSize, int weight) {
	if (text.empty() || r.width() <= 0 || r.height() <= 0) return;
	int w = r.width(), h = r.height();
	HDC screen = GetDC(nullptr); HDC dc = CreateCompatibleDC(screen);
	BITMAPINFO bmi = {}; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bmi.bmiHeader.biWidth = w; bmi.bmiHeader.biHeight = -h; bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32;
	void* bits = nullptr;
	HBITMAP bmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!bmp || !bits) { if (bmp) DeleteObject(bmp); DeleteDC(dc); ReleaseDC(nullptr, screen); return; }
	memset(bits, 0, (size_t)w * h * 4);
	HGDIOBJ oldBmp = SelectObject(dc, bmp);
	// 灰度抗锯齿：ClearType 的子像素排列在透明背景上会产生彩边
	HFONT font = CreateFontW(-fontSize, 0, 0, 0, weight, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
		util::fontNameW().c_str());
	HGDIOBJ oldFont = SelectObject(dc, font);
	SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
	RECT tr = { 0, 0, w, h };
	UINT flags = DT_SINGLELINE | DT_END_ELLIPSIS;
	if ((align & ALIGN_LEFT) == ALIGN_LEFT) flags |= DT_LEFT; else if ((align & ALIGN_RIGHT) == ALIGN_RIGHT) flags |= DT_RIGHT; else flags |= DT_CENTER;
	if ((align & ALIGN_TOP) == ALIGN_TOP) flags |= DT_TOP; else if ((align & ALIGN_BOTTOM) == ALIGN_BOTTOM) flags |= DT_BOTTOM; else flags |= DT_VCENTER;
	std::wstring ws = util::toWide(text);
	DrawTextW(dc, ws.c_str(), -1, &tr, flags);
	unsigned int a = CG_A(color); if (a == 0) a = 255;
	unsigned int cr = CG_R(color), cg = CG_G(color), cb = CG_B(color);
	unsigned int* pix = (unsigned int*)bits;
	for (int yy = 0; yy < h; ++yy) {
		for (int xx = 0; xx < w; ++xx) {
			unsigned int p = pix[yy * w + xx];
			unsigned int intensity = std::max(CG_R(p), std::max(CG_G(p), CG_B(p)));
			if (intensity == 0) continue;
			putPixelCov(r.l + xx, r.t + yy, (float)intensity / 255.0f, CG_ARGB(a, cr, cg, cb));
		}
	}
	SelectObject(dc, oldFont); DeleteObject(font);
	SelectObject(dc, oldBmp); DeleteObject(bmp);
	DeleteDC(dc); ReleaseDC(nullptr, screen);
}

void putPixel(int x, int y, unsigned int col) {
	putPixelCov(x, y, 1.0f, col);
}

void fillRect(c_rect r, unsigned int col) {
	GuiContext& c = ctx();
	int l = std::max(0, r.l), t = std::max(0, r.t), rr = std::min(c.w - 1, r.r), b = std::min(c.h - 1, r.b);
	for (int y = t; y <= b; ++y) for (int x = l; x <= rr; ++x) putPixel(x, y, col);
}

void drawRect(c_rect r, unsigned int col, int w) {
	for (int i = 0; i < w; ++i) {
		for (int x = r.l + i; x <= r.r - i; ++x) putPixel(x, r.t + i, col);
		for (int x = r.l + i; x <= r.r - i; ++x) putPixel(x, r.b - i, col);
		for (int y = r.t + i; y <= r.b - i; ++y) putPixel(r.l + i, y, col);
		for (int y = r.t + i; y <= r.b - i; ++y) putPixel(r.r - i, y, col);
	}
}

void drawRoundRect(c_rect r, int rad, unsigned int col) {
	if (rad <= 1) { fillRect(r, col); return; }
	GuiContext& c = ctx();
	int L = std::max(0, r.l), T = std::max(0, r.t), R = std::min(c.w - 1, r.r), B = std::min(c.h - 1, r.b);
	RoundRectSDF sdf(r, rad);
	for (int y = T; y <= B; ++y) {
		for (int x = L; x <= R; ++x) {
			float cov = 0.5f - sdf.dist(x, y);
			if (cov <= 0.0f) continue;
			putPixelCov(x, y, std::min(cov, 1.0f), col);
		}
	}
}

void strokeRoundRect(c_rect r, int rad, float thickness, unsigned int col) {
	GuiContext& c = ctx();
	if (rad <= 1) { drawRect(r, col, std::max(1, (int)(thickness + 0.5f))); return; }
	int L = std::max(0, r.l - 2), T = std::max(0, r.t - 2), R = std::min(c.w - 1, r.r + 2), B = std::min(c.h - 1, r.b + 2);
	RoundRectSDF sdf(r, rad);
	float half = thickness * 0.5f;
	for (int y = T; y <= B; ++y) {
		for (int x = L; x <= R; ++x) {
			float cov = 0.5f - (std::abs(sdf.dist(x, y)) - half);
			if (cov <= 0.0f) continue;
			putPixelCov(x, y, std::min(cov, 1.0f), col);
		}
	}
}

// 柔和投影：单次遍历，Alpha 随到边缘距离的平方衰减（近似高斯）
void drawSoftShadow(c_rect r, int rad, int blur, unsigned int col) {
	if (blur <= 0) { drawRoundRect(r, rad, col); return; }
	GuiContext& c = ctx();
	c_rect box(r.l - blur, r.t - blur, r.width() + blur * 2, r.height() + blur * 2);
	int L = std::max(0, box.l), T = std::max(0, box.t), R = std::min(c.w - 1, box.r), B = std::min(c.h - 1, box.b);
	if (L > R || T > B) return;
	RoundRectSDF sdf(r, rad);
	unsigned int baseA = CG_A(col);
	float fblur = (float)blur;
	for (int y = T; y <= B; ++y) {
		for (int x = L; x <= R; ++x) {
			float d = sdf.dist(x, y);
			if (d >= fblur) continue;
			float t = 1.0f - std::max(d, 0.0f) / fblur;      // 1（边缘）→ 0（blur 外）
			float cov = t * t;                                 // 平方衰减
			int na = (int)((float)baseA * cov + 0.5f);
			if (na <= 0) continue;
			putPixelCov(x, y, 1.0f, (col & 0x00FFFFFFu) | ((unsigned int)na << 24));
		}
	}
}

void drawRoundedIconBox(c_rect iconRc, int radius, unsigned int iconBg, const std::string& ch, int chFont, unsigned int chColor) {
	drawRoundRect(iconRc, radius, iconBg);
	drawText(iconRc, ch, chColor, ALIGN_HCENTER | ALIGN_VCENTER, chFont, FW_BOLD);
}

// Toast 内容切换时的交叉淡入淡出文字（新标题/旧标题按 contentT 插值透明度）
void drawFadingText2(int x, int y, int w, int h,
	float ctEase, float progressEase, unsigned int baseTextColor,
	const std::string& prevTitle, const std::string& prevMsg,
	const std::string& title, const std::string& msg,
	int fontTitle, int fontMsg,
	bool expandedNow, bool expandedPrev,
	int linePadY, int titlePadYTop,
	bool multiLineMsg, int* outMsgLines) {
	float oldA = 1.0f - ctEase;
	float newA = ctEase;
	unsigned int oldTx = util::rgbaScaleAlpha(baseTextColor, progressEase * std::max(0.0f, oldA));
	unsigned int newTx = util::rgbaScaleAlpha(baseTextColor, progressEase * std::max(0.0f, newA));
	int titleH2 = expandedPrev ? (int)(fontTitle * 1.56f + 0.5f) : (h - std::max(6, titlePadYTop));
	int titleH = expandedNow ? (int)(fontTitle * 1.56f + 0.5f) : (h - std::max(6, titlePadYTop));
	int tY = y + titlePadYTop;
	int mY = y + (fontTitle > 0 ? (int)(fontTitle * 1.35f + 0.5f) : 0) + linePadY;
	auto drawLine = [&](c_rect r, const std::string& txt, unsigned int col, int sz, int wgt, bool mline) {
		if (txt.empty()) return;
		int ww = r.width(), hh = r.height();
		if (ww <= 0 || hh <= 0) return;
		HDC screen = GetDC(nullptr); HDC dc = CreateCompatibleDC(screen);
		BITMAPINFO bmi = {}; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = ww; bmi.bmiHeader.biHeight = -hh;
		bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32;
		void* bits = nullptr;
		HBITMAP bmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (!bmp || !bits) { if (bmp) DeleteObject(bmp); DeleteDC(dc); ReleaseDC(nullptr, screen); return; }
		memset(bits, 0, (size_t)ww * hh * 4);
		HGDIOBJ oldBmp = SelectObject(dc, bmp);
		HFONT font = CreateFontW(-sz, 0, 0, 0, wgt, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
			util::fontNameW().c_str());
		HGDIOBJ oldFont = SelectObject(dc, font);
		SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
		RECT tr = { 0, 0, ww, hh };
		UINT flags = DT_END_ELLIPSIS | (mline ? DT_WORDBREAK : DT_SINGLELINE) | DT_LEFT | DT_VCENTER;
		std::wstring ws = util::toWide(txt);
		DrawTextW(dc, ws.c_str(), -1, &tr, flags);
		unsigned int a = CG_A(col); if (a == 0) a = 255;
		unsigned int cr = CG_R(col), cg = CG_G(col), cb = CG_B(col);
		unsigned int* pix = (unsigned int*)bits;
		for (int yy = 0; yy < hh; ++yy) {
			for (int xx = 0; xx < ww; ++xx) {
				unsigned int p = pix[yy * ww + xx];
				unsigned int intensity = std::max(CG_R(p), std::max(CG_G(p), CG_B(p)));
				if (intensity == 0) continue;
				putPixelCov(r.l + xx, r.t + yy, (float)intensity / 255.0f, CG_ARGB(a, cr, cg, cb));
			}
		}
		SelectObject(dc, oldFont); DeleteObject(font);
		SelectObject(dc, oldBmp); DeleteObject(bmp);
		DeleteDC(dc); ReleaseDC(nullptr, screen);
	};
	if (oldA > 0.01f && !prevTitle.empty()) {
		drawLine(c_rect(x, tY, w, titleH2), prevTitle, oldTx, fontTitle, FW_BOLD, false);
		if (expandedPrev && !prevMsg.empty()) {
			drawLine(c_rect(x, mY, w, h - (mY - y)), prevMsg, oldTx, fontMsg, FW_NORMAL, multiLineMsg);
		}
	}
	if (newA > 0.01f) {
		drawLine(c_rect(x, tY, w, titleH), title, newTx, fontTitle, FW_BOLD, false);
		if (expandedNow && !msg.empty()) {
			drawLine(c_rect(x, mY, w, h - (mY - y)), msg, newTx, fontMsg, FW_NORMAL, multiLineMsg);
		}
	}
}

void drawToggle(c_rect r, float onFrac, unsigned int borderCol) {
	float t = std::clamp(onFrac, 0.0f, 1.0f);
	float e = easeOutCubic(t);
	unsigned int onBg = CG_RGB(48, 200, 120);
	unsigned int offBg = CG_RGB(70, 70, 78);
	unsigned int back = rgbaLerp(offBg, onBg, e);
	drawRoundRect(r, r.height() / 2, back);
	int knob = r.height() - 8;
	int xL = r.l + 4;
	int xR = r.r - knob - 4;
	int x = xL + (int)((float)(xR - xL) * e + 0.5f);
	drawRoundRect(c_rect(x, r.t + 4, knob, knob), knob / 2, CG_RGB(255, 255, 255));
	strokeRoundRect(r, r.height() / 2, 1.0f, borderCol);
}

// 注册命中区域（与当前裁剪区有交集才有效）
void addHit(int type, c_rect rect, Feature* f, Setting* s, int dir, int tid, const std::string& page) {
	if (!ctx().clipRect.contains(rect.l, rect.t) && !ctx().clipRect.contains(rect.r, rect.b)) {
		bool overlap = false;
		if (ctx().clipRect.contains(rect.l, rect.t)) overlap = true;
		else if (ctx().clipRect.contains(rect.r, rect.t)) overlap = true;
		else if (ctx().clipRect.contains(rect.l, rect.b)) overlap = true;
		else if (ctx().clipRect.contains(rect.r, rect.b)) overlap = true;
		else {
			if (rect.contains(ctx().clipRect.l, ctx().clipRect.t) ||
				rect.contains(ctx().clipRect.r, ctx().clipRect.b)) overlap = true;
		}
		if (!overlap) return;
	}
	GuiContext::Hit h; h.type = (GuiContext::Hit::Type)type; h.rect = rect; h.feat = f; h.set = s; h.dir = dir; h.toastId = tid; h.pageName = page; h.optionIndex = -1;
	ctx().hits.push_back(h);
}

} // namespace draw
} // namespace detail
} // namespace cgui
