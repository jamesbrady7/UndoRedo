#pragma once
// ════════════════════════════════════════════════════════════════════════
// SelfTest —— offscreen 无头电池（demo 层集成回归）
//
// 与 unit_tests 的分工：unit_tests 用测试替身验框架契约（core 层），
// 这里用【真实 MainWindow + 真实命令】验集成接线：选择快照、时间窗
// 合并、事务、context 撤销、目标安全、payload 往返在真实域对象上
// 全链路跑通。退出码 = 失败数（同 NavTreeGroup 约定）。
// 运行：undoredo_demo.exe --selftest（launcher 注入 QT_QPA_PLATFORM=offscreen）
// ════════════════════════════════════════════════════════════════════════
#include "canvas/ShapeMeta.h"
#include "core/CommandFactory.h"

namespace ur { class CommandStack; }

namespace cd {

class CanvasScene;

// demo 侧的重建上下文：creator 需要的外部世界（这里是画布）挂在它上面。
// core 只原样透传 CommandContext&，demo 用 dynamic_cast 认领——注册表因此
// 不再需要捕获任何实例，可以在"场景还不存在"时就把类型注册好。
class CanvasCommandContext : public ur::CommandContext
{
public:
    explicit CanvasCommandContext(CanvasScene* s) : scene(s) {}
    CanvasScene* scene;                 // 不持有；只需活过 fromPayload 调用
};

// 把 demo 的可序列化命令注册进 ur::CommandFactory（main 与 selftest 共用）。
// 不接收 scene：注册的是"类型 → 怎么从 payload 重建"，重建时的场景由
// fromPayload 的 ctx 参数给（A1 修正的正是"把注册表焊死在瞬时对象上"）。
void registerDemoPayloads();

// 无头电池入口：返回失败断言数（0 = 全绿）
int runCanvasSelfTest();

} // namespace cd
