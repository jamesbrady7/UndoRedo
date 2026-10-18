cmake_minimum_required(VERSION 3.16)
project(UndoRedoDemo VERSION 1.0.0 LANGUAGES CXX)

# ═══════════════════════════════════════════════════════════════════════
# UndoRedoDemo —— 自研 undo/redo 命令框架 + QGraphicsView 画布编辑器 demo
#
# 三个目标：
#   undoredo_core  STATIC 库，仅链 Qt5::Core（分层干净的结构性证明：
#                  框架层若不小心 include 了 Widgets，这里直接编不过）
#   unit_tests     纯 Core 单元测试，--selftest 退出码=失败数
#   undoredo_demo  GUI 画布编辑器（故意不加 WIN32：保留控制台，
#                  --selftest 无头电池的 printf 输出才看得见）
# ═══════════════════════════════════════════════════════════════════════

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 1. 定位 Qt5（5.15.2 / msvc2019_64），换机器只改这一行 ---
if(NOT DEFINED CMAKE_PREFIX_PATH OR CMAKE_PREFIX_PATH STREQUAL "")
    set(CMAKE_PREFIX_PATH "D:/Qt/5.15.2/msvc2019_64")
endif()
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets)

# --- 2. 自动处理 Q_OBJECT ---
set(CMAKE_AUTOMOC ON)

# --- 3. 输出目录（MSVC 多配置生成器实际产物在 bin/Debug、bin/Release）---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- 4. 框架核心（纯 QtCore，零 Widgets 依赖）---
# ★ CommandStack.h 带 Q_OBJECT，必须【显式列进源列表】AUTOMOC 才会扫它
#   （否则只扫 .cpp 及其同名头 → 链接缺 metaObject，NavTreeGroup 已踩过）
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
target_link_libraries(undoredo_core PUBLIC Qt5::Core)
target_include_directories(undoredo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- 5. 单元测试（14 块自包含，退出码=失败数）---
add_executable(unit_tests
    tests/unit_tests.cpp
)
target_link_libraries(unit_tests PRIVATE undoredo_core)

# --- 5b. 画布 demo（GUI；故意不加 WIN32：--selftest 的输出留在控制台）---
# ★ 带 Q_OBJECT 的头全部显式列出（AUTOMOC 扫描规则，同 core 的注释）
add_executable(undoredo_demo
    src/app/main.cpp
    src/app/MainWindow.h
    src/app/MainWindow.cpp
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

# --- 6. MSVC 编译选项（豁免逐条附理由，不整体静音；故意不加 /WX）---
if(MSVC)
    # /utf-8 : 源码含中文注释/字符串，MSVC 默认按 GBK 解码 → 一堆莫名语法错误
    # /W3    : 从默认 /W1 提到常规级别，噪声用 /wd 精确豁免
    # ⚠⚠ 硬规矩：printf 格式串里只能用 ASCII 符号。
    #    → ★ ─ │ 这些 GBK 表示不了的字符会截断错位 MSVC 的格式串分析 →
    #    C4474「参数太多」。屏蔽 C4474 = 参数检查失效，代价真实，
    #    所以改格式串，而不是 /wd4474。（装饰符号只准出现在注释/正文文本里）
    set(UR_TARGETS undoredo_core unit_tests undoredo_demo)

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
