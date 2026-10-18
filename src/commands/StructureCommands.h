#pragma once
// ════════════════════════════════════════════════════════════════════════
// StructureCommands —— z 序调整 / 成组 / 解组
//
// 共同模式："首次 redo 捕获初态，其后重放只应用已捕获的终态"——
// z 序命令尤其依赖这条：nextTopZ() 的结果取决于当时的场景状态，
// 重放时重新计算会得到不同的值（幂等被破坏），所以新 z 必须在
// 首次执行时冻结进命令。
//
// 成组/解组是"单命令原子性"的示范：一个 SetGroupCommand 承载整批
// id 的组变更，天然原子，不需要事务。需要多命令协作的场景（对齐/
// 分布）才动用 TransactionScope —— 两种手段的分工见设计精讲 §6。
// ════════════════════════════════════════════════════════════════════════
#include <QHash>
#include <QList>
#include <cstdint>
#include <string>
#include "canvas/ShapeMeta.h"
#include "commands/CommandIds.h"
#include "core/Command.h"

namespace cd {

class CanvasScene;

class ReorderZCommand : public ur::Command
{
public:
    ReorderZCommand(CanvasScene* scene, QList<ShapeId> ids, bool toFront);

    void redo() override;
    void undo() override;
    int  id() const override { return kCdReorderZ; }
    std::string text() const override;
    int64_t estimatedMemory() const override;
    ur::ContextSet contexts() const override;

private:
    void applyZ(const QHash<ShapeId, qreal>& zs);

    CanvasScene* scene_;
    QList<ShapeId> ids_;
    bool toFront_;
    QHash<ShapeId, qreal> oldZ_, newZ_;
    bool captured_ = false;
};

class SetGroupCommand : public ur::Command
{
public:
    // newGroupId=0 → 解组；非 0 → 把 ids 全部编入该组（组 id 由场景分配）
    SetGroupCommand(CanvasScene* scene, QList<ShapeId> ids, ShapeId newGroupId);

    void redo() override;
    void undo() override;
    int  id() const override { return kCdSetGroup; }
    std::string text() const override;
    int64_t estimatedMemory() const override;
    ur::ContextSet contexts() const override;

private:
    void applyGroup(const QHash<ShapeId, ShapeId>& groups);

    CanvasScene* scene_;
    QList<ShapeId> ids_;
    ShapeId newGroup_;
    QHash<ShapeId, ShapeId> oldGroups_;
    bool captured_ = false;
};

} // namespace cd
