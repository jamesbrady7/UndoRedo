#include "app/StackBridge.h"

namespace ur {

StackBridge::StackBridge(CommandStack* stack, QObject* parent)
    : QObject(parent)
    , stack_(stack)
{
    if (stack_)
        stack_->addObserver(this);
}

StackBridge::~StackBridge()
{
    // 契约：bridge 先于 stack 析构（持有方的成员声明顺序保证），
    // 这里的 removeObserver 必须踩在活对象上。
    if (stack_)
        stack_->removeObserver(this);
}

} // namespace ur
