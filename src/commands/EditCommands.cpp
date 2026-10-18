#include "commands/EditCommands.h"

#include <QJsonDocument>
#include <QJsonObject>
#include "canvas/CanvasScene.h"
#include "core/Context.h"

namespace cd {

// ════════════════════════════════════════════════════════════════════════
// AddShapeCommand
// ════════════════════════════════════════════════════════════════════════
AddShapeCommand::AddShapeCommand(CanvasScene* scene, const ShapeMeta& meta, ShapeId fixedId)
    : scene_(scene), meta_(meta), id_(fixedId)
{
}

void AddShapeCommand::redo()
{
    if (!scene_)
        return;
    if (id_ != 0 && scene_->findShape(id_)) {   // 幂等：已存在（重放场景）
        scene_->selectIds({id_});
        return;
    }
    if (id_ == 0)
        id_ = scene_->createShape(meta_);
    else
        scene_->createShapeWithId(id_, meta_);
    scene_->selectIds({id_});                   // 新增后自然选中（provider 捕获进 selAfter）
}

void AddShapeCommand::undo()
{
    if (scene_ && id_ != 0)
        scene_->removeShape(id_);               // 已不在 → scene 内 no-op
}

std::string AddShapeCommand::text() const
{
    // 边界纪律：中文格式化在 demo 层用 QString 完成，出口统一 toStdString
    return QStringLiteral("添加%1").arg(shapeKindName(meta_.kind)).toStdString();
}

int64_t AddShapeCommand::estimatedMemory() const
{
    return int64_t(sizeof(*this)) + 128;        // meta 字符串/颜色的粗略摊销
}

ur::ContextSet AddShapeCommand::contexts() const
{
    return {ur::kCtxCanvas};
}

// payload 对 core 是不透明文本；"装 JSON"是 demo 层的格式知识——
// 组装用 QJsonObject（复用 ShapeMeta::toJson），出口 Compact 序列化。
std::string AddShapeCommand::toPayload() const
{
    QJsonObject o = meta_.toJson();
    o.insert(QStringLiteral("id"), double(id_));
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)).toStdString();
}

std::unique_ptr<AddShapeCommand> AddShapeCommand::fromPayload(CanvasScene* scene,
                                                              const std::string& data)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(data), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return nullptr;                         // 解析失败 → nullptr（Creator 契约）
    const QJsonObject obj = doc.object();
    ShapeMeta m = ShapeMeta::fromJson(obj);
    const ShapeId id = ShapeId(obj.value(QStringLiteral("id")).toDouble(0.0));
    return std::make_unique<AddShapeCommand>(scene, m, id);
}

// ════════════════════════════════════════════════════════════════════════
// RemoveShapesCommand
// ════════════════════════════════════════════════════════════════════════
RemoveShapesCommand::RemoveShapesCommand(CanvasScene* scene, const QList<ShapeId>& ids)
    : scene_(scene), ids_(ids)
{
}

void RemoveShapesCommand::redo()
{
    if (!scene_)
        return;
    if (!captured_) {                           // 初态只捕获一次（幂等纪律）
        saved_.clear();
        for (ShapeId id : ids_) {
            ShapeMeta m;
            if (scene_->getMeta(id, &m))
                saved_.append({id, m});         // 已不存在的 id 静默跳过
        }
        captured_ = true;
    }
    for (const auto& pair : saved_)
        scene_->removeShape(pair.first);        // 重复 redo → removeShape no-op
}

void RemoveShapesCommand::undo()
{
    if (!scene_)
        return;
    QList<ShapeId> restored;
    restored.reserve(saved_.size());
    for (const auto& pair : saved_) {
        if (scene_->createShapeWithId(pair.first, pair.second))
            restored.push_back(pair.first);     // 复用原 id → 老命令重放仍能找到目标
    }
    scene_->selectIds(restored);                // 恢复后自然选中
}

std::string RemoveShapesCommand::text() const
{
    const int n = (std::max)(1, int(saved_.size()));
    return n == 1 ? std::string("删除图形")
                  : QStringLiteral("删除 %1 个图形").arg(n).toStdString();
}

int64_t RemoveShapesCommand::estimatedMemory() const
{
    return int64_t(sizeof(*this)) + int64_t((std::max)(1, int(saved_.size()))) * 128;
}

ur::ContextSet RemoveShapesCommand::contexts() const
{
    return {ur::kCtxCanvas};
}

} // namespace cd
