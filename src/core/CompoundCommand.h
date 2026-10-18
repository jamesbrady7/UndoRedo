#pragma once
// ════════════════════════════════════════════════════════════════════════
// CompoundCommand —— 复合命令（宏的产物）
//
// TransactionScope::commit / endMacro 把缓冲区内 ≥2 条子命令包成一条：
//   redo = 子命令正序重放；undo = 子命令逆序回滚。
// 对栈呈现为单个历史条目（GIMP undo_group / Qt beginMacro 同款语义），
// 但比 Qt 强的两点：
//   · contexts() 穿透聚合子命令标签 → undoIn(ctx) 能"看见"宏内部
//   · estimatedMemory() 聚合子命令字节 → 字节预算对宏同样生效
// id 恒为 kCmdCompound，合并策略恒拒（宏是原子步，不参与合并）。
// ════════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "core/Command.h"

namespace ur {

class CompoundCommand : public Command
{
public:
    CompoundCommand(std::string text, std::vector<std::unique_ptr<Command>> children);

    void redo() override;               // 正序重放
    void undo() override;               // 逆序回滚
    int  id() const override { return kCmdCompound; }
    std::string text() const override { return text_; }
    int64_t estimatedMemory() const override;
    ContextSet contexts() const override;   // 子命令标签的并集

    int childCount() const { return int(children_.size()); }
    const Command* childAt(int i) const;

private:
    std::string text_;
    std::vector<std::unique_ptr<Command>> children_;
};

} // namespace ur
