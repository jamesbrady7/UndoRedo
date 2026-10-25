#include "canvas/CanvasView.h"

#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include "canvas/CanvasScene.h"
#include "canvas/ShapeItem.h"
#include "commands/GeometryCommands.h"
#include "core/CommandStack.h"

namespace cd {

namespace {
constexpr qreal kMinSize = 24.0;        // 缩放下限（场景单位）
}

CanvasView::CanvasView(CanvasScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
    , scene_(scene)
{
    setRenderHint(QPainter::Antialiasing);
    setDragMode(RubberBandDrag);                    // 空白处框选
    setTransformationAnchor(AnchorUnderMouse);      // 滚轮缩放跟随鼠标
    setResizeAnchor(AnchorViewCenter);
    setBackgroundBrush(QColor(0xf7, 0xf7, 0xfa));
    setMouseTracking(true);
}

// ── 按下：分流 缩放手柄 / 移动手势 / 框选 ─────────────────────────────
void CanvasView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MiddleButton) {          // 中键平移
        gesture_ = Gesture::Pan;
        lastPanPos_ = e->pos();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    if (e->button() != Qt::LeftButton) {
        QGraphicsView::mousePressEvent(e);
        return;
    }

    const QPointF sp = mapToScene(e->pos());

    // 1) 单选态命中右下角手柄 → 缩放手势
    if (beginResizeIfHandleHit(sp)) {
        e->accept();
        return;
    }

    // 2) 命中图形 → 选择（含成组联动）+ 移动手势
    QGraphicsItem* hit = scene_ ? scene_->itemAt(sp, transform()) : nullptr;
    ShapeItem* si = (hit && hit->type() == ShapeItem::Type)
                    ? static_cast<ShapeItem*>(hit) : nullptr;
    if (si) {
        const bool ctrl = e->modifiers() & Qt::ControlModifier;
        if (ctrl) {
            si->setSelected(!si->isSelected());
        } else if (!si->isSelected()) {
            scene_->clearSelection();
            const ShapeId gid = si->meta().groupId;
            if (gid != 0)
                scene_->selectIds(scene_->groupMembers(gid));   // 点组员=选整组
            else
                si->setSelected(true);
        }
        startMoveGesture(sp);
        e->accept();
        return;
    }

    // 3) 空白 → 交回基类（橡皮筋框选）
    QGraphicsView::mousePressEvent(e);
}

bool CanvasView::beginResizeIfHandleHit(const QPointF& scenePos)
{
    if (!scene_ || !stack_)
        return false;
    const QList<ShapeId> sel = scene_->selectedIds();
    if (sel.size() != 1)
        return false;                               // 多选不提供手柄缩放
    ShapeItem* item = scene_->findShape(sel.first());
    if (!item || !item->handleRectScene().contains(scenePos))
        return false;

    resizeId_ = sel.first();
    resizeOrigRect_ = QRectF(item->meta().pos, item->meta().size);
    startScenePos_ = scenePos;
    gesture_ = Gesture::Resize;
    ++gestureId_;                                   // 新手势 = 新合并段
    return true;
}

void CanvasView::startMoveGesture(const QPointF& scenePos)
{
    gesture_ = Gesture::Move;
    lastScenePos_ = scenePos;
    ++gestureId_;
}

// ── 拖动：每个事件一条命令（push 即执行 = 实时反馈）───────────────────
void CanvasView::mouseMoveEvent(QMouseEvent* e)
{
    if (gesture_ == Gesture::Pan) {
        const QPoint d = e->pos() - lastPanPos_;
        lastPanPos_ = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        e->accept();
        return;
    }
    const QPointF sp = mapToScene(e->pos());
    if (gesture_ == Gesture::Move) {
        pushMoveStep(sp);
        e->accept();
        return;
    }
    if (gesture_ == Gesture::Resize) {
        pushResizeStep(sp);
        e->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(e);               // 框选/悬停交基类
}

void CanvasView::pushMoveStep(const QPointF& scenePos)
{
    if (!scene_ || !stack_)
        return;
    const QPointF delta = scenePos - lastScenePos_;
    if (qFuzzyIsNull(delta.x()) && qFuzzyIsNull(delta.y()))
        return;
    lastScenePos_ = scenePos;

    QHash<ShapeId, QPointF> from, to;
    const QList<ShapeId> ids = scene_->selectedIds();
    for (ShapeId id : ids) {
        ShapeMeta m;
        if (!scene_->getMeta(id, &m))
            continue;
        from.insert(id, m.pos);
        to.insert(id, m.pos + delta);
    }
    if (from.isEmpty())
        return;
    stack_->push(std::make_unique<MoveShapesCommand>(scene_, from, to, gestureId_));
    // TimeWindow 策略把本手势内的连续 push 折叠成一条历史；
    // 松手后 gestureId_ 递增，下一段拖拽必然另起一条。
}

void CanvasView::pushResizeStep(const QPointF& scenePos)
{
    if (!scene_ || !stack_ || resizeId_ == 0)
        return;
    ShapeMeta m;
    if (!scene_->getMeta(resizeId_, &m)) {
        gesture_ = Gesture::None;                   // 目标中途消失（防御）
        return;
    }
    const QPointF d = scenePos - startScenePos_;
    QSizeF newSize(qMax(kMinSize, resizeOrigRect_.width() + d.x()),
                   qMax(kMinSize, resizeOrigRect_.height() + d.y()));
    const QRectF fromRect(m.pos, m.size);           // 当下 = from
    const QRectF toRect(resizeOrigRect_.topLeft(), newSize);   // 绝对终态
    if (fromRect == toRect)
        return;
    stack_->push(std::make_unique<ResizeShapeCommand>(scene_, resizeId_, fromRect, toRect,
                                                      gestureId_));
}

void CanvasView::mouseReleaseEvent(QMouseEvent* e)
{
    if (gesture_ == Gesture::Pan && e->button() == Qt::MiddleButton) {
        gesture_ = Gesture::None;
        unsetCursor();
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton && gesture_ != Gesture::None) {
        gesture_ = Gesture::None;                   // 手势结束（gestureId 下次按下才变）
        e->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

// ── 滚轮缩放 ───────────────────────────────────────────────────────────
void CanvasView::wheelEvent(QWheelEvent* e)
{
    const double factor = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    const double newScale = transform().m11() * factor;
    if (newScale < 0.2 || newScale > 8.0)           // 缩放限位
        return;
    scale(factor, factor);
    e->accept();
}

// Delete / Ctrl+Z / Ctrl+Y 全部由 MainWindow 的 QAction 快捷键接管
// （WindowShortcut 优先于视图按键），视图不再重复处理——见 MainWindow.cpp
// buildUi 里"删除"action 处的注释。

} // namespace cd
