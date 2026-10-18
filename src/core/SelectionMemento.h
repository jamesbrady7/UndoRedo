#pragma once
// ════════════════════════════════════════════════════════════════════════
// SelectionMemento —— 选择集快照（轻量 Memento）
//
// 画布类应用的体验刚需：撤销"删除"后，刚恢复的对象应当仍处于选中态。
// 惯例（Qt Blocks 示例/Inkscape）是选择集【不单独入栈】，而是作为每条
// 命令的附带状态一起恢复——否则每次点选都吃一条 undo，历史被污染。
//
// 快照只记对象 id（quint64），不记指针：与"命令不持裸指针目标"同一条
// 安全纪律（对象可能已被删除重建，id 才是身份，指针只是当下住处）。
// ════════════════════════════════════════════════════════════════════════
#include <QVector>
#include <QtGlobal>

namespace ur {

struct SelectionMemento {
    QVector<quint64> ids;   // 排序与否均可，比较按序列相等

    bool isEmpty() const { return ids.isEmpty(); }
    bool operator==(const SelectionMemento& other) const { return ids == other.ids; }
    bool operator!=(const SelectionMemento& other) const { return !(*this == other); }

    static SelectionMemento fromList(const QList<quint64>& list)
    {
        SelectionMemento m;
        m.ids.reserve(list.size());
        for (quint64 id : list)
            m.ids.push_back(id);
        return m;
    }
};

} // namespace ur
