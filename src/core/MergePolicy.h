#pragma once
// ════════════════════════════════════════════════════════════════════════
// MergePolicy —— 可插拔合并策略
//
// QUndoStack 把合并规则写死在命令里（id 相同 + mergeWith 成功，且只试
// 栈顶相邻一对）。本框架把"能不能合并"抽成栈级策略对象，命令只负责
// 提供判据（id + mergeTag）与"怎么合并"（mergeWith）：
//
//   canMerge(top, incoming, lastPushMs, nowMs)  ← 栈在 push 时询问策略
//        └ true → 栈再调 top->mergeWith(incoming)，成功则丢弃 incoming
//
// 内置两个实现：
//   AdjacentIdMergePolicy   同 id + 同非空 mergeTag（≈Qt 语义但 tag 阻断更细）
//   TimeWindowMergePolicy   在其上加时间窗（业界共识：CodeMirror ~50ms、
//                           Syncfusion 300ms、Quill 1000ms；默认 500ms）
//
// Krita 的 cumulative undo 证明还可以做"与最后 N 条深度合并"——接口已
// 预留（策略拿到完整判据即可扩展），demo 不实现以控规模。
// ════════════════════════════════════════════════════════════════════════
#include <cstdint>

namespace ur {

class Command;

class MergePolicy
{
public:
    virtual ~MergePolicy() = default;

    // top=栈顶已执行命令，incoming=本次 push 的新命令，
    // lastPushMs=栈顶条目的压栈时间戳，nowMs=当前时刻（均来自可注入时钟）。
    virtual bool canMerge(const Command* top, const Command* incoming,
                          int64_t lastPushMs, int64_t nowMs) const = 0;

protected:
    // 共享的静态判据：id 有效且相等、mergeTag 非空且相等、非 Compound。
    // （tag 非空是"合并许可"：不声明 tag 的命令类型默认不参与合并）
    static bool mergeCompatible(const Command* top, const Command* incoming);
};

class AdjacentIdMergePolicy : public MergePolicy
{
public:
    bool canMerge(const Command* top, const Command* incoming,
                  int64_t lastPushMs, int64_t nowMs) const override;
};

class TimeWindowMergePolicy : public MergePolicy
{
public:
    explicit TimeWindowMergePolicy(int64_t windowMs = 500) : windowMs_(windowMs) {}
    void setWindowMs(int64_t ms) { windowMs_ = ms; }
    int64_t windowMs() const { return windowMs_; }

    bool canMerge(const Command* top, const Command* incoming,
                  int64_t lastPushMs, int64_t nowMs) const override;

private:
    int64_t windowMs_;
};

} // namespace ur
