// CClickGUI 后处理特效管线（CPU Shader）：
//   • 自定义 shader：AddPostShader / RemovePostShader / SetPostShaderEnabled
//   • 内置预设：发光/扫描线/噪点/色差/暗角/波浪/像素化（注册为「特效」类别 Feature）
// 每帧在 GUI 渲染完成后、Alpha 预乘之前对帧缓冲执行（直通 Alpha）。

#include "Internal.h"
#include "Util.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>

namespace cgui {

// ---------------------------------------------------------------------------
// shader 注册表（保持注册顺序执行）
// ---------------------------------------------------------------------------
namespace {

struct PostShaderEntry {
	std::string name;
	PostShader fn;
	bool enabled = true;
};

std::recursive_mutex s_shaderMtx;
std::vector<PostShaderEntry> s_shaders;
float s_shaderClock = 0.0f;   // 累计时间（秒）

[[nodiscard]] PostShaderEntry* findEntry(const std::string& name) {
	for (auto& e : s_shaders) if (e.name == name) return &e;
	return nullptr;
}

} // anonymous namespace

void AddPostShader(const std::string& name, PostShader shader, bool enabled) {
	std::lock_guard<std::recursive_mutex> lock(s_shaderMtx);
	if (auto* e = findEntry(name)) { e->fn = std::move(shader); e->enabled = enabled; return; }
	s_shaders.push_back({ name, std::move(shader), enabled });
}

void RemovePostShader(const std::string& name) {
	std::lock_guard<std::recursive_mutex> lock(s_shaderMtx);
	for (auto it = s_shaders.begin(); it != s_shaders.end(); ++it)
		if (it->name == name) { s_shaders.erase(it); return; }
}

void SetPostShaderEnabled(const std::string& name, bool enabled) {
	std::lock_guard<std::recursive_mutex> lock(s_shaderMtx);
	if (auto* e = findEntry(name)) e->enabled = enabled;
}

bool HasPostShader(const std::string& name) {
	std::lock_guard<std::recursive_mutex> lock(s_shaderMtx);
	return findEntry(name) != nullptr;
}

std::vector<std::string> GetPostShaderNames() {
	std::lock_guard<std::recursive_mutex> lock(s_shaderMtx);
	std::vector<std::string> out;
	out.reserve(s_shaders.size());
	for (auto& e : s_shaders) out.push_back(e.name);
	return out;
}

namespace detail {

void RunPostShaders(GuiContext& c, float dtSec) {
	if (c.fb.empty() || c.w <= 0 || c.h <= 0) return;
	c_rect reg = c.contentRect;
	if (reg.width() <= 0 || reg.height() <= 0) return;
	reg.l = std::max(0, reg.l); reg.t = std::max(0, reg.t);
	reg.r = std::min(c.w - 1, reg.r); reg.b = std::min(c.h - 1, reg.b);
	if (reg.l > reg.r || reg.t > reg.b) return;

	if (dtSec <= 0.0f) dtSec = 0.016f;
	if (dtSec > 0.25f) dtSec = 0.25f;
	s_shaderClock += dtSec;

	std::lock_guard<std::recursive_mutex> lock(s_shaderMtx);
	for (auto& e : s_shaders) {
		if (!e.enabled || !e.fn) continue;
		try { e.fn(c.fb.data(), c.w, c.h, reg, s_shaderClock, dtSec); } catch (...) {}
	}
}

} // namespace detail

// ===========================================================================
// 内置预设特效（均注册为「特效」类别的 Feature，shader 读取其开关与参数）
// ===========================================================================
namespace effects {

namespace {

[[nodiscard]] Feature* effectFeature(const char* name) { return GetFeature(name); }
[[nodiscard]] float effectFloat(Feature* f, const char* setting, float def) {
	if (!f) return def;
	Setting* s = f->findSetting(setting);
	return s ? s->asFloat() : def;
}
[[nodiscard]] int effectInt(Feature* f, const char* setting, int def) {
	if (!f) return def;
	Setting* s = f->findSetting(setting);
	return s ? s->asInt() : def;
}

[[nodiscard]] inline unsigned int pixHash(unsigned int x, unsigned int y, unsigned int f) {
	unsigned int h = x * 374761393u + y * 668265263u + f * 974634749u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}

// ---- 发光（Bloom）：亮部提取 → 半分辨率模糊 → 叠加回帧 ----
void bloomShader(unsigned int* px, int w, int /*h*/, const c_rect& reg, float, float) {
	Feature* f = effectFeature("发光");
	if (!f || !f->enabled.load()) return;
	float strength = effectFloat(f, "强度", 0.5f);
	if (strength <= 0.001f) return;

	int rw = reg.width(), rh = reg.height();
	int hw = rw / 2, hh = rh / 2;
	if (hw < 2 || hh < 2) return;

	// 半分辨率亮部提取
	static std::vector<unsigned int> half; half.assign((size_t)hw * hh, 0);
	for (int y = 0; y < hh; ++y) {
		for (int x = 0; x < hw; ++x) {
			unsigned int p0 = px[(size_t)(reg.t + y * 2) * w + (reg.l + x * 2)];
			unsigned int p1 = px[(size_t)(reg.t + y * 2) * w + (reg.l + x * 2 + 1 < w ? reg.l + x * 2 + 1 : w - 1)];
			unsigned int p2 = px[(size_t)(reg.t + y * 2 + 1 < reg.b ? reg.t + y * 2 + 1 : reg.b) * w + (reg.l + x * 2)];
			unsigned int lum0 = std::max(CG_R(p0), std::max(CG_G(p0), CG_B(p0)));
			unsigned int lum = (lum0 + std::max(CG_R(p1), std::max(CG_G(p1), CG_B(p1)))
				+ std::max(CG_R(p2), std::max(CG_G(p2), CG_B(p2)))) / 3;
			if (lum <= 130) { half[(size_t)y * hw + x] = 0; continue; }
			float k = (float)(lum - 130) / 125.0f;
			if (k > 1.0f) k = 1.0f;
			unsigned int r = (CG_R(p0) + CG_R(p1) + CG_R(p2)) / 3;
			unsigned int g = (CG_G(p0) + CG_G(p1) + CG_G(p2)) / 3;
			unsigned int b = (CG_B(p0) + CG_B(p1) + CG_B(p2)) / 3;
			half[(size_t)y * hw + x] = CG_ARGB(255, (unsigned int)(r * k), (unsigned int)(g * k), (unsigned int)(b * k));
		}
	}

	// 可分离盒滤波 ×2
	static std::vector<unsigned int> tmp;
	auto blurPass = [&](int r) {
		tmp.assign(half.size(), 0);
		for (int y = 0; y < hh; ++y)
			for (int x = 0; x < hw; ++x) {
				int sr = 0, sg = 0, sb = 0, n = 0;
				for (int k = -r; k <= r; ++k) {
					int xx = x + k; if (xx < 0 || xx >= hw) continue;
					unsigned int p = half[(size_t)y * hw + xx];
					sr += CG_R(p); sg += CG_G(p); sb += CG_B(p); ++n;
				}
				tmp[(size_t)y * hw + x] = CG_ARGB(255, sr / n, sg / n, sb / n);
			}
		for (int y = 0; y < hh; ++y)
			for (int x = 0; x < hw; ++x) {
				int sr = 0, sg = 0, sb = 0, n = 0;
				for (int k = -r; k <= r; ++k) {
					int yy = y + k; if (yy < 0 || yy >= hh) continue;
					unsigned int p = tmp[(size_t)yy * hw + x];
					sr += CG_R(p); sg += CG_G(p); sb += CG_B(p); ++n;
				}
				half[(size_t)y * hw + x] = CG_ARGB(255, sr / n, sg / n, sb / n);
			}
	};
	blurPass(2);
	blurPass(2);

	// 叠加回帧（加色 + 提升透明度形成外溢光晕）
	for (int y = reg.t; y <= reg.b; ++y) {
		for (int x = reg.l; x <= reg.r; ++x) {
			size_t idx = (size_t)y * w + x;
			unsigned int dst = px[idx];
			// 奇数尺寸区域时下采样索引会越界，钳制到半分辨率缓冲内
			int sy = std::min((y - reg.t) / 2, hh - 1);
			int sx = std::min((x - reg.l) / 2, hw - 1);
			unsigned int gl = half[(size_t)sy * hw + sx];
			unsigned int gr = CG_R(gl), gg = CG_G(gl), gb = CG_B(gl);
			if (gr == 0 && gg == 0 && gb == 0) continue;
			unsigned int nr = std::min(255u, CG_R(dst) + (unsigned int)(gr * strength));
			unsigned int ng = std::min(255u, CG_G(dst) + (unsigned int)(gg * strength));
			unsigned int nb = std::min(255u, CG_B(dst) + (unsigned int)(gb * strength));
			unsigned int lum = std::max(gr, std::max(gg, gb));
			unsigned int na = std::max(CG_A(dst), std::min(255u, (unsigned int)(lum * strength * 1.6f)));
			px[idx] = CG_ARGB(na, nr, ng, nb);
		}
	}
}

// ---- 扫描线 ----
void scanlineShader(unsigned int* px, int w, int /*h*/, const c_rect& reg, float, float) {
	Feature* f = effectFeature("扫描线");
	if (!f || !f->enabled.load()) return;
	float strength = effectFloat(f, "强度", 0.35f);
	if (strength <= 0.001f) return;
	float k = 1.0f - 0.22f * strength;
	for (int y = reg.t; y <= reg.b; ++y) {
		if (y % 3 != 0) continue;
		unsigned int* row = px + (size_t)y * w;
		for (int x = reg.l; x <= reg.r; ++x) {
			unsigned int p = row[x];
			if (CG_A(p) == 0) continue;
			row[x] = CG_ARGB(CG_A(p), (unsigned int)(CG_R(p) * k), (unsigned int)(CG_G(p) * k), (unsigned int)(CG_B(p) * k));
		}
	}
}

// ---- 噪点（动态胶片颗粒）----
void grainShader(unsigned int* px, int w, int /*h*/, const c_rect& reg, float time, float) {
	Feature* f = effectFeature("噪点");
	if (!f || !f->enabled.load()) return;
	float strength = effectFloat(f, "强度", 0.25f);
	if (strength <= 0.001f) return;
	unsigned int frame = (unsigned int)(time * 60.0f);
	int amount = (int)(34.0f * strength);
	for (int y = reg.t; y <= reg.b; ++y) {
		for (int x = reg.l; x <= reg.r; ++x) {
			size_t idx = (size_t)y * w + x;
			unsigned int p = px[idx];
			if (CG_A(p) == 0) continue;
			int n = (int)(pixHash((unsigned int)x, (unsigned int)y, frame) & 0xFF) - 128;
			int d = (int)(n * amount / 128);
			int r = std::clamp((int)CG_R(p) + d, 0, 255);
			int g = std::clamp((int)CG_G(p) + d, 0, 255);
			int b = std::clamp((int)CG_B(p) + d, 0, 255);
			px[idx] = CG_ARGB(CG_A(p), r, g, b);
		}
	}
}

// ---- 色差（径向 RGB 通道分离）----
void chromaticShader(unsigned int* px, int w, int /*h*/, const c_rect& reg, float, float) {
	Feature* f = effectFeature("色差");
	if (!f || !f->enabled.load()) return;
	float strength = effectFloat(f, "强度", 1.2f);
	if (strength <= 0.01f) return;
	int rw = reg.width(), rh = reg.height();
	float ccx = reg.l + rw * 0.5f, ccy = reg.t + rh * 0.5f;
	float maxd = std::sqrt((float)(rw * rw + rh * rh)) * 0.5f;
	if (maxd < 1.0f) return;

	static std::vector<unsigned int> copy;
	copy.assign((size_t)rw * rh, 0);
	for (int y = 0; y < rh; ++y)
		memcpy(&copy[(size_t)y * rw], px + (size_t)(reg.t + y) * w + reg.l, (size_t)rw * sizeof(unsigned int));

	for (int y = reg.t; y <= reg.b; ++y) {
		for (int x = reg.l; x <= reg.r; ++x) {
			size_t idx = (size_t)y * w + x;
			unsigned int p = px[idx];
			if (CG_A(p) == 0) continue;
			float dx = x - ccx, dy = y - ccy;
			float d = std::sqrt(dx * dx + dy * dy) / maxd;
			int off = (int)(strength * (0.4f + 1.6f * d));
			if (off < 1) off = 1;
			int sxr = x + off, sxb = x - off;
			unsigned int pr = (sxr >= reg.l && sxr <= reg.r) ? copy[(size_t)(y - reg.t) * rw + (sxr - reg.l)] : p;
			unsigned int pb = (sxb >= reg.l && sxb <= reg.r) ? copy[(size_t)(y - reg.t) * rw + (sxb - reg.l)] : p;
			px[idx] = CG_ARGB(CG_A(p), CG_R(pr), CG_G(p), CG_B(pb));
		}
	}
}

// ---- 暗角（Vignette）----
void vignetteShader(unsigned int* px, int w, int /*h*/, const c_rect& reg, float, float) {
	Feature* f = effectFeature("暗角");
	if (!f || !f->enabled.load()) return;
	float strength = effectFloat(f, "强度", 0.35f);
	if (strength <= 0.001f) return;
	int rw = reg.width(), rh = reg.height();
	float ccx = reg.l + rw * 0.5f, ccy = reg.t + rh * 0.5f;
	float maxd = std::sqrt((float)(rw * rw + rh * rh)) * 0.5f;
	if (maxd < 1.0f) return;
	for (int y = reg.t; y <= reg.b; ++y) {
		for (int x = reg.l; x <= reg.r; ++x) {
			size_t idx = (size_t)y * w + x;
			unsigned int p = px[idx];
			if (CG_A(p) == 0) continue;
			float dx = x - ccx, dy = y - ccy;
			float d = std::sqrt(dx * dx + dy * dy) / maxd;
			float k = 1.0f - strength * d * d;
			if (k < 0.0f) k = 0.0f;
			px[idx] = CG_ARGB(CG_A(p), (unsigned int)(CG_R(p) * k), (unsigned int)(CG_G(p) * k), (unsigned int)(CG_B(p) * k));
		}
	}
}

// ---- 波浪（行正弦偏移）----
void waveShader(unsigned int* px, int w, int /*h*/, const c_rect& reg, float time, float) {
	Feature* f = effectFeature("波浪");
	if (!f || !f->enabled.load()) return;
	float amp = effectFloat(f, "幅度", 4.0f);
	float freq = effectFloat(f, "频率", 0.06f);
	float speed = effectFloat(f, "速度", 2.0f);
	if (amp < 0.1f) return;
	int rw = reg.width();

	static std::vector<unsigned int> copy;
	copy.assign((size_t)rw, 0);
	for (int y = reg.t; y <= reg.b; ++y) {
		unsigned int* row = px + (size_t)y * w;
		memcpy(copy.data(), row + reg.l, (size_t)rw * sizeof(unsigned int));
		int xo = (int)(std::sin((float)y * freq + time * speed) * amp);
		for (int x = 0; x < rw; ++x) {
			int sx = x - xo;
			row[reg.l + x] = (sx >= 0 && sx < rw) ? copy[sx] : 0;
		}
	}
}

// ---- 像素化 ----
void pixelateShader(unsigned int* px, int w, int /*h*/, const c_rect& reg, float, float) {
	Feature* f = effectFeature("像素化");
	if (!f || !f->enabled.load()) return;
	int block = effectInt(f, "块大小", 4);
	if (block < 2) return;
	for (int by = reg.t; by <= reg.b; by += block) {
		for (int bx = reg.l; bx <= reg.r; bx += block) {
			unsigned int sr = 0, sg = 0, sb = 0, sa = 0, n = 0;
			int beY = std::min(by + block - 1, reg.b), beX = std::min(bx + block - 1, reg.r);
			for (int y = by; y <= beY; ++y)
				for (int x = bx; x <= beX; ++x) {
					unsigned int p = px[(size_t)y * w + x];
					if (CG_A(p) == 0) continue;
					sr += CG_R(p); sg += CG_G(p); sb += CG_B(p); sa += CG_A(p); ++n;
				}
			if (n == 0) continue;
			unsigned int avg = CG_ARGB(sa / n, sr / n, sg / n, sb / n);
			for (int y = by; y <= beY; ++y)
				for (int x = bx; x <= beX; ++x)
					px[(size_t)y * w + x] = avg;
		}
	}
}

struct BuiltinDef {
	const char* name;
	const char* desc;
	PostShader shader;
	void (*addSettings)(Feature&);
};

void makeBuiltin(const BuiltinDef& def) {
	static std::vector<std::unique_ptr<Feature>> s_storage;
	auto f = std::make_unique<Feature>(def.name, "特效", def.desc);
	f->useWorkerThread = false;   // 纯视觉特效：由 shader 管线按 enabled 状态采样
	def.addSettings(*f);
	AddPostShader(def.name, def.shader, true);
	s_storage.push_back(std::move(f));
	Registry::instance().add(s_storage.back().get());
}

} // anonymous namespace

void RegisterBuiltinEffects() {
	static bool s_registered = false;
	if (s_registered) return;
	s_registered = true;

	const BuiltinDef defs[] = {
		{ "发光", "亮部发光（Bloom）", bloomShader,
		  [](Feature& f) { f.addSetting(Setting::Float("强度", "0 ~ 1", 0.5f, 0.0f, 1.0f, 0.05f)); } },
		{ "扫描线", "CRT 扫描线", scanlineShader,
		  [](Feature& f) { f.addSetting(Setting::Float("强度", "0 ~ 1", 0.35f, 0.0f, 1.0f, 0.05f)); } },
		{ "噪点", "动态胶片颗粒", grainShader,
		  [](Feature& f) { f.addSetting(Setting::Float("强度", "0 ~ 1", 0.25f, 0.0f, 1.0f, 0.05f)); } },
		{ "色差", "径向 RGB 通道分离", chromaticShader,
		  [](Feature& f) { f.addSetting(Setting::Float("强度", "偏移像素", 1.2f, 0.0f, 4.0f, 0.2f)); } },
		{ "暗角", "边缘变暗（Vignette）", vignetteShader,
		  [](Feature& f) { f.addSetting(Setting::Float("强度", "0 ~ 1", 0.35f, 0.0f, 1.0f, 0.05f)); } },
		{ "波浪", "行正弦波偏移", waveShader,
		  [](Feature& f) {
			  f.addSetting(Setting::Float("幅度", "像素", 4.0f, 0.0f, 12.0f, 0.5f));
			  f.addSetting(Setting::Float("频率", "波长倒数", 0.06f, 0.01f, 0.2f, 0.01f));
			  f.addSetting(Setting::Float("速度", "相位速度", 2.0f, 0.0f, 8.0f, 0.1f));
		  } },
		{ "像素化", "块状马赛克", pixelateShader,
		  [](Feature& f) { f.addSetting(Setting::Int("块大小", "像素", 4, 2, 16, 1)); } },
	};
	for (const auto& d : defs) makeBuiltin(d);
}

} // namespace effects
} // namespace cgui
