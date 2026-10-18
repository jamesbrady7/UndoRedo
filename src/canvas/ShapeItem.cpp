#include "canvas/ShapeItem.h"

#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionGraphicsItem>

namespace cd {

namespace {
constexpr qreal kHandleSize = 10.0;   // 缩放手柄边长（场景单位）
constexpr qreal kMargin = 6.0;        // boundingRect 余量（描边+手柄+选中框）
}

ShapeItem::ShapeItem(ShapeId id, const ShapeMeta& meta, QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , id_(id)
    , meta_(meta)
{
    setFlags(ItemIsSelectable);       // 注意：故意不给 ItemIsMovable（见头文件纪律）
    setZValue(meta_.z);
    setPos(meta_.pos);
    setToolTip(meta_.name);
}

void ShapeItem::setMeta(const ShapeMeta& meta)
{
    if (meta_ == meta)
        return;
    prepareGeometryChange();
    const bool posChanged = meta_.pos != meta.pos;
    const bool sizeChanged = meta_.size != meta.size;
    meta_ = meta;
    if (posChanged)
        setPos(meta_.pos);
    if (meta_.z != zValue())
        setZValue(meta_.z);
    setToolTip(meta_.name);
    if (sizeChanged)
        update();
}

QRectF ShapeItem::boundingRect() const
{
    return QRectF(-kMargin, -kMargin,
                  meta_.size.width() + 2 * kMargin,
                  meta_.size.height() + 2 * kMargin);
}

QPainterPath ShapeItem::kindPath() const
{
    QPainterPath path;
    const QRectF r(0, 0, meta_.size.width(), meta_.size.height());
    switch (meta_.kind) {
    case ShapeKind::Rect:
        path.addRoundedRect(r, 6, 6);
        break;
    case ShapeKind::Ellipse:
        path.addEllipse(r);
        break;
    case ShapeKind::Triangle:
        path.moveTo(r.center().x(), r.top());
        path.lineTo(r.right(), r.bottom());
        path.lineTo(r.left(), r.bottom());
        path.closeSubpath();
        break;
    }
    return path;
}

QPainterPath ShapeItem::shape() const
{
    return kindPath();                // 命中区收紧到图形本体
}

QRectF ShapeItem::handleRectLocal() const
{
    return QRectF(meta_.size.width() - kHandleSize / 2,
                  meta_.size.height() - kHandleSize / 2,
                  kHandleSize, kHandleSize);
}

void ShapeItem::paint(QPainter* p, const QStyleOptionGraphicsItem* opt, QWidget* w)
{
    Q_UNUSED(opt)
    Q_UNUSED(w)
    p->setRenderHint(QPainter::Antialiasing);

    // ── 本体：填充 + 描边 ──
    const QPainterPath body = kindPath();
    p->fillPath(body, meta_.fill);
    QPen pen(meta_.stroke, meta_.strokeWidth);
    p->setPen(pen);
    p->drawPath(body);

    // ── 选中态：虚线外框 + 右下角缩放手柄 ──
    if (isSelected()) {
        QPen selPen(QColor(0x2f, 0x9e, 0xff), 1.5, Qt::DashLine);
        selPen.setCosmetic(true);         // 线宽不随视图缩放
        p->setPen(selPen);
        p->setBrush(Qt::NoBrush);
        p->drawRect(QRectF(0, 0, meta_.size.width(), meta_.size.height()).adjusted(-2, -2, 2, 2));

        p->setPen(QPen(QColor(0x2f, 0x9e, 0xff), 1));
        p->setBrush(Qt::white);
        p->drawRect(handleRectLocal());
    }
}

} // namespace cd
