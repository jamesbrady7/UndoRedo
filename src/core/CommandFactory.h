#pragma once
// ════════════════════════════════════════════════════════════════════════
// CommandFactory —— 按类型名重建命令（序列化就绪的地基）
//
// 命令 = 类型名 + 参数包（payload）。注册 creator 后即可：
//   toPayloadDoc(cmd) → 存档/日志/网络 → fromPayload(p, ctx) → 回放。
// 这是 Qt/OCAF/Blender 都缺的能力（跨会话 undo、崩溃恢复、审计日志、
// 脚本批处理的前置条件）。demo 只 JSON 化 AddShape/SetColor 两例证明
// 通路，不做全栈持久化（见设计精讲 §10 的取舍说明）。
//
// 分层纪律：payload 对 core 是【不透明文本】（Payload{type,data} 两个
// std::string）。core 不认识 JSON——"data 里装什么、外层文档怎么拼"
// 是上层（demo/业务）的知识。这样 core 保持零依赖，上层想换 JSON/
// protobuf/自定义二进制都不用动这里。
//
// ★ 为什么 creator 要收一个 CommandContext&（而不是靠 lambda 捕获）
//   旧签名是 `(const std::string&) -> Command`，于是"重建命令需要场景/
//   文档指针"只能靠捕获：`registerType("x", [scene](...){...})`。
//   捕获把注册表（全局、长命）和瞬时对象（窗口/局部变量）焊在一起——
//   对象销毁后 creator 里就是一个悬垂引用，而注册表完全不知情。
//   改成显式注入上下文后，creator 只认类型、不认实例：
//     registerType("cd.addshape", [](data, ctx) {
//         auto* c = dynamic_cast<CanvasCtx*>(&ctx);
//         return c ? AddShapeCommand::fromPayload(c->scene, data) : nullptr;
//     });
//   上下文不对就返回 nullptr（优雅降级，与"未注册类型"同一条路）。
//
// ★ 测试请自建 CommandFactory 实例，别用 instance()：全局注册表跨用例
//   累积状态，会让"这个类型没注册"之类的断言悄悄失真。
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

// 重建上下文的基类：creator 需要的外部依赖（场景/文档/工程…）挂在派生类上。
// core 只负责原样透传，具体类型由 creator 用 dynamic_cast 认领。
// 生命周期由调用方保证：ctx 必须活过这一次 fromPayload 调用即可，
// 注册表不保存它（这正是它取代"捕获"的意义）。
struct CommandContext {
    virtual ~CommandContext() = default;
};

class CommandFactory
{
public:
    // creator 收 data 文本 + 上下文；解析失败/上下文不匹配应返回 nullptr。
    using Creator = std::function<std::unique_ptr<Command>(const std::string& data,
                                                           CommandContext& ctx)>;

    // 全局便捷入口（demo 用）。测试请自建实例。
    static CommandFactory& instance();

    void registerType(const std::string& type, Creator creator);   // 同名覆盖
    bool unregisterType(const std::string& type);                  // 测试清理用
    void clear();                                                  // 清空注册表
    bool contains(const std::string& type) const;
    std::vector<std::string> types() const;   // 字典序排序（确定性输出）

    // p.type 为空或未注册 → 返回 nullptr（不抛）。
    std::unique_ptr<Command> fromPayload(const Payload& p, CommandContext& ctx) const;
    // 不可序列化（payloadType 空）→ 返回 Payload 且 type 为空。
    static Payload toPayloadDoc(const Command& cmd);

private:
    std::unordered_map<std::string, Creator> creators_;
};

} // namespace ur
