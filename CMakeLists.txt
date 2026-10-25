cmake_minimum_required(VERSION 3.16)
project(UndoRedoDemo VERSION 1.0.0 LANGUAGES CXX)

# ═══════════════════════════════════════════════════════════════════════
# UndoRedoDemo —— 自研 undo/redo 命令框架 + QGraphicsView 画布编辑器 demo
#
# 三个目标：
#   undoredo_core  STATIC 库，纯 C++17 零依赖（不链任何 Qt——链接器当
#                  守门员：框架层若不小心 include 了 Qt 头，这里直接编不过。
#                  通知面走内嵌 Observer 接口，Qt 信号由 app 层 StackBridge 桥接）
#   unit_tests     纯 C++ 单元测试（与 core 一样零 Qt），退出码=失败数
#   undoredo_demo  GUI 画布编辑器（故意不加 WIN32：保留控制台，
#                  --selftest 无头电池的 printf 输出才看得见）
# ═══════════════════════════════════════════════════════════════════════

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 1. 定位 Qt5（5.15.2 / msvc2019_64），换机器只改这一行 ---
# 只有 demo 目标需要 Qt；Widgets 会传递依赖 Core/Gui，显式只列 Widgets
# 正是"core/tests 不碰 Qt"的声明式表达。
# ★ UR_BUILD_DEMO=OFF 时根本不 find_package(Qt5)：纯 core/单元测试可以在
#   完全没装 Qt 的机器（或 CI 容器）上配置+构建通过——"core 零依赖"从
#   编译期承诺升级为构建系统承诺（此前 find_package 是 REQUIRED 全局的，
#   没 Qt 连 unit_tests 都配不出来）。
option(UR_BUILD_DEMO "Build the Qt5 Widgets canvas demo (needs Qt5)" ON)
if(UR_BUILD_DEMO)
    if(NOT DEFINED CMAKE_PREFIX_PATH OR CMAKE_PREFIX_PATH STREQUAL "")
        set(CMAKE_PREFIX_PATH "D:/Qt/5.15.2/msvc2019_64")
    endif()
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
endif()

# --- 2. 自动处理 Q_OBJECT（仅 demo 有 Q_OBJECT；core/tests 空转无害）---
if(UR_BUILD_DEMO)
    set(CMAKE_AUTOMOC ON)
endif()

# --- 3. 输出目录（MSVC 多配置生成器实际产物在 bin/Debug、bin/Release）---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- 4. 框架核心（纯 C++17 零依赖：不链 Qt，AUTOMOC 对其空转无害）---
add_library(undoredo_core STATIC
    src/core/Command.h
    src/core/Context.h
    src/core/SelectionMemento.h
    src/core/MergePolicy.h
    src/core/MergePolicy.cpp
    src/core/CompoundCommand.h
    src/core/CompoundCommand.cpp
    src/core/CommandFactory.h
    src/core/CommandFactory.cpp
    src/core/CommandStack.h
    src/core/CommandStack.cpp
    src/core/TransactionScope.h
)
target_include_directories(undoredo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# ★ 不写 target_link_libraries(undoredo_core ... Qt5::*)——core 零依赖是
#   结构性承诺：任何 Qt include 混进 core 都会在这里编译失败，链接器守门。

# --- 5. 单元测试（14 块自包含，退出码=失败数；与 core 一样零 Qt）---
add_executable(unit_tests
    tests/unit_tests.cpp
)
target_link_libraries(unit_tests PRIVATE undoredo_core)

# --- 5b. 画布 demo（GUI；故意不加 WIN32：--selftest 的输出留在控制台）---
# ★ 带 Q_OBJECT 的头全部显式列出（AUTOMOC 只扫源列表内文件及其同名头，
#   漏列 → 链接缺 metaObject，NavTreeGroup 已踩过；StackBridge.h 同理）
if(UR_BUILD_DEMO)
add_executable(undoredo_demo
    src/app/main.cpp
    src/app/MainWindow.h
    src/app/MainWindow.cpp
    src/app/StackBridge.h
    src/app/StackBridge.cpp
    src/app/InspectorPanel.h
    src/app/InspectorPanel.cpp
    src/app/HistoryDock.h
    src/app/HistoryDock.cpp
    src/app/SelfTest.h
    src/app/SelfTest.cpp
    src/canvas/ShapeMeta.h
    src/canvas/ShapeItem.h
    src/canvas/ShapeItem.cpp
    src/canvas/CanvasScene.h
    src/canvas/CanvasScene.cpp
    src/canvas/CanvasView.h
    src/canvas/CanvasView.cpp
    src/commands/CommandIds.h
    src/commands/EditCommands.h
    src/commands/EditCommands.cpp
    src/commands/GeometryCommands.h
    src/commands/GeometryCommands.cpp
    src/commands/PropertyCommands.h
    src/commands/PropertyCommands.cpp
    src/commands/StructureCommands.h
    src/commands/StructureCommands.cpp
)
target_link_libraries(undoredo_demo PRIVATE undoredo_core Qt5::Widgets)
endif()   # UR_BUILD_DEMO

# --- 6. MSVC 编译选项（豁免逐条附理由，不整体静音；故意不加 /WX）---
if(MSVC)
    # /utf-8 : 源码含中文注释/字符串，MSVC 默认按 GBK 解码 → 一堆莫名语法错误
    # /W3    : 从默认 /W1 提到常规级别，噪声用 /wd 精确豁免
    # ⚠⚠ 硬规矩：printf 格式串里只能用 ASCII 符号。
    #    → ★ ─ │ 这些 GBK 表示不了的字符会截断错位 MSVC 的格式串分析 →
    #    C4474「参数太多」。屏蔽 C4474 = 参数检查失效，代价真实，
    #    所以改格式串，而不是 /wd4474。（装饰符号只准出现在注释/正文文本里）
    set(UR_TARGETS undoredo_core unit_tests)
    if(UR_BUILD_DEMO)
        list(APPEND UR_TARGETS undoredo_demo)
    endif()

    # 4127: conditional expression is constant —— Q_UNUSED/constexpr 判断必然触发
    # 4514: unreferenced inline function removed —— 头文件 inline 未用到，无害
    # 4668: preprocessor macro is not defined —— Qt 的 Q_OS_*/Q_CC_* 探测宏
    # 4819: 代码页警告 —— 已实证是装饰性 Unicode 符号触发的误报（/utf-8 已在，
    #       源文件合法 UTF-8 无 BOM；详见 NavTreeGroup CMakeLists 的调查记录）
    set(UR_WARN_EXEMPT /wd4127 /wd4514 /wd4668 /wd4819)

    foreach(t IN LISTS UR_TARGETS)
        target_compile_options(${t} PRIVATE /utf-8 /W3)
        target_compile_options(${t} PRIVATE ${UR_WARN_EXEMPT})
    endforeach()
endif()
