#pragma once
// ════════════════════════════════════════════════════════════════════════
// EditCommands —— 增/删图形（对象生命周期类命令）
//
// 这一对命令集中演示三条纪律：
//  1. 目标安全：只存 ShapeId + ShapeMeta 快照，执行前 findShape，
//     找不到按幂等规则 no-op —— 悬垂指针在结构上不可能
//  2. id 复用重建：RemoveShapes 的 undo 走 createShapeWithId 复用原 id，
//     历史里更早的命令（改色/移动）重放时仍按 id 找得到对象
//  3. 幂等：AddShape::redo 发现 id 已存在 → no-op（undoIn 重放 /
//     setIndex 跳转都可能重复执行 redo，绝不能重复建对象）
//
// AddShape 还是 payload 序列化示例之一（toPayload/fromPayload 出入口是
// 不透明 JSON 文本；CommandFactory 注册见 SelfTest.cpp::registerDemoPayloads）。
// ════════════════════════════════════════════════════════════════════════
#include <QList>
#include <QVector>
#include <cstdint>
#include <memory>
#include <string>
#include "canvas/ShapeMeta.h"
#include "commands/CommandIds.h"
#include "core/Command.h"

namespace cd {

class CanvasScene;

class AddShapeCommand : public ur::Command
{
public:
    // fixedId=0 → redo 时分配新 id；非 0 → 按指定 id 重建（payload 回放用）
    AddShapeCommand(CanvasScene* scene, const ShapeMeta& meta, ShapeId fixedId = 0);

    void redo() override;
    void undo() override;
    int  id() const override { return kCdAddShape; }
    std::string text() const override;
    int64_t estimatedMemory() const override;
    ur::ContextSet contexts() const override;

    std::string payloadType() const override { return "cd.addshape"; }
    std::string toPayload() const override;         // JSON 文本（demo 层格式知识）
    // data 解析失败返回 nullptr（Creator 契约）
    static std::unique_ptr<AddShapeCommand> fromPayload(CanvasScene* scene,
                                                        const std::string& data);

    ShapeId shapeId() const { return id_; }

private:
    CanvasScene* scene_;
    ShapeMeta meta_;
    ShapeId id_;
};

class RemoveShapesCommand : public ur::Command
{
public:
    RemoveShapesCommand(CanvasScene* scene, const QList<ShapeId>& ids);

    void redo() override;               // 首次执行捕获快照，其后按快照删除
    void undo() override;               // 按快照 + 原 id 重建
    int  id() const override { return kCdRemoveShape; }
    std::string text() const override;
    int64_t estimatedMemory() const override;
    ur::ContextSet contexts() const override;

private:
    CanvasScene* scene_;
    QList<ShapeId> ids_;
    QVector<QPair<ShapeId, ShapeMeta>> saved_;  // 首次 redo 捕获（含 z 序）
    bool captured_ = false;
};

} // namespace cd
