#ifndef CCLICKGUI_H
#define CCLICKGUI_H

// ============================================================================
//  CClickGUI — 现代化 C++17 ClickGUI 框架（Win32）
//  提取自 LuxumbraCilent 3.0 的 ClickGUI 子系统并重构为独立框架。
//
//  架构：
//    • Win32 分层窗口（WS_EX_LAYERED，逐像素 Alpha）覆盖整个屏幕
//    • 纯软件光栅化渲染（ARGB 帧缓冲 + GDI 文字），无需任何图形库依赖
//    • SDF 抗锯齿图形 + 柔和投影，灰度抗锯齿文字
//    • 渲染时构建命中区域（Hit）列表，点击时逆序遍历命中测试
//    • 全套动画：面板开合缩放、分类切换指示条、行展开、开关滑块
//    • 后处理特效管线（CPU Shader）：自定义 + 内置预设（发光/扫描线/噪点/
//      色差/暗角/波浪/像素化）
//
//  快速开始：
//    cgui::InitGui();                    // 初始化（注册 ClickGUI 功能，Alt+C 呼出）
//    cgui::effects::RegisterBuiltinEffects();  // 可选：注册内置特效
//    // 注册你自己的功能（Feature）到 Registry::instance()
//    cgui::RefreshAllHotkeys();
//    while (running) { cgui::Tick(); Sleep(1); }
//    cgui::ShutdownGui();
// ============================================================================

