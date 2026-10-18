#include "core/CommandFactory.h"

namespace ur {

CommandFactory& CommandFactory::instance()
{
    static CommandFactory f;
    return f;
}

void CommandFactory::registerType(const QString& type, Creator creator)
{
    if (!type.isEmpty() && creator)
        creators_.insert(type, std::move(creator));
}

bool CommandFactory::contains(const QString& type) const
{
    return creators_.contains(type);
}

QStringList CommandFactory::types() const
{
    QStringList list;
    list.reserve(creators_.size());
    for (auto it = creators_.cbegin(); it != creators_.cend(); ++it)
        list.push_back(it.key());
    list.sort();
    return list;
}

std::unique_ptr<Command> CommandFactory::fromPayload(const QJsonObject& doc) const
{
    const QString type = doc.value(QStringLiteral("type")).toString();
    auto it = creators_.constFind(type);
    if (it == creators_.constEnd())
        return nullptr;
    return it.value()(doc.value(QStringLiteral("data")).toObject());
}

QJsonObject CommandFactory::toPayloadDoc(const Command& cmd)
{
    const QString type = cmd.payloadType();
    if (type.isEmpty())
        return QJsonObject();           // 未声明 payloadType = 不可序列化
    QJsonObject doc;
    doc.insert(QStringLiteral("type"), type);
    doc.insert(QStringLiteral("data"), cmd.toPayload());
    return doc;
}

} // namespace ur
