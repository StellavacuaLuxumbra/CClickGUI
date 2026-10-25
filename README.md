# CClickGUI

C++ 开发的 Windows ClickGUI 框架（Win32，无第三方依赖）。

现代化 C++ ClickGUI 框架（Win32，无第三方依赖），提取自 **LuxumbraCilent 3.0** 的 ClickGUI 子系统并重构为独立框架。

## 特性

- **零依赖**：纯 Win32 —— 分层窗口（`WS_EX_LAYERED`，逐像素 Alpha、透明区域自动点击穿透）+ 软件光栅化渲染（CPU 帧缓冲 + GDI 文字），不需要任何图形库。
- **完整控件集**：功能开关（动画滑块）、可拖拽数值滑条（Int/Float，按 step 量化）、下拉选择框（浮层 + 悬停高亮）、颜色选择（设置名含 `色` / `RGB` 即识别，弹出系统取色器）、文本编辑（双击弹框）、分页设置、子功能嵌套、每功能独立快捷键（左键绑定 / 右键清除）。
- **现代交互**：面板标题栏拖拽移动、滚轮 + 可拖拽滚动条、原地键入搜索框（支持中文输入法，实时过滤，ESC 清空 / Enter 确认）、悬停描述提示。
- **全套动画**：面板开合缩放、分类切换滑动指示条、行展开裁剪动画、开关滑块、Toast 进出场 / 内容交叉淡入 / 槽位平滑下移。
- **数据模型**：`Feature`（功能）+ `Setting`（Bool/Int/Float/String/Select）+ `Registry`（分类自动聚合），`onEnable` / `onDisable` / `onTick` 回调，可选独立 worker 线程或 UI 线程驱动。
- **全局热键**：message-only 窗口 + `RegisterHotKey`（须带修饰键），默认 `Alt+C` 呼出面板。
- **配置持久化**：INI 风格文本（`名称.enabled` / `名称.setting.设置名=值`），可开启变更自动保存。
- **Toast 通知**：右上角滑入、按内容自适应尺寸、同 key 去重更新。

## 快速开始

```cpp
#include <CClickGUI.h>
using namespace cgui;

static Feature s_demo("示例功能", "我的分类", "描述文字");

int main() {
    s_demo.addSetting(Setting::Float("速度", "0.5 ~ 10", 2.0f, 0.5f, 10.0f, 0.1f));
    Registry::instance().add(&s_demo);   // 注册后自动附加快捷键设置

    InitGui();                           // 初始化，Alt+C 呼出面板
    RefreshAllHotkeys();
    LoadConfig("my.cfg");                // 可选：恢复配置
    EnableAutoSave("my.cfg");            // 可选：变更自动保存

    while (running) { Tick(); Sleep(1); } // 主循环驱动 GUI

    ShutdownAllFeatures();
    SaveConfig("my.cfg");
    ShutdownGui();
}
```

## 构建

方式一：快速构建脚本（MinGW g++，推荐本机使用）：

```bat
build.bat D:\mingw64\bin\g++.exe
```

方式二：CMake（适合 MSVC / CI / ASCII 路径环境）：

```bash
cmake -B build
cmake --build build --config Release
# 演示程序：build/examples/cgu_demo.exe（控制台宿主，Alt+C 呼出）
```

仅 MinGW g++ 亦可：

```bash
g++ -std=c++17 -O2 -Iinclude src/*.cpp examples/Demo.cpp -o demo.exe -lgdi32 -luser32 -lcomdlg32
```

> 注意：`mingw32-make` 无法处理含中文的工程路径（如 `F:\项目\...`），
> 此情况请用 `build.bat` 或 Ninja / MSVC 生成器。

MSVC 下框架以 `/utf-8` 编译（源码含中文界面字符串）。

## 操作方式

| 操作 | 行为 |
|---|---|
| `Alt+C`（可改） | 呼出 / 关闭面板 |
| 左键功能行 | 启用 / 禁用功能 |
| 右键功能行 | 展开 / 收起设置 |
| 拖拽滑条 | 连续调整数值（按 step 量化） |
| 双击数值 / 文本 | 弹框精确输入 |
| 设置名含「色/RGB」 | 点击弹系统取色器 |
| 点击快捷键行 / 右键 | 绑定组合键 / 清除 |
| 搜索框点击后键入 | 实时过滤（Enter 确认，ESC 清空） |
| 标题栏拖拽 / 滚轮 | 移动面板 / 滚动列表 |

## 目录结构

```
include/CClickGUI.h   公开 API（唯一对外头文件）
src/Internal.h        内部：GuiContext 状态 + detail 声明
src/Util.h            内部：混色 / 缓动 / 编码 / 计时工具
src/Core.cpp          Registry / Feature 生命周期 / GUI 显隐 / 样式
src/Draw.cpp          软件光栅绘图基元（含 GDI 文字混合）
src/Anim.cpp          动画推进
src/Render.cpp        主面板 / 功能行 / 下拉浮层 / Toast 渲染
src/Input.cpp         点击命中分发 / 拖拽 / 滚动 / 搜索键盘输入
src/Dialogs.cpp       输入框 / 取色器 / 快捷键捕获对话框
src/Window.cpp        分层窗口 / UpdateLayeredWindow / Tick
src/Hotkeys.cpp       全局热键线程
src/Config.cpp        配置保存 / 加载 / 自动保存
src/Toasts.cpp        Toast 通知
examples/Demo.cpp     演示程序
```
