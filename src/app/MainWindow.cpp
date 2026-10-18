#include "app/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QKeySequence>
#include <QLabel>
#include <QStatusBar>
#include <QToolBar>
#include <algorithm>
#include "app/HistoryDock.h"
#include "app/InspectorPanel.h"
#include "app/StackBridge.h"
#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "commands/EditCommands.h"
#include "commands/GeometryCommands.h"
#include "commands/StructureCommands.h"
#include "core/CommandStack.h"
#include "core/MergePolicy.h"
#include "core/TransactionScope.h"

namespace cd {

namespace {
// 新图形填充色轮（演示用的确定性调色板）
const QColor kPalette[] = {
    QColor(0x4f, 0x8c, 0xd9), QColor(0xe0, 0x7b, 0x4f), QColor(0x5c, 0xb8, 0x7a),
    QColor(0xb0, 0x6a, 0xc9), QColor(0xd9, 0xb6, 0x4f), QColor(0x4f, 0xc3, 0xc9),
};
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("UndoRedoDemo —— 自研撤销/重做框架 · 画布编辑器"));
    // 构造顺序 stack_ → bridge_ → buildUi()：面板装配时要拿到已存在的桥。
    // 析构顺序由成员声明序保证（见 MainWindow.h 的 ★ 注释）。
    stack_ = std::make_unique<ur::CommandStack>();
    bridge_ = std::make_unique<ur::StackBridge>(stack_.get());  // 故意不挂 parent
    buildUi();
    wireStack();
    onCleanChanged(true);
    resize(1280, 800);
}

// 定义处 StackBridge/CommandStack 均完整（本文件已 include）——unique_ptr
// 的 delete 在这里实例化。析构顺序 = 成员声明逆序：bridge_ 先于 stack_。
MainWindow::~MainWindow() = default;

// ── UI 装配 ─────────────────────────────────────────────────────────────
void MainWindow::buildUi()
{
    scene_ = new CanvasScene(this);
    view_ = new CanvasView(scene_, this);
    view_->setFocusPolicy(Qt::StrongFocus);
    setCentralWidget(view_);

    auto* tb = addToolBar(QStringLiteral("主工具栏"));
    tb->setMovable(false);

    auto* addRect = tb->addAction(QStringLiteral("＋矩形"));
    auto* addEllipse = tb->addAction(QStringLiteral("＋椭圆"));
    auto* addTriangle = tb->addAction(QStringLiteral("＋三角"));
    connect(addRect, &QAction::triggered, this, [this] { addShape(ShapeKind::Rect); });
    connect(addEllipse, &QAction::triggered, this, [this] { addShape(ShapeKind::Ellipse); });
    connect(addTriangle, &QAction::triggered, this, [this] { addShape(ShapeKind::Triangle); });
    tb->addSeparator();

    auto* delAct = tb->addAction(QStringLiteral("删除"));
    delAct->setShortcut(QKeySequence::Delete);
    connect(delAct, &QAction::triggered, this, &MainWindow::deleteSelected);
    connect(view_, &CanvasView::deleteRequested, this, &MainWindow::deleteSelected);
    tb->addSeparator();

    auto* frontAct = tb->addAction(QStringLiteral("置顶"));
    auto* backAct = tb->addAction(QStringLiteral("置底"));
    connect(frontAct, &QAction::triggered, this, &MainWindow::bringToFront);
    connect(backAct, &QAction::triggered, this, &MainWindow::sendToBack);
    tb->addSeparator();

    auto* groupAct = tb->addAction(QStringLiteral("成组"));
    auto* ungroupAct = tb->addAction(QStringLiteral("解组"));
    connect(groupAct, &QAction::triggered, this, &MainWindow::groupSelected);
    connect(ungroupAct, &QAction::triggered, this, &MainWindow::ungroupSelected);
    tb->addSeparator();

    auto* alignAct = tb->addAction(QStringLiteral("左对齐(事务)"));
    auto* distAct = tb->addAction(QStringLiteral("横向分布(事务)"));
    connect(alignAct, &QAction::triggered, this, &MainWindow::alignLeft);
    connect(distAct, &QAction::triggered, this, &MainWindow::distributeH);
    tb->addSeparator();

    auto* selectAllAct = tb->addAction(QStringLiteral("全选"));
    selectAllAct->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAct, &QAction::triggered, this, &MainWindow::selectAll);

    auto* saveAct = tb->addAction(QStringLiteral("保存"));
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, &MainWindow::onSave);
    tb->addSeparator();

    undoAct_ = tb->addAction(QStringLiteral("撤销"));
    redoAct_ = tb->addAction(QStringLiteral("重做"));
    undoAct_->setShortcut(QKeySequence::Undo);            // Ctrl+Z
    redoAct_->setShortcut(QKeySequence::Redo);            // Ctrl+Y (Windows)
    undoAct_->setEnabled(false);
    redoAct_->setEnabled(false);

    // 右侧 dock：属性面板 + 历史面板（tab 化；面板是 QWidget，需包 QDockWidget）
    inspector_ = new InspectorPanel(scene_, stack_.get(), this);
    historyDock_ = new HistoryDock(stack_.get(), bridge_.get(), this);
    auto* inspDock = new QDockWidget(QStringLiteral("属性"), this);
    inspDock->setWidget(inspector_);
    auto* histDock = new QDockWidget(QStringLiteral("历史"), this);
    histDock->setWidget(historyDock_);
    addDockWidget(Qt::RightDockWidgetArea, inspDock);
    addDockWidget(Qt::RightDockWidgetArea, histDock);
    tabifyDockWidget(inspDock, histDock);

    cleanLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(cleanLabel_);
    statusBar()->showMessage(
        QStringLiteral("拖动图形/滑条观察历史合并；点击历史行跳转；Delete 删除；中键平移；滚轮缩放"),
        8000);
}

