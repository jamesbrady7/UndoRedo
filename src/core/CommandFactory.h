#pragma once
// ════════════════════════════════════════════════════════════════════════
// CommandFactory —— 按类型名重建命令（序列化就绪的地基）
//
// 命令 = 类型名 + 参数包（payload）。注册 creator 后即可：
//   toPayloadDoc(cmd) → JSON 存档/日志/网络 → fromPayload(json) → 回放。
// 这是 Qt/OCAF/Blender 都缺的能力（跨会话 undo、崩溃恢复、审计日志、
// 脚本批处理的前置条件）。demo 只 JSON 化 AddShape/SetColor 两例证明
// 通路，不做全栈持久化（见设计精讲 §10 的取舍说明）。
// ════════════════════════════════════════════════════════════════════════
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include "core/Command.h"

namespace ur {

class CommandFactory
{
public:
    using Creator = std::function<std::unique_ptr<Command>(const QJsonObject& data)>;

    static CommandFactory& instance();

    void registerType(const QString& type, Creator creator);
    bool contains(const QString& type) const;
    QStringList types() const;

    // payload 文档格式：{ "type": <payloadType>, "data": <toPayload 结果> }
    std::unique_ptr<Command> fromPayload(const QJsonObject& doc) const;
    static QJsonObject toPayloadDoc(const Command& cmd);   // 不可序列化返回空对象

private:
    CommandFactory() = default;
    QHash<QString, Creator> creators_;
};

} // namespace ur