#if defined(_WIN32) || defined(WIN32)
#  ifdef CCLICKGUI_BUILD_DLL
#    define CGUI_API __declspec(dllexport)
#  else
#    define CGUI_API
#  endif
#else
#  define CGUI_API
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace cgui {

// ============================================================================
// 颜色宏（ARGB）
// ============================================================================
#define CG_ARGB(a,r,g,b) ((((unsigned int)(a))<<24)|(((unsigned int)(r))<<16)|(((unsigned int)(g))<<8)|((unsigned int)(b)))
#define CG_RGB(r,g,b) CG_ARGB(255,r,g,b)
#define CG_A(c) (((unsigned int)(c)>>24)&0xFF)
#define CG_R(c) (((unsigned int)(c)>>16)&0xFF)
#define CG_G(c) (((unsigned int)(c)>>8)&0xFF)
#define CG_B(c) (((unsigned int)(c))&0xFF)

// 文字对齐标志
#define ALIGN_LEFT    0x01000000L
#define ALIGN_RIGHT   0x02000000L
#define ALIGN_HCENTER 0x00000000L
#define ALIGN_TOP     0x00100000L
#define ALIGN_BOTTOM  0x00200000L
#define ALIGN_VCENTER 0x00000000L

// ============================================================================
// 基础整数矩形
// ============================================================================
class c_rect {
public:
	int l = 0, t = 0, r = 0, b = 0;
	c_rect() = default;
	c_rect(int x, int y, int w, int h) { set(x, y, w, h); }
	void set(int x, int y, int w, int h) {
		l = x; t = y; r = x + w - 1; b = y + h - 1;
		if (r < l) r = l;
		if (b < t) b = t;
	}
	[[nodiscard]] int width() const { return r - l + 1; }
	[[nodiscard]] int height() const { return b - t + 1; }
	[[nodiscard]] bool contains(int x, int y) const { return x >= l && x <= r && y >= t && y <= b; }
};

enum class Theme { Dark, Blue, Success, Warning, Error, Purple };
enum class Icon { None, Info, Success, Warning, Error, Bell, Star };
enum class SettingType { Bool, Int, Float, String, Select };

// ============================================================================
// 设置值（可变类型，基于 std::variant）
// ============================================================================
struct CGUI_API SettingValue {
	enum Kind : std::size_t { KBool, KInt, KFloat, KString };
	std::variant<bool, int, float, std::string> v;

	SettingValue() : v(false) {}
	SettingValue(bool x) : v(x) {}
	SettingValue(int x) : v(x) {}
	SettingValue(float x) : v(x) {}
	SettingValue(const std::string& x) : v(x) {}
	SettingValue(const char* x) : v(std::string(x ? x : "")) {}
	SettingValue& operator=(bool x) { v = x; return *this; }
	SettingValue& operator=(int x) { v = x; return *this; }
	SettingValue& operator=(float x) { v = x; return *this; }
	SettingValue& operator=(const std::string& x) { v = x; return *this; }
	SettingValue& operator=(const char* x) { v = std::string(x ? x : ""); return *this; }

	[[nodiscard]] Kind kind() const { return static_cast<Kind>(v.index()); }
	[[nodiscard]] bool isBool() const { return std::holds_alternative<bool>(v); }
	[[nodiscard]] bool isInt() const { return std::holds_alternative<int>(v); }
	[[nodiscard]] bool isFloat() const { return std::holds_alternative<float>(v); }
	[[nodiscard]] bool isString() const { return std::holds_alternative<std::string>(v); }

	[[nodiscard]] bool asBool() const {
		if (const auto* p = std::get_if<bool>(&v)) return *p;
		if (const auto* p = std::get_if<int>(&v)) return *p != 0;
		if (const auto* p = std::get_if<float>(&v)) return *p != 0.0f;
		const auto& s = std::get<std::string>(v);
		return s == "true" || s == "开启";
	}
	[[nodiscard]] int asInt() const {
		if (const auto* p = std::get_if<bool>(&v)) return *p ? 1 : 0;
		if (const auto* p = std::get_if<int>(&v)) return *p;
		if (const auto* p = std::get_if<float>(&v)) return static_cast<int>(*p);
		try { return std::stoi(std::get<std::string>(v)); } catch (...) { return 0; }
	}
	[[nodiscard]] float asFloat() const {
		if (const auto* p = std::get_if<bool>(&v)) return *p ? 1.0f : 0.0f;
		if (const auto* p = std::get_if<int>(&v)) return static_cast<float>(*p);
		if (const auto* p = std::get_if<float>(&v)) return *p;
		try { return std::stof(std::get<std::string>(v)); } catch (...) { return 0.0f; }
	}
	[[nodiscard]] std::string asString() const {
		char buf[32];
		if (const auto* p = std::get_if<std::string>(&v)) return *p;
		if (const auto* p = std::get_if<bool>(&v)) return *p ? "开启" : "关闭";
		if (const auto* p = std::get_if<int>(&v)) { snprintf(buf, 32, "%d", *p); return buf; }
		snprintf(buf, 32, "%.2f", std::get<float>(v));
		return buf;
	}
};

// ============================================================================
// 设置项（Bool / Int / Float / String / Select）
// ============================================================================
struct Setting {
	std::string name, desc;
	SettingType type = SettingType::Bool;
	SettingValue value;
	SettingValue defaultValue;
	float minVal = 0.0f, maxVal = 1.0f, step = 1.0f;
	std::string page;                      // 非空时只显示在指定分页
	std::vector<std::string> options;      // SettingType::Select 候选列表

	Setting() = default;
	static Setting Bool(const std::string& n, const std::string& d, bool v, const std::string& pg = "") {
		Setting s; s.name = n; s.desc = d; s.type = SettingType::Bool; s.value = v; s.defaultValue = v; s.page = pg; return s;
	}
	static Setting Int(const std::string& n, const std::string& d, int v, int minv, int maxv, int stepv = 1, const std::string& pg = "") {
		Setting s; s.name = n; s.desc = d; s.type = SettingType::Int; s.value = v; s.defaultValue = v;
		s.minVal = static_cast<float>(minv); s.maxVal = static_cast<float>(maxv); s.step = static_cast<float>(stepv); s.page = pg; return s;
	}
	static Setting Float(const std::string& n, const std::string& d, float v, float minv, float maxv, float stepv = 0.1f, const std::string& pg = "") {
		Setting s; s.name = n; s.desc = d; s.type = SettingType::Float; s.value = v; s.defaultValue = v;
		s.minVal = minv; s.maxVal = maxv; s.step = stepv; s.page = pg; return s;
	}
	static Setting String(const std::string& n, const std::string& d, const std::string& v, const std::string& pg = "") {
		Setting s; s.name = n; s.desc = d; s.type = SettingType::String; s.value = v; s.defaultValue = v; s.page = pg; return s;
	}
	static Setting Select(const std::string& n, const std::string& d, const std::string& v,
		std::initializer_list<std::string> opts, const std::string& pg = "") {
		Setting s; s.name = n; s.desc = d; s.type = SettingType::Select;
		s.value = v; s.defaultValue = v; s.page = pg;
		s.options.assign(opts.begin(), opts.end());
		bool ok = false;
		for (const auto& o : s.options) if (o == v) { ok = true; break; }
		if (!ok && !s.options.empty()) { s.value = s.options[0]; s.defaultValue = s.options[0]; }
		return s;
	}
	[[nodiscard]] int selectIndex() const {
		if (type != SettingType::Select) return -1;
		const std::string cur = value.asString();
		for (size_t i = 0; i < options.size(); ++i) if (options[i] == cur) return static_cast<int>(i);
		return -1;
	}
	void selectIndex(int idx) {
		if (type != SettingType::Select) return;
		if (idx < 0 || idx >= static_cast<int>(options.size())) return;
		value = options[idx];
	}
	void cycleSelect(int dir) {
		if (type != SettingType::Select || options.empty()) return;
		const int n = static_cast<int>(options.size());
		int cur = selectIndex();
		if (cur < 0) cur = 0;
		int nxt = (cur + (dir >= 0 ? 1 : -1) + n * 10) % n;
		value = options[nxt];
	}
	[[nodiscard]] std::string text() const {
		if (type == SettingType::Bool) return value.asBool() ? "开启" : "关闭";
		return value.asString();
	}
	[[nodiscard]] bool asBool() const { return value.asBool(); }
	[[nodiscard]] int asInt() const { return value.asInt(); }
	[[nodiscard]] float asFloat() const { return value.asFloat(); }
	[[nodiscard]] std::string asString() const { return value.asString(); }
	void setBool(bool v) { value = v; }
	void setInt(int v) { value = v; }
	void setFloat(float v) { value = v; }
	void setString(const std::string& v) { value = v; }
};

class Registry;

// ============================================================================
// 功能（模块）。ClickGUI 面板中的每一行即一个 Feature。
// ============================================================================
class Feature {
public:
	std::string name, category, desc;
	std::atomic<bool> enabled{ false };
	bool expanded = false;
	float expandAnim = 0.0f;    // 行展开动画 0→1
	float toggleAnim = 0.0f;    // 开关滑块动画 0→1
	bool visibleInClickGui = true;
	// ===== 线程策略标志 =====
	// true（默认）：setEnabled(true) 自动启动独立 worker 线程，每 16ms 调 onTick。
	// false：从不启动线程，依赖主循环 Tick() 在 UI 线程遍历调用 onTick
	//（适合需要创建 HWND / GDI 绘制 / 窗口消息的视觉类功能）。
	bool useWorkerThread = true;
	std::vector<Setting> settings;
	std::vector<Feature*> children;
	std::function<void(Feature&)> onEnable, onDisable, onTick;
	std::string currentPage;    // 分页设置：当前显示的分页名（空 = 自动选第一页）

	Feature(std::string n, std::string c, std::string d = "")
		: name(std::move(n)), category(std::move(c)), desc(std::move(d)) {}
	Feature(const Feature&) = delete;
	Feature& operator=(const Feature&) = delete;
	~Feature() { _silentCleanupOnDestroy(); }

	void addSetting(const Setting& s) { settings.push_back(s); }
	void addChild(Feature* c) { if (c) children.push_back(c); }
	[[nodiscard]] Setting* findSetting(const std::string& nm) {
		for (auto& s : settings) if (s.name == nm) return &s;
		return nullptr;
	}
	void resetSettings() {
		for (auto& s : settings) s.value = s.defaultValue;
		for (auto* child : children) if (child) child->resetSettings();
	}
	[[nodiscard]] std::vector<std::string> getPages() const {
		std::set<std::string> pset;
		for (const auto& s : settings) if (!s.page.empty()) pset.insert(s.page);
		return { pset.begin(), pset.end() };
	}

	void setEnabled(bool state);
	void toggle() { setEnabled(!enabled); }
	void restart() {
		bool wasEnabled = enabled.load();
		if (wasEnabled) {
			stopThread();
			bool expected = false;
			if (disableCalled.compare_exchange_strong(expected, true) && onDisable) onDisable(*this);
			disableCalled.store(false);
		}
		if (wasEnabled) {
			startThread();
			if (onEnable) onEnable(*this);
		}
	}
	void startThread() {
		if (!useWorkerThread) return;
		if (threadRunning) return;
		threadRunning = true;
		workerThread = std::thread([this]() {
			while (threadRunning && enabled) { if (onTick) onTick(*this); std::this_thread::sleep_for(std::chrono::milliseconds(16)); }
		});
	}
	void stopThread() {
		if (!threadRunning) return;
		threadRunning = false;
		if (workerThread.joinable()) workerThread.join();
	}
	[[nodiscard]] bool isThreadRunning() const { return threadRunning.load(); }

private:
	std::thread workerThread;
	std::atomic<bool> threadRunning{ false };
	std::atomic<bool> disableCalled{ false };
	void _silentCleanupOnDestroy() {
		bool expected = false;
		if (!disableCalled.compare_exchange_strong(expected, true)) return;
		stopThread();
		try { if (onDisable) onDisable(*this); } catch (...) {}
	}
};

// ============================================================================
// 功能注册表（单例）。add() 会自动为 Feature 附加快捷键设置项。
// ============================================================================
class CGUI_API Registry {
public:
	static Registry& instance();
	void add(Feature* f) {
		if (!f) return;
		if (!f->findSetting("HotkeyVK")) {
			f->addSetting(Setting::Int("HotkeyVK", "虚拟键码", 0, 0, 255, 1));
			f->addSetting(Setting::Bool("HotkeyAlt", "需要Alt", false));
			f->addSetting(Setting::Bool("HotkeyCtrl", "需要Ctrl", false));
			f->addSetting(Setting::Bool("HotkeyShift", "需要Shift", false));
			f->addSetting(Setting::String("ShortcutText", "显示文本", "无"));
		}
		features.push_back(f);
	}
	void remove(Feature* f) {
		for (auto it = features.begin(); it != features.end(); ++it) if (*it == f) { features.erase(it); return; }
	}
	[[nodiscard]] Feature* find(const std::string& name) {
		for (auto f : features) { if (f->name == name) return f; for (auto c : f->children) if (c->name == name) return c; }
		return nullptr;
	}
	[[nodiscard]] std::vector<std::string> categories() const {
		std::vector<std::string> out;
		for (auto f : features) {
			if (!f->visibleInClickGui) continue;
			if (std::find(out.begin(), out.end(), f->category) == out.end())
				out.push_back(f->category);
		}
		return out;
	}
	[[nodiscard]] const std::vector<Feature*>& allFeatures() const { return features; }
private:
	std::vector<Feature*> features;
};

// ============================================================================
// 样式
// ============================================================================
struct UIStyle {
	bool custom = false;
	unsigned int guiBg = CG_RGB(28, 28, 34), guiText = CG_RGB(255, 255, 255);
	unsigned int guiAccent = CG_RGB(130, 130, 150), guiBorder = CG_RGB(66, 66, 76);
	int guiBorderWidth = 2;
	unsigned char guiAlpha = 205;
	float guiScale = 1.0f;
	float scrollSpeed = 3.0f;
	int guiFontSize = 15;
	std::string guiFontName = "Microsoft YaHei UI";
	std::string title = "CClickGUI";
	int scrollBarWidth = 15;
};

struct NotifyStyle {
	unsigned int bg = CG_RGB(28, 28, 34), text = CG_RGB(255, 255, 255), accent = CG_RGB(130, 130, 150);
	unsigned int border = CG_RGB(66, 66, 76);
	int borderWidth = 1;
	unsigned char alpha = 224;
	float scale = 1.0f;
	int offsetX = 0, offsetY = 0;
	bool showIcon = true, showText = true;
	float enterSpeed = 0.020f, exitSpeed = 0.022f, contentSpeed = 0.025f, moveYSpeed = 0.040f;
};

// ============================================================================
// 后处理特效（CPU Shader）
//   每帧在 GUI 渲染完成之后、Alpha 预乘之前调用；
//   pixels 为整帧 ARGB（直通 Alpha）帧缓冲，region 为本帧有内容的包围盒。
//   注意：回调在 UI 线程持锁执行，应保持轻量。
// ============================================================================
using PostShader = std::function<void(unsigned int* pixels, int fbW, int fbH, const c_rect& region, float timeSec, float dtSec)>;

CGUI_API void AddPostShader(const std::string& name, PostShader shader, bool enabled = true);
CGUI_API void RemovePostShader(const std::string& name);
CGUI_API void SetPostShaderEnabled(const std::string& name, bool enabled);
CGUI_API [[nodiscard]] bool HasPostShader(const std::string& name);
CGUI_API [[nodiscard]] std::vector<std::string> GetPostShaderNames();

// 内置预设特效（发光/扫描线/噪点/色差/暗角/波浪/像素化），
// 注册为「特效」类别的 Feature，可在面板中开关与调参。
namespace effects {
CGUI_API void RegisterBuiltinEffects();
}

// ============================================================================
// 公开 API
// ============================================================================

// 生命周期
CGUI_API bool InitGui(int w = 0, int h = 0);   // 初始化（自动注册 ClickGUI 功能，默认 Alt+C 呼出）
CGUI_API void ShutdownGui();
CGUI_API void Tick();                          // 主循环驱动（内部限流 ~60fps），处理消息 / 渲染 / 视觉类 onTick

// GUI 显隐
CGUI_API void ToggleGui();
CGUI_API void ShowGui(bool show);
CGUI_API [[nodiscard]] bool IsGuiVisible();
CGUI_API [[nodiscard]] Feature& GetGuiFeature();  // ClickGUI 自身 Feature（可在其中改呼出热键）
CGUI_API void RegisterCoreFeatures();             // 注册内置功能（幂等）

// 通知（Toast，右上角滑入）
CGUI_API void Notify(const char* title, const char* msg, int ms = 2600, Theme theme = Theme::Dark, Icon icon = Icon::Info);
CGUI_API void NotifyKeyed(const char* key, const char* title, const char* msg, int ms = 2600, Theme theme = Theme::Dark, Icon icon = Icon::Info);

// 样式
CGUI_API void ApplyStyle(const UIStyle& style);
CGUI_API [[nodiscard]] UIStyle GetStyle();
CGUI_API [[nodiscard]] NotifyStyle GetNotifyStyle();
CGUI_API void SetNotifyStyle(const NotifyStyle& ns);

// 配置持久化（简单 INI 格式：名称.enabled / 名称.setting.设置名）
CGUI_API bool SaveConfig(const char* path);
CGUI_API bool LoadConfig(const char* path);
CGUI_API [[nodiscard]] std::vector<std::string> GetConfigList();   // 当前目录下所有 *.cfg
CGUI_API bool DeleteConfigFile(const char* name);
CGUI_API void EnableAutoSave(const char* path);      // 任何设置变更后自动保存
CGUI_API void DisableAutoSave();

// 全局热键（RegisterHotKey，需至少一个修饰键；无修饰键的热键不注册）
CGUI_API void RefreshHotkeyForFeature(Feature* f);
CGUI_API void RefreshAllHotkeys();

// 便捷查询
CGUI_API [[nodiscard]] Feature* GetFeature(const char* name);
CGUI_API [[nodiscard]] Setting* GetSetting(const char* featName, const char* setName);
CGUI_API [[nodiscard]] bool GetSettingBool(const char* featName, const char* setName, bool defValue = false);
CGUI_API [[nodiscard]] int GetSettingInt(const char* featName, const char* setName, int defValue = 0);
CGUI_API [[nodiscard]] std::string GetSettingString(const char* featName, const char* setName, const std::string& defValue = "");

// 停用并复位所有已启用的 Feature（进程退出前调用）
CGUI_API void ShutdownAllFeatures();

// 搜索文本（高级用法：外部直接读取/清空当前搜索词）
CGUI_API extern std::string searchText;

} // namespace cgui

#endif // CCLICKGUI_H
