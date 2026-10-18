#pragma once
// ════════════════════════════════════════════════════════════════════════
// StackBridge —— core Observer → Qt 信号的单向桥（demo/app 层专属）
//
// core 是纯 C++17 零依赖的（不继承 QObject、无 signals），Qt 侧的
// connect 生态由本类接回来：实现 ur::CommandStack::Observer，把两个
// 回调原样重发为 Qt 信号。UI 代码（MainWindow/HistoryDock）只连 bridge。
//
// ★ 生命周期硬约束（唯一可能 UAF 的地方，写死在结构上）：
//   · 构造 addObserver、析构 removeObserver——bridge 必须【先于】stack 析构
//   · 因此持有方（MainWindow）里 bridge_ 成员声明必须【紧跟在 stack_ 之后】
//     （成员按声明逆序析构 → bridge 先死、stack 后死，契约成立）
//   · 绝不给 bridge 挂 QObject parent：parent 化会让它推迟到 ~QObject
//     阶段才析构（晚于 unique_ptr 成员），踩已死的 stack
//   · 只做"回调→信号"单向转发，不转发 undo/redo 等命令 API（职责最小化）
// ════════════════════════════════════════════════════════════════════════
#include <QObject>
#include "core/CommandStack.h"

namespace ur {

class StackBridge : public QObject, public CommandStack::Observer
{
    Q_OBJECT
public:
    explicit StackBridge(CommandStack* stack, QObject* parent = nullptr);
    ~StackBridge() override;

signals:
    void indexChanged(int index);       // 语义与旧版 CommandStack 信号一致
    void cleanChanged(bool clean);

private:
    // Observer 回调 → Qt 信号（发射语义由 core 保证：批量只发一次等）
    void onIndexChanged(int index) override { emit indexChanged(index); }
    void onCleanChanged(bool clean) override { emit cleanChanged(clean); }

    CommandStack* stack_;               // 不持有；bridge 先于它析构（见头注释）
};

} // namespace ur
