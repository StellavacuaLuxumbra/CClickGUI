// CClickGUI 配置持久化：INI 风格文本（名称.enabled / 名称.setting.设置名=值），
// 支持启用自动保存（任何设置变更后写盘）。

#include "Internal.h"
#include "Util.h"

#include <cctype>
#include <cstdlib>

namespace cgui {

static std::string g_autoSavePath;
static bool g_autoSaveEnabled = false;

static void saveFeatureRecursive(FILE* fp, Feature* f) {
	if (!fp || !f) return;
	fprintf(fp, "%s.enabled=%s\n", f->name.c_str(), f->enabled ? "true" : "false");
	for (auto& s : f->settings) fprintf(fp, "%s.setting.%s=%s\n", f->name.c_str(), s.name.c_str(), s.asString().c_str());
	for (auto* child : f->children) saveFeatureRecursive(fp, child);
}

static bool SaveConfigToFileImpl(const char* path) {
	// 宽字符路径打开：修复中文文件名/中文目录经 ANSI fopen 失败的问题
	FILE* fp = _wfopen(detail::util::toWide(path ? path : "cclickgui.cfg").c_str(), L"w");
	if (!fp) return false;
	for (auto* f : Registry::instance().allFeatures()) saveFeatureRecursive(fp, f);
	fclose(fp); return true;
}

namespace detail {
void autoSaveIfEnabled() {
	if (!g_autoSaveEnabled || g_autoSavePath.empty()) return;
	SaveConfigToFileImpl(g_autoSavePath.c_str());
}
} // namespace detail

void EnableAutoSave(const char* path) {
	g_autoSavePath = path ? path : "";
	g_autoSaveEnabled = !g_autoSavePath.empty();
}

void DisableAutoSave() {
	g_autoSaveEnabled = false;
	g_autoSavePath.clear();
}

bool SaveConfig(const char* path) { return SaveConfigToFileImpl(path ? path : "cclickgui.cfg"); }

bool LoadConfig(const char* path) {
	FILE* fp = _wfopen(detail::util::toWide(path ? path : "cclickgui.cfg").c_str(), L"r");
	if (!fp) return false;
	char line[512];
	while (fgets(line, sizeof(line), fp)) {
		std::string s = line; while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
		if (s.empty() || s[0] == '#') continue;
		size_t eq = s.find('='); if (eq == std::string::npos) continue;
		std::string key = s.substr(0, eq), val = s.substr(eq + 1);
		if (key.size() > 8 && key.compare(key.size() - 8, 8, ".enabled") == 0) {
			std::string name = key.substr(0, key.size() - 8);
			Feature* f = Registry::instance().find(name);
			if (f) f->setEnabled(val == "true" || val == "开启");
		} else {
			size_t dot = key.find(".setting.");
			if (dot == std::string::npos) continue;
			std::string fname = key.substr(0, dot), sname = key.substr(dot + 9);
			Feature* f = Registry::instance().find(fname);
			if (f) {
				Setting* st = f->findSetting(sname);
				if (st) {
					if (st->type == SettingType::Bool) st->setBool(val == "true" || val == "开启");
					else if (st->type == SettingType::Int) st->setInt(std::atoi(val.c_str()));
					else if (st->type == SettingType::Float) st->setFloat((float)std::atof(val.c_str()));
					else st->setString(val);
				}
			}
		}
	}
	fclose(fp);
	RefreshAllHotkeys();
	return true;
}

std::vector<std::string> GetConfigList() {
	std::vector<std::string> files;
	WIN32_FIND_DATAA fd; HANDLE hFind = FindFirstFileA("*.cfg", &fd);
	if (hFind != INVALID_HANDLE_VALUE) {
		do { files.push_back(fd.cFileName); } while (FindNextFileA(hFind, &fd));
		FindClose(hFind);
	}
	std::sort(files.begin(), files.end());
	return files;
}

bool DeleteConfigFile(const char* name) { return DeleteFileW(detail::util::toWide(name).c_str()) != 0; }

} // namespace cgui
