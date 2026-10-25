#pragma once
// ════════════════════════════════════════════════════════════════════════
// GeometryCommands —— 移动/缩放（连续手势类命令，时间窗合并的主战场）
//
// 拖拽期间 CanvasView 每个 mouseMove 都 push 一条命令（push 即执行 =
// 天然的实时反馈，不需要"先改视图再补命令"的旁路）。TimeWindow 策略把
// 窗口内的连续 push 折叠成一条历史：
//   · mergeTag = "move:{排序id集}:{手势号}" —— 换目标、松手再拖（手势号
//     递增）都会阻断合并；停顿超过窗口也自然分段（Krita 笔触同理）
//   · mergeWith 只吸收 next 的终态 to_，不动自身 from_ → undo 一步
//     回到整段拖拽的起点
// 位置/尺寸都是【绝对终态】赋值 → redo 幂等，重放安全。
// ════════════════════════════════════════════════════════════════════════
#include <QHash>
#include <QPointF>
#include <QRectF>
#include <cstdint>
#include <string>
#include "canvas/ShapeMeta.h"
#include "commands/CommandIds.h"
#include "core/Command.h"

namespace cd {

class CanvasScene;

// 目标集合相等（顺序无关）：合并前必须确认"两条命令作用在同一批 id 上"。
// 用 size + 逐 key 查表，不构造 QList（旧版 `a.keys() != b.keys()` 每次都
// 分配两个列表，而且比较结果依赖 QHash 迭代序——同一批 key 因插入顺序不同
// 就可能判不等，合并时灵时不灵）。
bool sameTargetSet(const QHash<ShapeId, QPointF>& a, const QHash<ShapeId, QPointF>& b);

class MoveShapesCommand : public ur::Command
{
public:
    // gestureId：同一次按下-拖拽-松开为一个手势号；<=0 表示非手势（对齐等）
    MoveShapesCommand(CanvasScene* scene,
                      QHash<ShapeId, QPointF> from, QHash<ShapeId, QPointF> to,
                      int64_t gestureId = 0);

    void redo() override;
    void undo() override;
    int  id() const override { return kCdMove; }
    std::string text() const override;
    std::string mergeTag() const override;
    // 手势号 >0 是【显式会话边界】：一次拖拽内部的 push 无论间隔多久都必须
    // 合并（用户拖着不放 ≠ 两次操作），时间窗不该在这里插嘴（T17）。
    bool mergeTagHasSession() const override { return gestureId_ > 0; }
    bool mergeWith(const ur::Command* next) override;
    int64_t estimatedMemory() const override;
    ur::ContextSet contexts() const override;

private:
    void applyPositions(const QHash<ShapeId, QPointF>& map);

    CanvasScene* scene_;
    QHash<ShapeId, QPointF> from_, to_;
    int64_t gestureId_;
};

class ResizeShapeCommand : public ur::Command
{
public:
    ResizeShapeCommand(CanvasScene* scene, ShapeId id,
                       QRectF from, QRectF to, int64_t gestureId = 0);

    void redo() override;
    void undo() override;
    int  id() const override { return kCdResize; }
    std::string text() const override;
    std::string mergeTag() const override;
    bool mergeTagHasSession() const override { return gestureId_ > 0; }
    bool mergeWith(const ur::Command* next) override;
    int64_t estimatedMemory() const override;
    ur::ContextSet contexts() const override;

private:
    void applyRect(const QRectF& r);

    CanvasScene* scene_;
    ShapeId id_;
    QRectF from_, to_;
    int64_t gestureId_;
};

} // namespace cd
