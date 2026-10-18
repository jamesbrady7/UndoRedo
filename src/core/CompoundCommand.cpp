#include "core/CompoundCommand.h"

namespace ur {

CompoundCommand::CompoundCommand(std::string text, std::vector<std::unique_ptr<Command>> children)
    : text_(std::move(text))
    , children_(std::move(children))
{
}

void CompoundCommand::redo()
{
    for (auto& c : children_)           // 正序：与当初执行顺序一致
        c->redo();
}

void CompoundCommand::undo()
{
    for (auto it = children_.rbegin(); it != children_.rend(); ++it)
        (*it)->undo();                  // 逆序：后做的先撤
}

int64_t CompoundCommand::estimatedMemory() const
{
    int64_t sum = int64_t(sizeof(*this));
    for (const auto& c : children_)
        sum += c->estimatedMemory();
    return sum;
}

ContextSet CompoundCommand::contexts() const
{
    ContextSet unionSet;
    for (const auto& c : children_) {
        const ContextSet cs = c->contexts();    // 物化局部量：临时量的迭代器不越全表达式
        unionSet.insert(cs.begin(), cs.end());
    }
    return unionSet;
}

const Command* CompoundCommand::childAt(int i) const
{
    if (i < 0 || i >= int(children_.size()))
        return nullptr;
    return children_[size_t(i)].get();
}

} // namespace ur
