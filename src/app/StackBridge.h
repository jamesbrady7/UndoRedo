#pragma once
// ════════════════════════════════════════════════════════════════════════
// StackBridge —— core Observer → Qt 信号的单向桥（demo/app 层专属）
//
// core 是纯 C++17 零依赖的（不继承 QObject、无 signals），Qt 侧的
// connect 生态由本类接回来：实现 ur::CommandStack::Observer，把两个
// 回调原样重发为 Qt 信号。UI 代码（MainWindow/HistoryDock）只连 bridge。
//
// ★ 生命周期硬约束（唯一可能 UAF 的地方，现在由 StackHost 写死在结构上）：
//   · 构造 addObserver、析构 removeObserver——bridge 必须【先于】stack 析构
//   · StackHost 把两者装进同一个对象，成员声明序 stack → bridge 保证
//     析构序 bridge → stack：契约不再是"记得把两行按顺序声明"的口头纪律，
//     而是编译器强制执行的结构（谁把这两行调换，退出时就是堆损坏）
//   · 绝不给 bridge 挂 QObject parent：parent 化会让它推迟到 ~QObject
//     阶段才析构（晚于 StackHost 的成员析构），踩已死的 stack
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

// ★ 栈 + 桥的复合体：持有方只要放一个成员，析构序就自动正确。
//   声明序 stack → bridge ⇒ 逆序析构 bridge → stack（bridge 的
//   removeObserver 踩的永远是活栈）。注意 bridge 的构造依赖已就位的
//   stack，所以不能反过来声明（编译期报错，正是我们要的）。
//   用法：成员 `ur::StackHost stackHost_;`，对外暴露 `&stackHost_.stack`
//   与 `&stackHost_.bridge`。不要把它挂到 QObject 树上。
struct StackHost {
    CommandStack stack;
    StackBridge  bridge{&stack};        // 默认成员初始化：stack 已构造
};

} // namespace ur
