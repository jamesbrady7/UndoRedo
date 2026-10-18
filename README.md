# UndoRedoDemo — 自研撤销/重做命令框架 + 画布编辑器 Demo

一个**对标并超越 QUndoStack** 的 C++/Qt 撤销重做框架（`undoredo_core`，纯 QtCore），
配一个有一定规模的 QGraphicsView 画布编辑器（增删/拖动/缩放/改色/z 序/成组/对齐）
作为集成演示，外加 14 块框架单测与 10 步无头集成电池。

架构设计对标了 Qt QUndoStack、Eclipse IOperationHistory、Krita KUndo2、FreeCAD、
OpenCASCADE OCAF、Blender、GIMP/Inkscape 与文本编辑器系的合并策略 ——
完整的调研对照与机制精讲见 **[docs/设计精讲.md](docs/设计精讲.md)**。

## 六大差异化能力（QUndoStack 做不到的）

| 能力 | 说明 |
|---|---|
| RAII 事务 | `TransactionScope` 作用域对象：异常/提前 return 自动逆序回滚，空事务自动丢弃 |
| 可插拔合并策略 | `MergePolicy` 栈级策略：相邻同 id + **时间窗**（拖拽/滑条风暴折叠成一条历史），mergeTag 极性阻断 |
| 历史可遍历可跳转 | `history()` 只读 Entry 表（文本/时间/字节/context/merged）+ `setIndex()` 任意点跳转，历史面板点击直达 |
| context 子序列撤销 | Eclipse IUndoContext 式：「只撤销属性改动」精准挖除，游标不动，其余通道净效果不变 |
| 目标安全 | 命令只认对象 id + 数据快照，不持裸指针；删除后 undo 按**原 id 重建**，悬垂在结构上不可能 |
| 序列化就绪 | 命令 = 类型名 + JSON payload，`CommandFactory` 重建回放（跨会话 undo/审计日志的地基） |

另有：条数+字节**双预算**截断、clean 检查点（保存语义）、选择集随命令自动恢复、
最小信号面（`indexChanged` 单信号 + 批量抑制）、协同字段预留（actorId/logicalTick）。

## 构建 & 运行（本机路径已内置，换机器改 CMakeLists 第一处 set）

```bat
D:\CMAKE\bin\cmake.exe -S . -B build -G "Visual Studio 16 2019" -A x64
D:\CMAKE\bin\cmake.exe --build build --config Debug
set PATH=D:\Qt\5.15.2\msvc2019_64\bin;%PATH%
build\bin\Debug\undoredo_demo.exe              :: GUI 画布编辑器
build\bin\Debug\unit_tests.exe                 :: 框架单测 14 块(退出码=失败数)
build\bin\Debug\undoredo_demo.exe --selftest   :: 集成电池 10 步(offscreen)
```

## 交互速览

- 工具栏：加图形 / 删除 / 置顶置底 / 成组解组 / **左对齐·横向分布（事务）** / 保存（clean 检查点）/ 撤销重做（带命令文本）
- 画布：左键拖动=移动（连续事件自动合并成一条历史）、右下角手柄=缩放、框选/Ctrl 多选、点组员=选整组、中键平移、滚轮缩放、Delete 删除
- 属性面板：色相/描边滑条**每个刻度都 push 命令**，500ms 时间窗折叠（观察历史面板的 `<merged>` 标记）
- 历史面板：点击任意行 `setIndex` 跳转；「只撤销一步属性/画布改动」演示 context 子序列撤销

## 工程结构

```
src/core/      框架层(纯 QtCore): Command · CommandStack · CompoundCommand
               MergePolicy · TransactionScope · CommandFactory · Context · SelectionMemento
src/canvas/    画布域: ShapeMeta(纯数据快照) · ShapeItem · CanvasScene(id 注册表) · CanvasView(手势→命令)
src/commands/  业务命令: Edit(增删) · Geometry(移动/缩放) · Property(改色/描边) · Structure(z 序/成组)
src/app/       装配层: MainWindow · InspectorPanel · HistoryDock · SelfTest · main(--selftest 分流)
tests/         unit_tests.cpp: T1~T14
docs/          设计精讲.md: 十四章(调研对照/机制走读/踩坑记录)
```

## 验证基线（2026-10-16，本机实测）

- Debug + Release 双配置三目标构建：**0 error / 0 warning**
- `unit_tests`：86 OK / 0 FAIL，退出码 0（双配置）
- `undoredo_demo --selftest`：34 OK / 0 FAIL，退出码 0（双配置）
- GUI 冒烟：窗口正常启动运行
