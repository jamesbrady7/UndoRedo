#pragma once
// ════════════════════════════════════════════════════════════════════════
// Command —— 命令协议（一切可撤销操作的契约）
//
// 与 QUndoCommand 的协议差异（设计精讲 §4 有完整走读）：
//  · mergeTag()：合并许可从"id 相同"细化为"id 相同且 tag 相同且 tag 非空"。
//    tag 是极性/目标阻断键——多选集合变了、滑条换了目标对象，tag 就变，
//    合并自动被阻断（Zed #38130"插入与删除被破坏性合并"的教训制度化）。
//  · estimatedMemory()：字节预算截断的输入（Krita/Blender 经验：对大载荷
//    命令只按条数限额不够）。
//  · contexts()：Eclipse 式多标签（见 Context.h）。
//  · payloadType()/toPayload()：序列化就绪——命令=类型名+参数包。payload
//    对 core 是【不透明文本】（约定为 JSON 文本，但 core 不认识 JSON，
//    格式知识在上层）：经 CommandFactory 按类型名重建回放（Qt/OCAF/
//    Blender 都缺这个能力）。
//  · selBefore/selAfter：选择集快照，由栈在 push 时自动盖章（若配置了
//    SelectionProvider），undo/redo 时恢复。命令作者通常无需关心。
//  · actorId/logicalTick：协同预留字段（只留字段，本框架不实现协同）。
//
// 幂等性纪律（Command 模式最大的坑）：redo() 必须"设置绝对终态"而不是
// "施加相对增量"，即 redo() 重放 N 次效果与 1 次相同——push 即执行 +
// setIndex 跳转 + undoIn 重放都依赖这条。单测 T1/T11 专门校验。
//
// 文本编码约定：text()/mergeTag()/payload 一律 UTF-8 std::string。
// Qt 侧消费用 QString::fromStdString，生产用 QStringLiteral(...).toStdString()
// ——边界只允许这两个函数，禁止 fromLocal8Bit/toLocal8Bit 混入。
// ════════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <string>
#include "core/Context.h"
#include "core/SelectionMemento.h"

namespace ur {

// 框架保留的命令类型 id；业务命令请从 100 起编号
enum FrameworkCommandId : int {
    kCmdInvalid  = -1,  // "不可合并"哨兵（与 QUndoCommand 默认 id 语义一致）
    kCmdCompound = 0,   // CompoundCommand 专用：合并策略恒拒
};

class Command
{
public:
    virtual ~Command() = default;

    // ── 核心协议 ────────────────────────────────────────────────
    virtual void redo() = 0;            // push 时由栈调用（push 即执行）
    virtual void undo() = 0;
    virtual int  id() const = 0;        // 类型 id：合并/策略分派的键
    virtual std::string text() const = 0;  // undo/redo action 与历史面板文本（UTF-8）

    // ── 合并协议 ────────────────────────────────────────────────
    // 合并许可：空 tag = 不参与合并（比 Qt"id=-1 才不合并"更保守的默认值）
    virtual std::string mergeTag() const { return std::string(); }
    // 把 next（更新的命令）的效果折进 this；成功返回 true，栈随即丢弃 next。
    // 约定：只吸收 next 的"终态"，不改动 this 的"初态"——这样 undo 一步
    // 回到整段连续操作的起点（拖拽合并的关键语义）。
    virtual bool mergeWith(const Command* next) { (void)next; return false; }

    // ── 内存预算 ────────────────────────────────────────────────
    virtual int64_t estimatedMemory() const { return int64_t(sizeof(*this)); }

    // ── 上下文标签 ──────────────────────────────────────────────
    virtual ContextSet contexts() const { return ContextSet(); }

    // ── 序列化就绪（payload 对 core 不透明，约定 JSON 文本）──────
    virtual std::string payloadType() const { return std::string(); }  // 空 = 不可序列化
    virtual std::string toPayload() const { return std::string(); }

    // ── 选择集快照（栈负责盖章与恢复时机，见 CommandStack）────────
    SelectionMemento selBefore;
    SelectionMemento selAfter;

    // ── 协同预留（字段级，未实现协同逻辑）─────────────────────────
    uint64_t actorId = 0;
    int64_t  logicalTick = 0;
};

} // namespace ur
