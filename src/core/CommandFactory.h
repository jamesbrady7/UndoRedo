#pragma once
// ════════════════════════════════════════════════════════════════════════
// CommandFactory —— 按类型名重建命令（序列化就绪的地基）
//
// 命令 = 类型名 + 参数包（payload）。注册 creator 后即可：
//   toPayloadDoc(cmd) → 存档/日志/网络 → fromPayload(p) → 回放。
// 这是 Qt/OCAF/Blender 都缺的能力（跨会话 undo、崩溃恢复、审计日志、
// 脚本批处理的前置条件）。demo 只 JSON 化 AddShape/SetColor 两例证明
// 通路，不做全栈持久化（见设计精讲 §10 的取舍说明）。
//
// 分层纪律：payload 对 core 是【不透明文本】（Payload{type,data} 两个
// std::string）。core 不认识 JSON——"data 里装什么、外层文档怎么拼"
// 是上层（demo/业务）的知识。这样 core 保持零依赖，上层想换 JSON/
// protobuf/自定义二进制都不用动这里。
// ════════════════════════════════════════════════════════════════════════
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "core/Command.h"

namespace ur {

// 信封的类型安全对：type=注册名，data=命令自述的参数包文本。
// 约定 data 为 JSON 文本（上层负责解析），core 只透传。
struct Payload {
    std::string type;   // 空 = 无效（toPayloadDoc 对不可序列化命令返回空 type）
    std::string data;
};

class CommandFactory
{
public:
    // creator 收 data 文本，重建命令；解析失败应返回 nullptr。
    using Creator = std::function<std::unique_ptr<Command>(const std::string& data)>;

    static CommandFactory& instance();

    void registerType(const std::string& type, Creator creator);
    bool contains(const std::string& type) const;
    std::vector<std::string> types() const;   // 字典序排序（确定性输出）

    // p.type 为空或未注册 → 返回 nullptr（不抛）。
    std::unique_ptr<Command> fromPayload(const Payload& p) const;
    // 不可序列化（payloadType 空）→ 返回 Payload 且 type 为空。
    static Payload toPayloadDoc(const Command& cmd);

private:
    CommandFactory() = default;
    std::unordered_map<std::string, Creator> creators_;
};

} // namespace ur
