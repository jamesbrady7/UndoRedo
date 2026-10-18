#include "commands/StructureCommands.h"

#include "canvas/CanvasScene.h"
#include "core/Context.h"

namespace cd {

// ════════════════════════════════════════════════════════════════════════
// ReorderZCommand
// ════════════════════════════════════════════════════════════════════════
ReorderZCommand::ReorderZCommand(CanvasScene* scene, QList<ShapeId> ids, bool toFront)
    : scene_(scene), ids_(std::move(ids)), toFront_(toFront)
{
}

void ReorderZCommand::applyZ(const QHash<ShapeId, qreal>& zs)
{
    if (!scene_)
        return;
    for (auto it = zs.cbegin(); it != zs.cend(); ++it) {
        ShapeMeta m;
        if (!scene_->getMeta(it.key(), &m))
            continue;                       // 目标已删 → no-op
        m.z = it.value();
        scene_->setShapeMeta(it.key(), m);
    }
}

void ReorderZCommand::redo()
{
    if (!scene_)
        return;
    if (!captured_) {
        // 首次执行：冻结初态与终态（nextTopZ 依赖当时场景，重放不可重算）
        oldZ_.clear();
        newZ_.clear();
        qreal base = toFront_ ? scene_->nextTopZ() : scene_->nextBottomZ();
        const qreal step = toFront_ ? 1.0 : -1.0;
        int i = 0;
        for (ShapeId id : ids_) {
            ShapeMeta m;
            if (!scene_->getMeta(id, &m))
                continue;
            oldZ_.insert(id, m.z);
            newZ_.insert(id, base + step * i);
            ++i;
        }
        captured_ = true;
    }
    applyZ(newZ_);
}

void ReorderZCommand::undo()
{
    applyZ(oldZ_);
}

std::string ReorderZCommand::text() const
{
    return toFront_ ? std::string("置于顶层") : std::string("置于底层");
}

int64_t ReorderZCommand::estimatedMemory() const
{
    return int64_t(sizeof(*this)) + int64_t(oldZ_.size() + newZ_.size()) * 16;
}

ur::ContextSet ReorderZCommand::contexts() const
{
    return {ur::kCtxCanvas};
}

// ════════════════════════════════════════════════════════════════════════
// SetGroupCommand
// ════════════════════════════════════════════════════════════════════════
SetGroupCommand::SetGroupCommand(CanvasScene* scene, QList<ShapeId> ids, ShapeId newGroupId)
    : scene_(scene), ids_(std::move(ids)), newGroup_(newGroupId)
{
}

void SetGroupCommand::applyGroup(const QHash<ShapeId, ShapeId>& groups)
{
    if (!scene_)
        return;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        ShapeMeta m;
        if (!scene_->getMeta(it.key(), &m))
            continue;
        m.groupId = it.value();
        scene_->setShapeMeta(it.key(), m);
    }
}

void SetGroupCommand::redo()
{
    if (!scene_)
        return;
    if (!captured_) {
        oldGroups_.clear();
        for (ShapeId id : ids_) {
            ShapeMeta m;
            if (scene_->getMeta(id, &m))
                oldGroups_.insert(id, m.groupId);
        }
        captured_ = true;
    }
    QHash<ShapeId, ShapeId> applied;
    for (auto it = oldGroups_.cbegin(); it != oldGroups_.cend(); ++it)
        applied.insert(it.key(), newGroup_);
    applyGroup(applied);
}

void SetGroupCommand::undo()
{
    applyGroup(oldGroups_);
}

std::string SetGroupCommand::text() const
{
    if (newGroup_ == 0)
        return ids_.size() == 1 ? std::string("解组")
                                : QStringLiteral("解组 %1 个图形").arg(ids_.size()).toStdString();
    return QStringLiteral("成组 %1 个图形").arg(ids_.size()).toStdString();
}

int64_t SetGroupCommand::estimatedMemory() const
{
    return int64_t(sizeof(*this)) + int64_t(ids_.size() + oldGroups_.size()) * 16;
}

ur::ContextSet SetGroupCommand::contexts() const
{
    return {ur::kCtxCanvas};
}

} // namespace cd
