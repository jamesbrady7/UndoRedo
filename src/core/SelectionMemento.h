#pragma once
// ════════════════════════════════════════════════════════════════════════
// SelectionMemento —— 选择集快照（轻量 Memento）
//
// 画布类应用的体验刚需：撤销"删除"后，刚恢复的对象应当仍处于选中态。
// 惯例（Qt Blocks 示例/Inkscape）是选择集【不单独入栈】，而是作为每条
// 命令的附带状态一起恢复——否则每次点选都吃一条 undo，历史被污染。
//
// 快照只记对象 id（uint64_t），不记指针：与"命令不持裸指针目标"同一条
// 安全纪律（对象可能已被删除重建，id 才是身份，指针只是当下住处）。
// ════════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <vector>

namespace ur {

struct SelectionMemento {
    std::vector<uint64_t> ids;   // 排序与否均可，比较按序列相等

    bool isEmpty() const { return ids.empty(); }
    bool operator==(const SelectionMemento& other) const { return ids == other.ids; }
    bool operator!=(const SelectionMemento& other) const { return !(*this == other); }

    // 从任意可迭代序列构造（std::vector/QList/初始化列表…都吃得下，
    // 元素须可转 uint64_t）——core 不知道调用方用什么容器。
    template <class Seq>
    static SelectionMemento fromSeq(const Seq& list)
    {
        SelectionMemento m;
        m.ids.reserve(list.size());
        for (const auto& id : list)
            m.ids.push_back(uint64_t(id));
        return m;
    }
};

} // namespace ur
