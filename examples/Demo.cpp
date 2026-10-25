// ============================================================================
// CClickGUI Hello World（最简示例）
//   • Alt+C 呼出 / 关闭面板
//   • 左键「Hello World」行 → 弹出问候 Toast，内容可在设置里改
//   • 左键「退出程序」行 → 结束演示
// ============================================================================

#include <CClickGUI.h>
#include <cstdio>
#include <windows.h>

using namespace cgui;

static bool g_running = true;

static Feature s_hello("Hello World", "示例", "启用后弹出问候 Toast");
static Feature s_exit("退出程序", "示例", "结束演示");

int main() {
	SetConsoleOutputCP(CP_UTF8);
	printf("CClickGUI Hello World（Alt+C 呼出面板）\n");

	s_hello.addSetting(Setting::String("问候语", "Toast 显示的内容", "Hello, World!"));
	s_hello.addSetting(Setting::Float("时长(秒)", "Toast 停留时间", 2.0f, 0.5f, 10.0f, 0.5f));
	s_hello.onEnable = [](Feature& f) {
		Setting* msg = f.findSetting("问候语");
		Setting* dur = f.findSetting("时长(秒)");
		Notify("Hello World",
			msg ? msg->asString().c_str() : "Hello, World!",
			dur ? (int)(dur->asFloat() * 1000.0f) : 2000,
			Theme::Success, Icon::Star);
	};
	Registry::instance().add(&s_hello);

	s_exit.onEnable = [](Feature&) { g_running = false; };
	Registry::instance().add(&s_exit);

	if (!InitGui()) { printf("InitGui 失败\n"); return 1; }
	RefreshAllHotkeys();

	while (g_running) {
		Tick();
		Sleep(1);
	}

	ShutdownAllFeatures();
	ShutdownGui();
	printf("演示结束\n");
	return 0;
}
