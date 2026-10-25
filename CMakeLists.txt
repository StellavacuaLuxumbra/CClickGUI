cmake_minimum_required(VERSION 3.15)
project(CClickGUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CClickGUI 静态库
add_library(clickgui STATIC
    src/Core.cpp
    src/Draw.cpp
    src/Anim.cpp
    src/Render.cpp
    src/Toasts.cpp
    src/Dialogs.cpp
    src/Input.cpp
    src/Window.cpp
    src/Hotkeys.cpp
    src/Config.cpp
    src/Effects.cpp
)
target_include_directories(clickgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(clickgui PUBLIC cxx_std_17)

if(MSVC)
    # 源码为 UTF-8（含中文界面字符串）
    target_compile_options(clickgui PUBLIC /utf-8 /W3)
else()
    target_compile_options(clickgui PUBLIC -Wall -O2)
endif()

target_link_libraries(clickgui PUBLIC user32 gdi32 comdlg32)

# 演示程序（可选）
add_subdirectory(examples)
