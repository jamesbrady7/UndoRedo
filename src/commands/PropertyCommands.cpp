#include "commands/PropertyCommands.h"

#include <QJsonArray>
#include "canvas/CanvasScene.h"
#include "core/Context.h"

namespace cd {

// ════════════════════════════════════════════════════════════════════════
// SetFillCommand
// ════════════════════════════════════════════════════════════════════════
SetFillCommand::SetFillCommand(CanvasScene* scene, QList<ShapeId> ids,
                               QHash<ShapeId, QColor> from, QColor to)
    : scene_(scene), ids_(std::move(ids)), from_(std::move(from)), to_(to)
{
}

void SetFillCommand::apply(const QColor& c)
{
    if (!scene_)
        return;
    for (ShapeId id : ids_) {
        ShapeMeta m;
        if (!scene_->getMeta(id, &m))
            continue;                       // 目标已删 → no-op
        m.fill = c;                         // 绝对终态 → 幂等
        scene_->setShapeMeta(id, m);
    }
}

void SetFillCommand::applyMap(const QHash<ShapeId, QColor>& map)
{
    if (!scene_)
        return;
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        ShapeMeta m;
        if (!scene_->getMeta(it.key(), &m))
            continue;
        m.fill = it.value();
        scene_->setShapeMeta(it.key(), m);
    }
}

void SetFillCommand::redo() { apply(to_); }
void SetFillCommand::undo() { applyMap(from_); }

QString SetFillCommand::text() const
{
    return ids_.size() == 1 ? QStringLiteral("修改填充色")
                            : QStringLiteral("修改 %1 个图形的填充色").arg(ids_.size());
}

QString SetFillCommand::mergeTag() const
{
    return QStringLiteral("fill:%1").arg(idsTag(ids_));   // 换目标即阻断
}

bool SetFillCommand::mergeWith(const ur::Command* next)
{
    const auto* n = dynamic_cast<const SetFillCommand*>(next);
    if (!n || n->ids_ != ids_)
        return false;
    to_ = n->to_;                           // 只吸收终态色
    return true;
}

qint64 SetFillCommand::estimatedMemory() const
{
    return qint64(sizeof(*this)) + qint64(ids_.size() + from_.size()) * 16;
}

ur::ContextSet SetFillCommand::contexts() const
{
    return {ur::kCtxInspector};             // 属性通道（context 过滤演示主角）
}

QJsonObject SetFillCommand::toPayload() const
{
    QJsonObject o;
    QJsonArray arr;
    for (ShapeId id : ids_)
        arr.append(double(id));
    o.insert(QStringLiteral("ids"), arr);
    o.insert(QStringLiteral("to"), to_.name());
    return o;
}

// ════════════════════════════════════════════════════════════════════════
// SetStrokeWidthCommand
// ════════════════════════════════════════════════════════════════════════
SetStrokeWidthCommand::SetStrokeWidthCommand(CanvasScene* scene, QList<ShapeId> ids,
                                             QHash<ShapeId, qreal> from, qreal to)
    : scene_(scene), ids_(std::move(ids)), from_(std::move(from)), to_(to)
{
}

void SetStrokeWidthCommand::apply(qreal w)
{
    if (!scene_)
        return;
    for (ShapeId id : ids_) {
        ShapeMeta m;
        if (!scene_->getMeta(id, &m))
            continue;
        m.strokeWidth = w;
        scene_->setShapeMeta(id, m);
    }
}

void SetStrokeWidthCommand::applyMap(const QHash<ShapeId, qreal>& map)
{
    if (!scene_)
        return;
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        ShapeMeta m;
        if (!scene_->getMeta(it.key(), &m))
            continue;
        m.strokeWidth = it.value();
        scene_->setShapeMeta(it.key(), m);
    }
}

void SetStrokeWidthCommand::redo() { apply(to_); }
void SetStrokeWidthCommand::undo() { applyMap(from_); }

QString SetStrokeWidthCommand::text() const
{
    return ids_.size() == 1 ? QStringLiteral("修改描边宽度")
                            : QStringLiteral("修改 %1 个图形的描边宽度").arg(ids_.size());
}

QString SetStrokeWidthCommand::mergeTag() const
{
    return QStringLiteral("stroke:%1").arg(idsTag(ids_));
}

bool SetStrokeWidthCommand::mergeWith(const ur::Command* next)
{
    const auto* n = dynamic_cast<const SetStrokeWidthCommand*>(next);
    if (!n || n->ids_ != ids_)
        return false;
    to_ = n->to_;
    return true;
}

qint64 SetStrokeWidthCommand::estimatedMemory() const
{
    return qint64(sizeof(*this)) + qint64(ids_.size() + from_.size()) * 16;
}

ur::ContextSet SetStrokeWidthCommand::contexts() const
{
    return {ur::kCtxInspector};
}

} // namespace cd
