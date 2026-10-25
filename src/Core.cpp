// CClickGUI 核心：Registry / Feature 生命周期 / GUI 显隐控制 / 样式访问。

#include "Internal.h"
#include "Util.h"

namespace cgui {

std::string searchText;

Registry& Registry::instance() {
	static Registry r;
	return r;
}

Feature* GetFeature(const char* name) {
	if (!name) return nullptr;
	return Registry::instance().find(name);
}

Setting* GetSetting(const char* featName, const char* setName) {
	Feature* f = GetFeature(featName);
	return f ? f->findSetting(setName ? setName : "") : nullptr;
}

bool GetSettingBool(const char* featName, const char* setName, bool defValue) {
	Setting* s = GetSetting(featName, setName);
	return s ? s->asBool() : defValue;
}

int GetSettingInt(const char* featName, const char* setName, int defValue) {
	Setting* s = GetSetting(featName, setName);
	return s ? s->asInt() : defValue;
}

std::string GetSettingString(const char* featName, const char* setName, const std::string& defValue) {
	Setting* s = GetSetting(featName, setName);
	return s ? s->asString() : defValue;
}

void Feature::setEnabled(bool state) {
	if (enabled.load() == state) return;
	enabled.store(state);
	if (state) {
		disableCalled.store(false);
		startThread();
		if (onEnable) onEnable(*this);
	} else {
		stopThread();
		bool expected = false;
		if (disableCalled.compare_exchange_strong(expected, true) && onDisable) onDisable(*this);
		disableCalled.store(false);
	}
}

namespace detail {
	bool& initialized() { static bool init = false; return init; }
	GuiContext& ctx() { static GuiContext g; return g; }
}

void RegisterCoreFeatures() {
	static bool coreRegistered = false;
	if (!coreRegistered) {
		Feature& clickgui = GetGuiFeature();
		Registry::instance().add(&clickgui);
		coreRegistered = true;
		RefreshHotkeyForFeature(&clickgui);
	}
}

Feature& GetGuiFeature() {
	static Feature f("ClickGUI", "GUI", "半透明功能面板，默认 Alt+C 开关");
	static bool inited = false;
	if (!inited) {
		f.visibleInClickGui = true;
		f.onEnable = [](Feature&) { detail::ctx().guiVisible = true; };
		f.onDisable = [](Feature&) { detail::ctx().guiVisible = false; detail::exitSearchMode(); };
		if (!f.findSetting("HotkeyVK")) {
			f.addSetting(Setting::Int("HotkeyVK", "虚拟键码", 'C', 0, 255, 1));
			f.addSetting(Setting::Bool("HotkeyAlt", "需要Alt", true));
			f.addSetting(Setting::Bool("HotkeyCtrl", "需要Ctrl", false));
			f.addSetting(Setting::Bool("HotkeyShift", "需要Shift", false));
			f.addSetting(Setting::String("ShortcutText", "显示文本", "Alt+C"));
		}
		inited = true;
	}
	return f;
}

void ToggleGui() { RegisterCoreFeatures(); GetGuiFeature().toggle(); }
void ShowGui(bool s) { RegisterCoreFeatures(); GetGuiFeature().setEnabled(s); }
bool IsGuiVisible() { return detail::ctx().guiVisible; }

void ApplyStyle(const UIStyle& style) { auto& c = detail::ctx(); std::lock_guard<std::recursive_mutex> lock(c.mtx); c.style = style; c.style.custom = true; }
UIStyle GetStyle() { return detail::ctx().style; }
NotifyStyle GetNotifyStyle() { return detail::ctx().notifyStyle; }
void SetNotifyStyle(const NotifyStyle& ns) { detail::ctx().notifyStyle = ns; }

void ShutdownAllFeatures() {
	static std::atomic<bool> s_done{ false };
	if (s_done.exchange(true)) return;
	for (auto* f : Registry::instance().allFeatures())
		if (f && f->enabled.load()) f->setEnabled(false);
	s_done.store(false);
}

} // namespace cgui
