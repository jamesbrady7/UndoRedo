#pragma once
// ════════════════════════════════════════════════════════════════════════
// MainWindow —— 装配层：把 栈 / 场景 / 视图 / 面板 接成一台完整编辑器
//
// 接线要点（每一条都对应框架的一个机制）：
//   · stack 装 TimeWindowMergePolicy(500ms)      —— 拖拽/滑条合并
//   · SelectionProvider/Applier 接 scene          —— 选择集自动随命令恢复
//   · undo/redo action 文本从 history() 取        —— 可遍历历史的直接受益
//   · Observer→StackBridge→indexChanged 单信号驱动全部 UI 刷新 —— 最小通知面
//     （core 零 Qt：栈发 Observer 回调，StackBridge 在 app 层重发为 Qt 信号）
//   · clean 检查点接"保存"按钮与窗口标题 * 号      —— 文档脏标记
//   · 对齐/分布用 TransactionScope                —— 多命令事务演示
// ════════════════════════════════════════════════════════════════════════
#include <QMainWindow>
#include "app/StackBridge.h"        // StackHost（值成员，需要完整类型）
#include "canvas/ShapeMeta.h"

class QAction;
class QLabel;

namespace cd {

class CanvasScene;
class CanvasView;
class InspectorPanel;
class HistoryDock;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    // 出线析构：显式按依赖顺序拆 UI（view → 面板 → scene），理由见 .cpp
    ~MainWindow() override;

    // selftest 无头电池的驱动入口（非 const：栈是可写对象，const 成员函数
    // 里 &stackHost_.stack 会退化成 const 指针）
    CanvasScene* scene() const { return scene_; }
    ur::CommandStack* stack() { return &stackHost_.stack; }

public slots:
    void addShape(ShapeKind kind);
    void deleteSelected();
    void bringToFront();
    void sendToBack();
    void groupSelected();
    void ungroupSelected();
    void alignLeft();                   // TransactionScope 演示（多条移动 → 一条历史）
    void distributeH();                 // 同上
    void selectAll();
    void onSave();                      // "保存" = 记 clean 检查点（demo 语义，不写盘）

private slots:
    void onIndexChanged(int index);
    void onCleanChanged(bool clean);

private:
    void buildUi();
    void wireStack();
    QPointF nextSpawnPos() const;

    CanvasScene* scene_ = nullptr;
    CanvasView* view_ = nullptr;
    // ★ stack 与 bridge 装在一个复合体里：声明序 stack → bridge，析构逆序
    //   bridge → stack，"bridge 必须先于 stack 死"由编译器保证（StackBridge.h）。
    //   bridge 绝不挂 QObject parent（parent 化会推迟到 ~QObject 阶段 → UAF）。
    ur::StackHost stackHost_;
    InspectorPanel* inspector_ = nullptr;
    HistoryDock* historyDock_ = nullptr;

    QAction* undoAct_ = nullptr;
    QAction* redoAct_ = nullptr;
    QLabel* cleanLabel_ = nullptr;

    int spawnCount_ = 0;                // 命名/落点错位计数
};

} // namespace cd
