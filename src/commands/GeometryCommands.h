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
#include "canvas/ShapeMeta.h"
#include "commands/CommandIds.h"
#include "core/Command.h"

namespace cd {

class CanvasScene;

class MoveShapesCommand : public ur::Command
{
public:
    // gestureId：同一次按下-拖拽-松开为一个手势号；<=0 表示非手势（对齐等）
    MoveShapesCommand(CanvasScene* scene,
                      QHash<ShapeId, QPointF> from, QHash<ShapeId, QPointF> to,
                      qint64 gestureId = 0);

    void redo() override;
    void undo() override;
    int  id() const override { return kCdMove; }
    QString text() const override;
    QString mergeTag() const override;
    bool mergeWith(const ur::Command* next) override;
    qint64 estimatedMemory() const override;
    ur::ContextSet contexts() const override;

private:
    void applyPositions(const QHash<ShapeId, QPointF>& map);

    CanvasScene* scene_;
    QHash<ShapeId, QPointF> from_, to_;
    qint64 gestureId_;
};

class ResizeShapeCommand : public ur::Command
{
public:
    ResizeShapeCommand(CanvasScene* scene, ShapeId id,
                       QRectF from, QRectF to, qint64 gestureId = 0);

    void redo() override;
    void undo() override;
    int  id() const override { return kCdResize; }
    QString text() const override;
    QString mergeTag() const override;
    bool mergeWith(const ur::Command* next) override;
    qint64 estimatedMemory() const override;
    ur::ContextSet contexts() const override;

private:
    void applyRect(const QRectF& r);

    CanvasScene* scene_;
    ShapeId id_;
    QRectF from_, to_;
    qint64 gestureId_;
};

} // namespace cd
