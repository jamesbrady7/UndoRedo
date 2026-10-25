#include "core/MergePolicy.h"
#include "core/Command.h"

namespace ur {

bool MergePolicy::mergeCompatible(const Command* top, const Command* incoming)
{
    if (!top || !incoming)
        return false;
    // Compound 永不合并（宏是原子历史步；吸收别的命令会破坏事务语义）
    if (top->id() == kCmdCompound || incoming->id() == kCmdCompound)
        return false;
    // id 无效或不同类 → 拒绝
    if (top->id() < 0 || top->id() != incoming->id())
        return false;
    // mergeTag 是极性/目标阻断键：空 tag = 未声明合并许可 → 拒绝；
    // tag 不同（换了目标对象/手势会话）→ 拒绝
    const std::string t = top->mergeTag();
    const std::string n = incoming->mergeTag();
    return !t.empty() && t == n;
}

bool AdjacentIdMergePolicy::canMerge(const Command* top, const Command* incoming,
                                     int64_t lastPushMs, int64_t nowMs) const
{
    (void)lastPushMs;
    (void)nowMs;
    return mergeCompatible(top, incoming);
}

bool TimeWindowMergePolicy::canMerge(const Command* top, const Command* incoming,
                                     int64_t lastPushMs, int64_t nowMs) const
{
    if (!mergeCompatible(top, incoming))
        return false;
    // 命令自述"我的 tag 里已有会话边界"（手势号/笔画号）→ 时间窗不再参与：
    // 边界更硬的那个说了算。松手后 gestureId 递增、tag 变化，合并照样断开。
    if (incoming->mergeTagHasSession())
        return true;
    if (windowMs_ <= 0)
        return false;   // 窗口<=0 视为关闭时间维度 → 退化为不合并
    return (nowMs - lastPushMs) <= windowMs_;
}

} // namespace ur
