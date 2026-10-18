#include "core/CommandFactory.h"

#include <algorithm>

namespace ur {

CommandFactory& CommandFactory::instance()
{
    static CommandFactory f;
    return f;
}

void CommandFactory::registerType(const std::string& type, Creator creator)
{
    if (!type.empty() && creator)
        creators_[type] = std::move(creator);
}

bool CommandFactory::contains(const std::string& type) const
{
    return creators_.find(type) != creators_.end();
}

std::vector<std::string> CommandFactory::types() const
{
    std::vector<std::string> list;
    list.reserve(creators_.size());
    for (const auto& kv : creators_)
        list.push_back(kv.first);
    std::sort(list.begin(), list.end());   // 确定性输出（unordered 遍历序不定）
    return list;
}

std::unique_ptr<Command> CommandFactory::fromPayload(const Payload& p) const
{
    if (p.type.empty())
        return nullptr;
    auto it = creators_.find(p.type);
    if (it == creators_.end())
        return nullptr;
    return it->second(p.data);
}

Payload CommandFactory::toPayloadDoc(const Command& cmd)
{
    Payload p;
    p.type = cmd.payloadType();
    if (p.type.empty())
        return p;                       // 未声明 payloadType = 不可序列化（type 空即失败）
    p.data = cmd.toPayload();
    return p;
}

} // namespace ur
