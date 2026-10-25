#include "core/CommandStack.h"

#include <algorithm>
#include <chrono>
#include "core/CompoundCommand.h"

namespace ur {

CommandStack::CommandStack()
    : policy_(std::make_unique<AdjacentIdMergePolicy>())
{
}

CommandStack::~CommandStack() = default;

// ── 通知面 ─────────────────────────────────────────────────────────────
void CommandStack::addObserver(Observer* o)
{
    if (!o)
        return;
    for (Observer* e : observers_)
        if (e == o)
            return;                     // 重复添加忽略（幂等）
    observers_.push_back(o);
}

void CommandStack::removeObserver(Observer* o)
{
    observers_.erase(std::remove(observers_.begin(), observers_.end(), o),
                     observers_.end());
}

// 发射=拷贝指针快照后遍历：回调内增删 Observer 不会使本轮迭代失效
//（增删的生效时机是下一轮发射；纪律见头注释）。
void CommandStack::notifyIndexChanged(int index)
{
    const std::vector<Observer*> snapshot = observers_;
    for (Observer* o : snapshot)
        o->onIndexChanged(index);
}

void CommandStack::notifyCleanChanged(bool clean)
{
    const std::vector<Observer*> snapshot = observers_;
    for (Observer* o : snapshot)
        o->onCleanChanged(clean);
}

// ════════════════════════════════════════════════════════════════════════
// push —— 全部机制的汇聚点，流程（与设计精讲 §5 的图一致）：
//   ① 盖选择快照(前) → ② cmd->redo()（push 即执行）→ ③ 盖选择快照(后)
//   → ④ 宏激活？入缓冲区返回 → ⑤ 合并判定（仅 redo 段空 + 栈顶）
//   → ⑥ 截断 redo 段 → ⑦ append + 冻结 Entry → ⑧ 双预算裁剪 → ⑨ 通知
// ════════════════════════════════════════════════════════════════════════
void CommandStack::push(std::unique_ptr<Command> cmd)
{
    if (!cmd)
        return;
    const bool wasClean = isClean();

    // ①③ 选择快照：配置了 provider 则自动盖章（命令作者无需关心）；
    //     未配置则尊重调用方手填的 selBefore/selAfter（单测走这条路）
    if (selProvider_)
        cmd->selBefore = captureSelection();
    cmd->redo();                        // ② push 即执行
    if (selProvider_)
        cmd->selAfter = captureSelection();

    const int64_t now = nowMs();

    // ④ 宏缓冲区：宏内 push 不逐条入账、不发通知（结算在 endMacro）
    if (macroDepth_ > 0) {
        macroBuf_.push_back(std::move(cmd));
        return;
    }

    // ⑤ 合并判定：只在 redo 段空（index_==count）时与栈顶尝试。
    //    策略说"可合并"后还要命令自己 mergeWith 成功——两级握手。
    if (index_ == int(cmds_.size()) && !cmds_.empty() && policy_) {
        Command* top = cmds_.back().get();
        if (policy_->canMerge(top, cmd.get(), entries_.back().timestampMs, now)
            && top->mergeWith(cmd.get())) {
            // 吸收成功：更新栈顶 Entry 的字节/时间戳/merged 标记。
            // 游标与条数不变，但仍发一次 indexChanged——历史面板要刷新
            // 栈顶文本/字节（"最小通知面"= 只用这一个回调承载所有刷新）。
            const int64_t oldBytes = entries_.back().bytes;
            const int64_t newBytes = top->estimatedMemory();
            entries_.back().text = top->text();     // 文本反映合并后的最终效果
            entries_.back().bytes = newBytes;
            entries_.back().timestampMs = now;
            entries_.back().merged = true;
            top->selAfter = cmd->selAfter;  // 选择快照更新到最新效果之后
            totalBytes_ += newBytes - oldBytes;
            // 合并同样可能把字节账推过预算（吸收的载荷可能比原来大），
            // 所以这条路径也必须过一遍裁剪——否则"字节预算是硬承诺"就漏了一个入口。
            trimBudgetQuiet();
            notifyIndexChanged(index_);
            emitCleanIfChanged(wasClean);
            return;                     // cmd 随 unique_ptr 析构被丢弃
        }
    }

    // ⑥⑦⑧⑨ 常规入账
    truncateRedo();                     // ⑥ 新命令杀死 redo 段（线性历史语义）
    cmds_.push_back(std::move(cmd));
    Entry e = makeEntry(cmds_.back().get(), now);   // ⑦ 元数据在此冻结
    entries_.push_back(e);
    index_ = int(cmds_.size());
    totalBytes_ += e.bytes;
    trimBudgetQuiet();                  // ⑧ 双预算从栈底裁剪
    notifyIndexChanged(index_);         // ⑨
    emitCleanIfChanged(wasClean);
}

// ── 基本 undo/redo ─────────────────────────────────────────────────────
bool CommandStack::undo()
{
    if (!canUndo())
        return false;
    const bool wasClean = isClean();
    stepUndoInternal(true);
    notifyIndexChanged(index_);
    emitCleanIfChanged(wasClean);
    return true;
}

bool CommandStack::redo()
{
    if (!canRedo())
        return false;
    const bool wasClean = isClean();
    stepRedoInternal(true);
    notifyIndexChanged(index_);
    emitCleanIfChanged(wasClean);
    return true;
}

void CommandStack::stepUndoInternal(bool applySel)
{
    --index_;
    Command* c = cmds_[size_t(index_)].get();
    c->undo();
    if (applySel)
        applySelection(c->selBefore);
}

void CommandStack::stepRedoInternal(bool applySel)
{
    Command* c = cmds_[size_t(index_)].get();
    c->redo();
    ++index_;
    if (applySel)
        applySelection(c->selAfter);
}

// ── context 过滤撤销（向前搜索 + 上层重放 + 从历史移除）─────────────────
bool CommandStack::canUndoIn(const ContextSet& ctx) const
{
    if (ctx.empty())
        return canUndo();
    for (int i = index_ - 1; i >= 0; --i) {
        if (contextIntersects(entries_[size_t(i)].contexts, ctx))
            return true;
    }
    return false;
}

bool CommandStack::undoIn(const ContextSet& ctx)
{
    if (ctx.empty())
        return undo();
    int k = -1;
    for (int i = index_ - 1; i >= 0; --i) {
        if (contextIntersects(entries_[size_t(i)].contexts, ctx)) {
            k = i;
            break;
        }
    }
    if (k < 0)
        return false;

    // 语义：精准撤掉 k 的效果，并把这一步【从历史里删掉】（游标左移）。
    //   · 先撤 k，再把它上方的命令升序重放：k 的 undo 可能覆盖掉上层对
    //     同一目标的写入（B 把 X 移到 10、C 再移到 20；撤 B 会把 X 拽回 0，
    //     必须重放 C 才能让"其它通道的净效果不变"）。重放安全的唯一前提
    //     是 Command.h 的 redo 幂等纪律。
    //   · 为什么删：栈的公开不变量是"状态 == 前 index_ 条命令的重放结果"，
    //     历史面板的行就是状态、点击行调 setIndex(j)。若保留这条命令只做
    //     标记，状态就不再等于任何前缀，点击历史行必然对不上（用户实测
    //     报过这个 bug）。删掉后不变量恒成立，游标也自然左移——历史面板的
    //     高亮跟着动，反馈与状态一致。
    //   · 代价（如实声明，设计精讲 §8.2）：这一步不能再用 Ctrl+Y 重做回来，
    //     原有的 redo 分支也一并作废（改历史 = 原来的未来不再成立，与 push
    //     同规则）。它是"把某通道的一步从历史里抹掉"，不是"临时跳过"。
    const bool wasClean = isClean();
    const SelectionMemento selNow = captureSelection();

    // 改写历史 ⇒ redo 分支作废（与 push 同规则：你改了历史，原来的"未来"就不
    // 再成立）。必须显式截断，不能留着：redo 段里同通道命令的基线快照是按
    // "包含 k"的状态捕获的，留着它们以后重做就会算错（T20 实测场景）。
    truncateRedo();

    cmds_[size_t(k)]->undo();
    for (int i = k + 1; i < index_; ++i)
        cmds_[size_t(i)]->redo();

    // 从历史移除 k（两张平行表 + 字节账 + clean 检查点同步）
    totalBytes_ -= entries_[size_t(k)].bytes;
    cmds_.erase(cmds_.begin() + k);
    entries_.erase(entries_.begin() + k);
    if (cleanIndex_ == k)
        cleanIndex_ = -1;               // 检查点包含这一步，那个状态已不存在
    else if (cleanIndex_ > k)
        --cleanIndex_;
    index_ = int(cmds_.size());         // 剩余命令全部已执行（redo 段已被截断）

    applySelection(selNow);             // 选择集保持用户眼前的样子
    notifyIndexChanged(index_);
    emitCleanIfChanged(wasClean);
    return true;
}

bool CommandStack::contextIntersects(const ContextSet& frozen, const ContextSet& ctx)
{
    // 小集合对撞：先遍历元素少的那个（contexts 通常 1~2 个标签）。
    if (frozen.size() > ctx.size()) {
        for (const std::string& s : ctx)
            if (frozen.find(s) != frozen.end())
                return true;
        return false;
    }
    for (const std::string& s : frozen)
        if (ctx.find(s) != ctx.end())
            return true;
    return false;
}

// ── 历史导航 ───────────────────────────────────────────────────────────
void CommandStack::setIndex(int i)
{
    i = std::clamp(i, 0, count());
    if (i == index_)
        return;
    const bool wasClean = isClean();
    while (index_ > i)
        stepUndoInternal(false);        // 批量：中间步不应用选择快照
    while (index_ < i)
        stepRedoInternal(false);
    // 选择快照只在终点应用一次（避免中间态闪烁与通知风暴）
    if (i > 0)
        applySelection(cmds_[size_t(i - 1)]->selAfter);
    else
        applySelection(SelectionMemento());
    notifyIndexChanged(index_);         // 跨 N 步只发一次
    emitCleanIfChanged(wasClean);
}

void CommandStack::clear()
{
    const bool wasClean = isClean();
    cmds_.clear();
    entries_.clear();
    index_ = 0;
    totalBytes_ = 0;
    cleanIndex_ = -1;
    macroDepth_ = 0;
    macroBuf_.clear();
    macroText_.clear();
    notifyIndexChanged(0);
    emitCleanIfChanged(wasClean);
}

// ── 预算与检查点 ───────────────────────────────────────────────────────
void CommandStack::setUndoLimit(int steps)
{
    undoLimit_ = steps;
    const bool wasClean = isClean();
    if (trimBudgetQuiet()) {
        notifyIndexChanged(index_);
        emitCleanIfChanged(wasClean);
    }
}

void CommandStack::setMemoryBudget(int64_t bytes)
{
    memoryBudget_ = bytes;
    const bool wasClean = isClean();
    if (trimBudgetQuiet()) {
        notifyIndexChanged(index_);
        emitCleanIfChanged(wasClean);
    }
}

// 双预算裁剪：条数或字节任一超限 → 从栈底（最老、离游标最远）弹出。
// 游标与 clean 检查点随之下移；检查点弹出栈底即"永久丢失"（-1），
// 与 Qt 的"clean 状态不可恢复"语义一致。极端情况：单条命令就超预算 →
// 一直弹到空栈（诚实语义：预算是硬承诺，不做"至少留一条"的含糊）。
//
// 实现：先算出要弹几条，再一次性 erase 区间。旧版在循环里 erase(begin())
// 每次搬动整个 vector，最坏 O(n^2)——预算收紧到 0 时正好是最坏情况。
bool CommandStack::trimBudgetQuiet()
{
    if (cmds_.empty())
        return false;

    int drop = 0;
    int64_t freed = 0;
    while (drop < int(cmds_.size())) {
        const bool overSteps = undoLimit_ > 0 && (int(cmds_.size()) - drop) > undoLimit_;
        const bool overBytes = memoryBudget_ > 0 && (totalBytes_ - freed) > memoryBudget_;
        if (!overSteps && !overBytes)
            break;
        freed += entries_[size_t(drop)].bytes;
        ++drop;
    }
    if (drop == 0)
        return false;

    totalBytes_ -= freed;
    cmds_.erase(cmds_.begin(), cmds_.begin() + drop);
    entries_.erase(entries_.begin(), entries_.begin() + drop);
    index_ = (std::max)(0, index_ - drop);   // 括号防 Windows 的 max 宏
    if (cleanIndex_ >= 0) {
        cleanIndex_ -= drop;
        if (cleanIndex_ < 0)
            cleanIndex_ = -1;       // 检查点被裁掉 → 丢失
    }
    return true;
}

void CommandStack::setClean()
{
    const bool wasClean = isClean();
    cleanIndex_ = index_;
    // 变位才发（与 Qt QUndoStack::setClean 一致）：连点两次"保存"不该
    // 让观察者再刷一遍 UI。旧版无条件 notify，与"最小通知面"自相矛盾。
    if (!wasClean)
        notifyCleanChanged(true);
}

// ── 策略注入 ───────────────────────────────────────────────────────────
void CommandStack::setMergePolicy(std::unique_ptr<MergePolicy> p)
{
    policy_ = std::move(p);
}

// ── 宏原语 ─────────────────────────────────────────────────────────────
void CommandStack::beginMacro(const std::string& text)
{
    if (macroDepth_ == 0) {
        macroText_ = text;              // 最外层文本生效（嵌套宏折叠进外层）
        macroBuf_.clear();
    }
    ++macroDepth_;
}

void CommandStack::endMacro()
{
    if (macroDepth_ == 0)
        return;                         // 无宏可结算（防御：不配对调用）
    --macroDepth_;
    if (macroDepth_ > 0)
        return;                         // 嵌套层：结算留给最外层

    // 最外层结算（GIMP undo_group_end 语义）：
    if (macroBuf_.empty())
        return;                         // 空事务自动丢弃：不占栈位、不发通知
    const bool wasClean = isClean();
    const int64_t now = nowMs();
    if (macroBuf_.size() == 1) {
        appendExecuted(std::move(macroBuf_.front()), now, wasClean);
    } else {
        auto compound = std::make_unique<CompoundCommand>(macroText_, std::move(macroBuf_));
        compound->selBefore = compound->childAt(0)->selBefore;
        compound->selAfter  = compound->childAt(compound->childCount() - 1)->selAfter;
        appendExecuted(std::move(compound), now, wasClean);
    }
    macroBuf_.clear();
}

void CommandStack::rollbackMacroTo(int mark)
{
    if (macroDepth_ == 0)
        return;
    mark = std::clamp(mark, 0, int(macroBuf_.size()));
    for (int i = int(macroBuf_.size()) - 1; i >= mark; --i)
        macroBuf_[size_t(i)]->undo();   // 逆序回滚已执行的子命令
    macroBuf_.resize(size_t(mark));
}

// 已执行命令入账（不再执行 redo、不做合并判定——宏是原子历史步）
void CommandStack::appendExecuted(std::unique_ptr<Command> cmd, int64_t timestampMs,
                                  bool wasClean)
{
    truncateRedo();
    cmds_.push_back(std::move(cmd));
    Entry e = makeEntry(cmds_.back().get(), timestampMs);
    entries_.push_back(e);
    index_ = int(cmds_.size());
    totalBytes_ += e.bytes;
    trimBudgetQuiet();
    notifyIndexChanged(index_);
    emitCleanIfChanged(wasClean);
}

// ── 内部工具 ───────────────────────────────────────────────────────────
int64_t CommandStack::nowMs() const
{
    if (clock_)
        return clock_();
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

SelectionMemento CommandStack::captureSelection() const
{
    return selProvider_ ? selProvider_() : SelectionMemento();
}

void CommandStack::applySelection(const SelectionMemento& m)
{
    if (selApplier_)
        selApplier_(m);
}

void CommandStack::truncateRedo()
{
    while (int(cmds_.size()) > index_) {
        totalBytes_ -= entries_.back().bytes;
        cmds_.pop_back();
        entries_.pop_back();
    }
    // 检查点若落在被丢弃的 redo 段里，那个状态已不可达 → 永久丢失（同裁剪）
    if (cleanIndex_ > index_)
        cleanIndex_ = -1;
}

CommandStack::Entry CommandStack::makeEntry(const Command* c, int64_t timestampMs) const
{
    Entry e;
    e.text = c->text();
    e.id = c->id();
    e.timestampMs = timestampMs;
    e.bytes = c->estimatedMemory();
    e.contexts = c->contexts();
    return e;
}

void CommandStack::emitCleanIfChanged(bool wasClean)
{
    if (wasClean != isClean())
        notifyCleanChanged(isClean());
}

} // namespace ur
