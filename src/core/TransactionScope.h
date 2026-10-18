#pragma once
// ════════════════════════════════════════════════════════════════════════
// TransactionScope —— RAII 事务作用域（本框架对 QUndoStack 的第一记重拳）
//
// Qt 的 beginMacro/endMacro 必须手工配对：中途 return、异常、任何一条
// 提前退出的分支都会泄漏宏（此后所有 push 全被吸进一个永不结算的宏，
// 栈"失灵"且极难排查）。FreeCAD 的 openTransaction/commitTransaction
// 同样有此病灶（issue #7580），后来补了 AutoTransaction——就是本类。
//
// 语义（对标数据库事务）：
//   { TransactionScope tx(&stack, "对齐");   // 开事务
//     stack.push(...); stack.push(...);     // 子命令入缓冲区
//     tx.commit(); }                        // 析构结算 → 一条 Compound 历史
//
//   · 未 commit 就析构（提前 return / 异常栈展开）→ abort：
//     对已执行的子命令【逆序 undo()】回滚后整体丢弃，净效果为零
//   · 空事务（scope 内无净 push）→ 自动丢弃，不占栈位不发信号
//     （FreeCAD "隐身事务" UI 困惑的反向教训：明确定义并一致执行）
//   · 嵌套安全：内层 scope 只回滚自己 mark 之后的部分，结算归最外层
//
// 不可拷贝、不可移动——作用域对象就该钉死在一个词法作用域里。
// ════════════════════════════════════════════════════════════════════════
#include <QString>
#include "core/CommandStack.h"

namespace ur {

class TransactionScope
{
public:
    TransactionScope(CommandStack* stack, const QString& text)
        : stack_(stack)
    {
        if (!stack_)
            return;
        stack_->beginMacro(text);
        mark_ = stack_->macroMark();    // 嵌套回滚水位：只对本层之后的子命令负责
        active_ = true;
    }

    ~TransactionScope()
    {
        if (!active_)
            return;                     // stack_ 为空的防御分支
        if (!committed_)
            stack_->rollbackMacroTo(mark_); // abort：逆序回滚本层缓冲
        stack_->endMacro();             // 最外层在此结算（Compound/直入/丢弃）
    }

    void commit() { committed_ = true; }
    bool isCommitted() const { return committed_; }

    TransactionScope(const TransactionScope&) = delete;
    TransactionScope& operator=(const TransactionScope&) = delete;

private:
    CommandStack* stack_ = nullptr;
    int  mark_ = 0;
    bool committed_ = false;
    bool active_ = false;
};

} // namespace ur
