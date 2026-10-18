#pragma once
// ════════════════════════════════════════════════════════════════════════
// PropertyCommands —— 属性面板类命令（inspector context + 时间窗合并）
//
// 滑条拖动期间每次 valueChanged 都 push 一条：from_ = 当下值、to_ = 新值。
// TimeWindow 策略把连续输入折叠成一条历史（mergeTag 锚定目标 id 集）。
// 全部打 "inspector" context —— HistoryDock 的「只撤销属性改动」按钮
// （undoIn）演示的就是这批命令的子序列撤销（Eclipse IUndoContext 语义）。
//
// SetFillCommand 是 payload 序列化示例之二（"绝对终态"式命令最适合
// 序列化：payload 只需 {id集, to 色}，from 在重建后首次 redo 时捕获）。
// ════════════════════════════════════════════════════════════════════════
#include <QColor>
#include <QHash>
#include <QList>
#include <cstdint>
#include <memory>
#include <string>
#include "canvas/ShapeMeta.h"
#include "commands/CommandIds.h"
#include "core/Command.h"

namespace cd {

class CanvasScene;

class SetFillCommand : public ur::Command
{
public:
    SetFillCommand(CanvasScene* scene, QList<ShapeId> ids,
                   QHash<ShapeId, QColor> from, QColor to);

    void redo() override;
    void undo() override;
    int  id() const override { return kCdSetFill; }
    std::string text() const override;
    std::string mergeTag() const override;
    bool mergeWith(const ur::Command* next) override;
    int64_t estimatedMemory() const override;
    ur::ContextSet contexts() const override;

    std::string payloadType() const override { return "cd.setfill"; }
    std::string toPayload() const override;     // JSON 文本（demo 层格式知识）

private:
    void apply(const QColor& c);
    void applyMap(const QHash<ShapeId, QColor>& map);

    CanvasScene* scene_;
    QList<ShapeId> ids_;
    QHash<ShapeId, QColor> from_;   // 首次 redo 前由调用方捕获
    QColor to_;
};

class SetStrokeWidthCommand : public ur::Command
{
public:
    SetStrokeWidthCommand(CanvasScene* scene, QList<ShapeId> ids,
                          QHash<ShapeId, qreal> from, qreal to);

    void redo() override;
    void undo() override;
    int  id() const override { return kCdSetStroke; }
    std::string text() const override;
    std::string mergeTag() const override;
    bool mergeWith(const ur::Command* next) override;
    int64_t estimatedMemory() const override;
    ur::ContextSet contexts() const override;

private:
    void apply(qreal w);
    void applyMap(const QHash<ShapeId, qreal>& map);

    CanvasScene* scene_;
    QList<ShapeId> ids_;
    QHash<ShapeId, qreal> from_;
    qreal to_;
};

} // namespace cd