// ── 栈接线（框架机制与编辑器域的唯一交汇点）─────────────────────────────
void MainWindow::wireStack()
{
    stack_->setMergePolicy(std::make_unique<ur::TimeWindowMergePolicy>(500));

    // 选择集自动快照：push 前后各盖一次章，undo/redo 时由 applier 恢复
    stack_->setSelectionProvider([this] {
        return ur::SelectionMemento::fromSeq(scene_->selectedIds());
    });
    stack_->setSelectionApplier([this](const ur::SelectionMemento& m) {
        QList<ShapeId> ids;
        ids.reserve(int(m.ids.size()));
        for (uint64_t id : m.ids)
            ids.push_back(ShapeId(id));
        scene_->selectIds(ids);
    });

    // 最小通知面：core 的 Observer 回调经 StackBridge 重发为 indexChanged
    // 一个 Qt 信号，驱动 action 文本/可用态刷新
    connect(bridge_.get(), &ur::StackBridge::indexChanged, this, &MainWindow::onIndexChanged);
    connect(bridge_.get(), &ur::StackBridge::cleanChanged, this, &MainWindow::onCleanChanged);
    // stack 不再是 QObject，不能当 connect 的接收者 context——用 this
    //（返回值丢弃，同旧版把 &CommandStack::undo 当槽连接的语义）
    connect(undoAct_, &QAction::triggered, this, [this] { stack_->undo(); });
    connect(redoAct_, &QAction::triggered, this, [this] { stack_->redo(); });

    view_->setCommandStack(stack_.get());
    onIndexChanged(0);
}

// ── 命令生产（每个按钮 = 一种命令/机制的入口）───────────────────────────
void MainWindow::addShape(ShapeKind kind)
{
    ShapeMeta m;
    m.kind = kind;
    m.name = QStringLiteral("%1 %2").arg(shapeKindName(kind)).arg(++spawnCount_);
    m.fill = kPalette[spawnCount_ % int(sizeof(kPalette) / sizeof(kPalette[0]))];
    m.pos = nextSpawnPos();
    if (kind == ShapeKind::Ellipse)
        m.size = QSizeF(100, 100);
    else if (kind == ShapeKind::Triangle)
        m.size = QSizeF(110, 95);
    m.z = scene_->nextTopZ();
    stack_->push(std::make_unique<AddShapeCommand>(scene_, m));
}

QPointF MainWindow::nextSpawnPos() const
{
    const QPointF center = view_->mapToScene(view_->viewport()->rect().center());
    const qreal off = (spawnCount_ % 8) * 26.0;       // 错位落点，避免完全重叠
    return QPointF(center.x() - 60 + off, center.y() - 40 + off * 0.6);
}

void MainWindow::deleteSelected()
{
    const QList<ShapeId> ids = scene_->selectedIds();
    if (ids.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("没有选中图形可删除"), 2000);
        return;
    }
    stack_->push(std::make_unique<RemoveShapesCommand>(scene_, ids));
}

void MainWindow::bringToFront()
{
    const QList<ShapeId> ids = scene_->selectedIds();
    if (!ids.isEmpty())
        stack_->push(std::make_unique<ReorderZCommand>(scene_, ids, true));
}

void MainWindow::sendToBack()
{
    const QList<ShapeId> ids = scene_->selectedIds();
    if (!ids.isEmpty())
        stack_->push(std::make_unique<ReorderZCommand>(scene_, ids, false));
}

void MainWindow::groupSelected()
{
    const QList<ShapeId> ids = scene_->selectedIds();
    if (ids.size() < 2) {
        statusBar()->showMessage(QStringLiteral("成组需要选中至少 2 个图形"), 2000);
        return;
    }
    stack_->push(std::make_unique<SetGroupCommand>(scene_, ids, scene_->nextGroupId()));
}

