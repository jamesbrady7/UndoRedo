#include "commands/GeometryCommands.h"

#include "canvas/CanvasScene.h"
#include "core/Context.h"

namespace cd {

bool sameTargetSet(const QHash<ShapeId, QPointF>& a, const QHash<ShapeId, QPointF>& b)
{
    if (a.size() != b.size())
        return false;
    for (auto it = a.cbegin(); it != a.cend(); ++it)
        if (!b.contains(it.key()))
            return false;
    return true;
}

// ════════════════════════════════════════════════════════════════════════
// MoveShapesCommand
// ════════════════════════════════════════════════════════════════════════
MoveShapesCommand::MoveShapesCommand(CanvasScene* scene,
                                     QHash<ShapeId, QPointF> from,
                                     QHash<ShapeId, QPointF> to,
                                     int64_t gestureId)
    : scene_(scene), from_(std::move(from)), to_(std::move(to)), gestureId_(gestureId)
{
}

void MoveShapesCommand::applyPositions(const QHash<ShapeId, QPointF>& map)
{
    if (!scene_)
        return;
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        ShapeMeta m;
        if (!scene_->getMeta(it.key(), &m))
            continue;                       // 目标已删 → no-op（目标安全）
        m.pos = it.value();                 // 绝对终态赋值 → 幂等
        scene_->setShapeMeta(it.key(), m);
    }
}

void MoveShapesCommand::redo() { applyPositions(to_); }
void MoveShapesCommand::undo() { applyPositions(from_); }

std::string MoveShapesCommand::text() const
{
    return from_.size() == 1 ? std::string("移动图形")
                             : QStringLiteral("移动 %1 个图形").arg(from_.size()).toStdString();
}

std::string MoveShapesCommand::mergeTag() const
{
    // 手势号 >0 才发合并许可：对齐/分布等一次性操作（gestureId=0）不合并，
    // 它们由 TransactionScope 打包成 Compound（两种"原子化"手段各司其职）
    if (gestureId_ <= 0)
        return std::string();
    return "move:" + idsTag(from_.keys()) + ":" + std::to_string(gestureId_);
}

bool MoveShapesCommand::mergeWith(const ur::Command* next)
{
    const auto* n = dynamic_cast<const MoveShapesCommand*>(next);
    if (!n || !sameTargetSet(n->from_, from_))
        return false;                       // 目标集合变了 → 拒绝（双保险）
    to_ = n->to_;                           // 只吸收终态；from_ 保持整段起点
    return true;
}

int64_t MoveShapesCommand::estimatedMemory() const
{
    return int64_t(sizeof(*this)) + int64_t(from_.size() + to_.size()) * 24;
}

ur::ContextSet MoveShapesCommand::contexts() const
{
    return {ur::kCtxCanvas};
}

// ════════════════════════════════════════════════════════════════════════
// ResizeShapeCommand
// ════════════════════════════════════════════════════════════════════════
ResizeShapeCommand::ResizeShapeCommand(CanvasScene* scene, ShapeId id,
                                       QRectF from, QRectF to, int64_t gestureId)
    : scene_(scene), id_(id), from_(from), to_(to), gestureId_(gestureId)
{
}

void ResizeShapeCommand::applyRect(const QRectF& r)
{
    if (!scene_)
        return;
    ShapeMeta m;
    if (!scene_->getMeta(id_, &m))
        return;                             // 目标已删 → no-op
    m.pos = r.topLeft();
    m.size = r.size();
    scene_->setShapeMeta(id_, m);
}

void ResizeShapeCommand::redo() { applyRect(to_); }
void ResizeShapeCommand::undo() { applyRect(from_); }

std::string ResizeShapeCommand::text() const
{
    return "缩放图形";
}

std::string ResizeShapeCommand::mergeTag() const
{
    if (gestureId_ <= 0)
        return std::string();
    return "resize:" + std::to_string(id_) + ":" + std::to_string(gestureId_);
}

bool ResizeShapeCommand::mergeWith(const ur::Command* next)
{
    const auto* n = dynamic_cast<const ResizeShapeCommand*>(next);
    if (!n || n->id_ != id_)
        return false;
    to_ = n->to_;
    return true;
}

int64_t ResizeShapeCommand::estimatedMemory() const
{
    return int64_t(sizeof(*this));
}

ur::ContextSet ResizeShapeCommand::contexts() const
{
    return {ur::kCtxCanvas};
}

} // namespace cd
