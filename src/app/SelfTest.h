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

namespace ur { class CommandStack; }

namespace cd {

class CanvasScene;

// 把 demo 的可序列化命令注册进 ur::CommandFactory（main 与 selftest 共用）
void registerDemoPayloads(CanvasScene* scene);

// 无头电池入口：返回失败断言数（0 = 全绿）
int runCanvasSelfTest();

} // namespace cd