void MainWindow::ungroupSelected()
{
    const QList<ShapeId> ids = scene_->selectedIds();
    if (ids.isEmpty())
        return;
    stack_->push(std::make_unique<SetGroupCommand>(scene_, ids, 0));
}

// ── 事务演示：多条移动命令 → 一条 Compound 历史 ─────────────────────────
void MainWindow::alignLeft()
{
    const QList<ShapeId> ids = scene_->selectedIds();
    if (ids.size() < 2) {
        statusBar()->showMessage(QStringLiteral("对齐需要选中至少 2 个图形"), 2000);
        return;
    }
    qreal minX = 1e9;
    for (ShapeId id : ids) {
        ShapeMeta m;
        if (scene_->getMeta(id, &m))
            minX = qMin(minX, m.pos.x());
    }
    ur::TransactionScope tx(stack_.get(), "左对齐");
    for (ShapeId id : ids) {
        ShapeMeta m;
        if (!scene_->getMeta(id, &m) || qFuzzyCompare(m.pos.x(), minX))
            continue;                               // 已在目标位的跳过（空事务防线）
        QHash<ShapeId, QPointF> from{{id, m.pos}};
        QHash<ShapeId, QPointF> to{{id, QPointF(minX, m.pos.y())}};
        stack_->push(std::make_unique<MoveShapesCommand>(scene_, from, to, 0));
    }
    tx.commit();                                    // 析构结算：≥2 条 → 一条 Compound
}

void MainWindow::distributeH()
{
    const QList<ShapeId> ids = scene_->selectedIds();
    if (ids.size() < 3) {
        statusBar()->showMessage(QStringLiteral("横向分布需要选中至少 3 个图形"), 2000);
        return;
    }
    // (id, meta) 成对排序——只排 meta 会丢 id（初版就栽在这里）
    QList<QPair<ShapeId, ShapeMeta>> items;
    for (ShapeId id : ids) {
        ShapeMeta m;
        if (scene_->getMeta(id, &m))
            items.append({id, m});
    }
    if (items.size() < 3)
        return;
    std::sort(items.begin(), items.end(),
              [](const QPair<ShapeId, ShapeMeta>& a, const QPair<ShapeId, ShapeMeta>& b) {
                  return a.second.pos.x() < b.second.pos.x();
              });
    const qreal firstCx = items.first().second.pos.x() + items.first().second.size.width() / 2;
    const qreal lastCx = items.last().second.pos.x() + items.last().second.size.width() / 2;
    const qreal step = (lastCx - firstCx) / qreal(items.size() - 1);

    ur::TransactionScope tx(stack_.get(), "横向分布");
    for (int i = 0; i < items.size(); ++i) {
        const ShapeId id = items[i].first;
        const ShapeMeta& m = items[i].second;
        const qreal newX = firstCx + step * i - m.size.width() / 2;
        if (qFuzzyCompare(newX + 1.0, m.pos.x() + 1.0))
            continue;                               // 已在目标位的跳过
        QHash<ShapeId, QPointF> from{{id, m.pos}};
        QHash<ShapeId, QPointF> to{{id, QPointF(newX, m.pos.y())}};
        stack_->push(std::make_unique<MoveShapesCommand>(scene_, from, to, 0));
    }
    tx.commit();
}

void MainWindow::selectAll()
{
    scene_->selectIds(scene_->allIds());
}

void MainWindow::onSave()
{
    stack_->setClean();                             // demo 语义：保存 = 记检查点
    statusBar()->showMessage(QStringLiteral("已保存（clean 检查点已记录在当前历史位置）"), 3000);
}

// ── 信号槽：最小通知面 → 全部 UI 刷新 ───────────────────────────────────
void MainWindow::onIndexChanged(int index)
{
    const std::vector<ur::CommandStack::Entry> h = stack_->history();
    undoAct_->setEnabled(stack_->canUndo());
    redoAct_->setEnabled(stack_->canRedo());
    // Entry.text 是 UTF-8 std::string → 上 UI 统一 fromStdString（边界纪律）
    undoAct_->setText(index > 0 && index - 1 < int(h.size())
                      ? QStringLiteral("撤销 %1").arg(QString::fromStdString(h[size_t(index - 1)].text))
                      : QStringLiteral("撤销"));
    redoAct_->setText(index < int(h.size())
                      ? QStringLiteral("重做 %1").arg(QString::fromStdString(h[size_t(index)].text))
                      : QStringLiteral("重做"));
}

void MainWindow::onCleanChanged(bool clean)
{
    if (cleanLabel_)
        cleanLabel_->setText(clean ? QStringLiteral("● 已保存")
                                   : QStringLiteral("○ 未保存"));
    setWindowTitle(QStringLiteral("UndoRedoDemo —— 自研撤销/重做框架 · 画布编辑器%1")
                       .arg(clean ? QString() : QStringLiteral(" *")));
}

} // namespace cd
