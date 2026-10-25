#ifndef CCLICKGUI_UTIL_H
#define CCLICKGUI_UTIL_H

// CClickGUI 内部工具：颜色混合、编码转换、缓动插值、高精度计时（std::chrono）。

#include "Internal.h"

#include <chrono>

namespace cgui {
namespace detail {
namespace util {

inline unsigned int alphaBlend(unsigned int dst, unsigned int src) {
	unsigned int sa = CG_A(src);
	if (sa == 0) return dst;
	if (sa == 255) return src;
	unsigned int da = CG_A(dst);
	unsigned int sr = CG_R(src), sg = CG_G(src), sb = CG_B(src);
	unsigned int dr = CG_R(dst), dg = CG_G(dst), db = CG_B(dst);
	unsigned int outA = sa + (da * (255 - sa) + 127) / 255;
	if (outA == 0) return 0;
	unsigned int outR = (sr * sa + (dr * da * (255 - sa) + 127) / 255) / outA;
	unsigned int outG = (sg * sa + (dg * da * (255 - sa) + 127) / 255) / outA;
	unsigned int outB = (sb * sa + (db * da * (255 - sa) + 127) / 255) / outA;
	return CG_ARGB(outA, outR, outG, outB);
}

inline ThemeColor getCustomTheme(const UIStyle& style) {
	ThemeColor c; c.bg = style.guiBg; c.panel = style.guiBg; c.text = style.guiText; c.sub = style.guiText; c.accent = style.guiAccent; c.border = style.guiBorder; return c;
}

inline ThemeColor getTheme(Theme th, unsigned char alpha) {
	if (ctx().style.custom) return getCustomTheme(ctx().style);
	ThemeColor c;
	switch (th) {
	case Theme::Blue:    c.bg = CG_ARGB(alpha, 16, 24, 38); c.panel = CG_ARGB(alpha, 22, 34, 54); c.text = CG_RGB(255, 255, 255); c.sub = CG_RGB(205, 220, 240); c.accent = CG_RGB(75, 145, 255); c.border = CG_ARGB(alpha, 75, 145, 255); break;
	case Theme::Success: c.bg = CG_ARGB(alpha, 12, 30, 22); c.panel = CG_ARGB(alpha, 20, 42, 30); c.text = CG_RGB(255, 255, 255); c.sub = CG_RGB(205, 235, 220); c.accent = CG_RGB(60, 220, 130); c.border = CG_ARGB(alpha, 60, 220, 130); break;
	case Theme::Warning: c.bg = CG_ARGB(alpha, 38, 28, 12); c.panel = CG_ARGB(alpha, 54, 40, 18); c.text = CG_RGB(255, 255, 255); c.sub = CG_RGB(240, 225, 190); c.accent = CG_RGB(255, 190, 60); c.border = CG_ARGB(alpha, 255, 190, 60); break;
	case Theme::Error:   c.bg = CG_ARGB(alpha, 38, 16, 22); c.panel = CG_ARGB(alpha, 55, 24, 32); c.text = CG_RGB(255, 255, 255); c.sub = CG_RGB(240, 205, 215); c.accent = CG_RGB(255, 82, 105); c.border = CG_ARGB(alpha, 255, 82, 105); break;
	case Theme::Purple:  c.bg = CG_ARGB(alpha, 28, 20, 42); c.panel = CG_ARGB(alpha, 38, 28, 60); c.text = CG_RGB(255, 255, 255); c.sub = CG_RGB(225, 215, 245); c.accent = CG_RGB(165, 105, 255); c.border = CG_ARGB(alpha, 165, 105, 255); break;
	default:             c.bg = CG_ARGB(alpha, 20, 20, 24); c.panel = CG_ARGB(alpha, 28, 28, 34); c.text = CG_RGB(255, 255, 255); c.sub = CG_RGB(205, 205, 212); c.accent = CG_RGB(130, 130, 150); c.border = CG_ARGB(alpha, 66, 66, 76); break;
	}
	return c;
}

[[nodiscard]] inline std::wstring toWide(const std::string& s) {
	if (s.empty()) return L"";
	if (s.size() > (size_t)INT_MAX / 2) return L"";
	int len = (int)s.size();
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), len, nullptr, 0);
	if (n <= 0) {
		n = MultiByteToWideChar(CP_ACP, 0, s.data(), len, nullptr, 0);
		if (n <= 0) {
			std::wstring fallback;
			fallback.reserve(len);
			for (unsigned char ch : s) fallback.push_back(ch < 0x80 ? (wchar_t)ch : L'?');
			return fallback;
		}
		std::vector<wchar_t> buf(n);
		MultiByteToWideChar(CP_ACP, 0, s.data(), len, buf.data(), n);
		return { buf.begin(), buf.begin() + n };
	}
	std::vector<wchar_t> buf(n);
	MultiByteToWideChar(CP_UTF8, 0, s.data(), len, buf.data(), n);
	return { buf.begin(), buf.begin() + n };
}

[[nodiscard]] inline std::string toUtf8(const wchar_t* ws) {
	if (!ws || !*ws) return {};
	int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
	if (n <= 1) return {};
	std::string out(n - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, ws, -1, out.data(), n, nullptr, nullptr);
	return out;
}
[[nodiscard]] inline std::string toUtf8(const std::wstring& ws) { return toUtf8(ws.c_str()); }

[[nodiscard]] inline std::wstring fontNameW() {
	const auto& f = ctx().style.guiFontName;
	return toWide(f.empty() ? "Microsoft YaHei UI" : f);
}

inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float easeOutCubic(float t) { t = std::clamp(t, 0.0f, 1.0f); float u = 1.0f - t; return 1.0f - u * u * u; }
inline float easeInOutCubic(float t) { t = std::clamp(t, 0.0f, 1.0f); if (t < 0.5f) return 4.0f * t * t * t; float u = 2.0f * t - 2.0f; return 0.5f + 0.5f * (u * u * u + 1.0f); }
inline unsigned char lerpU8(unsigned char a, unsigned char b, float t) { int v = (int)(a + ((b - a) * t) + 0.5f); return (unsigned char)std::clamp(v, 0, 255); }

[[nodiscard]] inline unsigned int rgbaScaleAlpha(unsigned int rgba, float alphaScale) {
	unsigned char a = (unsigned char)(rgba >> 24), r = (unsigned char)(rgba >> 16), g = (unsigned char)(rgba >> 8), b = (unsigned char)rgba;
	int na = (int)((float)a * alphaScale + 0.5f);
	return ((unsigned int)std::clamp(na, 0, 255) << 24) | (r << 16) | (g << 8) | b;
}
[[nodiscard]] inline ThemeColor themeScaleAlpha(ThemeColor c, float s) {
	c.bg = rgbaScaleAlpha(c.bg, s); c.panel = rgbaScaleAlpha(c.panel, s); c.border = rgbaScaleAlpha(c.border, s);
	return c;
}
[[nodiscard]] inline unsigned int rgbaLerp(unsigned int a, unsigned int b, float t) {
	unsigned char aa = (unsigned char)(a >> 24), ar = (unsigned char)(a >> 16), ag = (unsigned char)(a >> 8), ab = (unsigned char)a;
	unsigned char ba = (unsigned char)(b >> 24), br = (unsigned char)(b >> 16), bg = (unsigned char)(b >> 8), bb = (unsigned char)b;
	return ((unsigned int)lerpU8(aa, ba, t) << 24) | ((unsigned int)lerpU8(ar, br, t) << 16) | ((unsigned int)lerpU8(ag, bg, t) << 8) | lerpU8(ab, bb, t);
}

// 单调高精度时钟（毫秒），替代 QueryPerformanceCounter/GetTickCount
[[nodiscard]] inline unsigned long long nowMs() {
	static const auto s_t0 = std::chrono::steady_clock::now();
	return (unsigned long long)std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - s_t0).count();
}

[[nodiscard]] inline bool inClip(int x, int y) { return ctx().clipRect.contains(x, y); }

[[nodiscard]] inline std::string iconChar(Icon ic) {
	switch (ic) { case Icon::Info: return "i"; case Icon::Success: return "+"; case Icon::Warning: return "!"; case Icon::Error: return "x"; case Icon::Bell: return "*"; case Icon::Star: return "*"; default: return ""; }
}

} // namespace util
} // namespace detail
} // namespace cgui

#endif // CCLICKGUI_UTIL_H
