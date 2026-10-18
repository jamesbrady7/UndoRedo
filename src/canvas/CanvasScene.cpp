#include "canvas/CanvasScene.h"

#include <QPainter>
#include <algorithm>
#include "canvas/ShapeItem.h"

namespace cd {

CanvasScene::CanvasScene(QObject* parent)
    : QGraphicsScene(parent)
{
    setSceneRect(-600, -400, 1600, 1200);
}

// ── 对象生命周期 ────────────────────────────────────────────────────────
ShapeId CanvasScene::createShape(const ShapeMeta& meta)
{
    const ShapeId id = nextId_++;
    createShapeWithId(id, meta);
    return id;
}

ShapeItem* CanvasScene::createShapeWithId(ShapeId id, const ShapeMeta& m)
{
    if (id == 0 || shapes_.contains(id))
        return nullptr;                 // id 复用冲突：拒绝（防御 undo 双重建）
    auto* item = new ShapeItem(id, m);
    addItem(item);                      // 场景接管 item 生命周期
    registerItem(id, item);
    if (id >= nextId_)                  // 重建的老 id 不影响分配器
        nextId_ = id + 1;
    return item;
}

void CanvasScene::removeShape(ShapeId id)
{
    ShapeItem* item = findShape(id);
    if (!item)
        return;                         // 已经不在了：no-op（幂等纪律）
    shapes_.remove(id);                 // 先摘账再销毁
    removeItem(item);
    delete item;
}

ShapeItem* CanvasScene::findShape(ShapeId id) const
{
    return shapes_.value(id, nullptr);
}

bool CanvasScene::getMeta(ShapeId id, ShapeMeta* out) const
{
    ShapeItem* item = findShape(id);
    if (!item || !out)
        return false;
    *out = item->meta();
    return true;
}

void CanvasScene::setShapeMeta(ShapeId id, const ShapeMeta& meta)
{
    if (ShapeItem* item = findShape(id))
        item->setMeta(meta);            // 找不到 → no-op（目标安全）
}

QList<ShapeId> CanvasScene::allIds() const
{
    QList<ShapeId> ids = shapes_.keys();
    std::sort(ids.begin(), ids.end(), [this](ShapeId a, ShapeId b) {
        return shapes_[a]->zValue() < shapes_[b]->zValue();
    });
    return ids;
}

// ── z 序与分组 ──────────────────────────────────────────────────────────
qreal CanvasScene::nextTopZ() const
{
    qreal top = 0.0;
    for (auto it = shapes_.cbegin(); it != shapes_.cend(); ++it)
        top = qMax(top, it.value()->zValue());
    return top + 1.0;
}

qreal CanvasScene::nextBottomZ() const
{
    qreal bottom = 0.0;
    for (auto it = shapes_.cbegin(); it != shapes_.cend(); ++it)
        bottom = qMin(bottom, it.value()->zValue());
    return bottom - 1.0;
}

ShapeId CanvasScene::nextGroupId()
{
    return nextGroup_++;
}

QList<ShapeId> CanvasScene::groupMembers(ShapeId groupId) const
{
    QList<ShapeId> members;
    if (groupId == 0)
        return members;
    for (auto it = shapes_.cbegin(); it != shapes_.cend(); ++it) {
        if (it.value()->meta().groupId == groupId)
            members.push_back(it.key());
    }
    return members;
}

// ── 选择集收口 ──────────────────────────────────────────────────────────
QList<ShapeId> CanvasScene::selectedIds() const
{
    QList<ShapeId> ids;
    const QList<QGraphicsItem*> sel = selectedItems();
    ids.reserve(sel.size());
    for (QGraphicsItem* it : sel) {
        if (it->type() == ShapeItem::Type)
            ids.push_back(static_cast<ShapeItem*>(it)->id());
    }
    std::sort(ids.begin(), ids.end());  // 稳定顺序（mergeTag 依赖确定性）
    return ids;
}

void CanvasScene::selectIds(const QList<ShapeId>& ids)
{
    clearSelection();
    for (ShapeId id : ids) {
        if (ShapeItem* item = findShape(id))
            item->setSelected(true);    // 找不到的 id 静默跳过（对象可能已删）
    }
}

void CanvasScene::registerItem(ShapeId id, ShapeItem* item)
{
    shapes_.insert(id, item);
}

// ── 背景点阵网格（Workbench 同款手法：场景坐标画点，随缩放自适应）──────
void CanvasScene::drawBackground(QPainter* p, const QRectF& rect)
{
    QGraphicsScene::drawBackground(p, rect);
    const qreal step = 24.0;
    const qreal left = std::floor(rect.left() / step) * step;
    const qreal top = std::floor(rect.top() / step) * step;
    p->save();
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(0, 0, 0, 28));
    for (qreal x = left; x < rect.right(); x += step)
        for (qreal y = top; y < rect.bottom(); y += step)
            p->drawEllipse(QPointF(x, y), 1.4, 1.4);
    p->restore();
}

} // namespace cd
