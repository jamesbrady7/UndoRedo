#include "core/CommandStack.h"

#include <QDateTime>
#include <algorithm>
#include "core/CompoundCommand.h"

namespace ur {

CommandStack::CommandStack(QObject* parent)
    : QObject(parent)
    , policy_(std::make_unique<AdjacentIdMergePolicy>())
{
}

CommandStack::~CommandStack() = default;

// ════════════════════════════════════════════════════════════════════════
// push —— 全部机制的汇聚点，流程（与设计精讲 §5 的图一致）：
//   ① 盖选择快照(前) → ② cmd->redo()（push 即执行）→ ③ 盖选择快照(后)
//   → ④ 宏激活？入缓冲区返回 → ⑤ 合并判定（仅 redo 段空 + 栈顶）
//   → ⑥ 截断 redo 段 → ⑦ append + 冻结 Entry → ⑧ 双预算裁剪 → ⑨ 信号
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

    const qint64 now = nowMs();

    // ④ 宏缓冲区：宏内 push 不逐条入账、不发信号（结算在 endMacro）
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
            // 栈顶文本/字节（"最小信号面"= 只用这一个信号承载所有刷新）。
            const qint64 oldBytes = entries_.back().bytes;
            const qint64 newBytes = top->estimatedMemory();
            entries_.back().text = top->text();     // 文本反映合并后的最终效果
            entries_.back().bytes = newBytes;
            entries_.back().timestampMs = now;
            entries_.back().merged = true;
            top->selAfter = cmd->selAfter;  // 选择快照更新到最新效果之后
            totalBytes_ += newBytes - oldBytes;
            emit indexChanged(index_);
            return;                     // cmd 随 unique_ptr 析构被丢弃
        }
    }

    // ⑥⑦⑧⑨ 常规入账
    truncateRedo();                     // ⑥ 新命令杀死 redo 段（线性历史语义）
    cmds_.push_back(std::move(cmd));
    Entry e = makeEntry(cmds_.back().get(), now);   // ⑦ 元数据在此冻结
    entries_.append(e);
    neutralized_.push_back(0);
    index_ = int(cmds_.size());
    totalBytes_ += e.bytes;
    trimBudgetQuiet();                  // ⑧ 双预算从栈底裁剪
    emit indexChanged(index_);          // ⑨
    emitCleanIfChanged(wasClean);
}

// ── 基本 undo/redo ─────────────────────────────────────────────────────
bool CommandStack::undo()
{
    if (!canUndo())
        return false;
    const bool wasClean = isClean();
    stepUndoInternal(true);
    emit indexChanged(index_);
    emitCleanIfChanged(wasClean);
    return true;
}

bool CommandStack::redo()
{
    if (!canRedo())
        return false;
    const bool wasClean = isClean();
    stepRedoInternal(true);
    emit indexChanged(index_);
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

// ── context 过滤撤销（Eclipse 式向前搜索 + 上方重放）───────────────────
bool CommandStack::canUndoIn(const ContextSet& ctx) const
{
    if (ctx.isEmpty())
        return canUndo();
    for (int i = index_ - 1; i >= 0; --i) {
        if (!neutralized_[size_t(i)] && matchesContext(cmds_[size_t(i)].get(), ctx))
            return true;
    }
    return false;
}

bool CommandStack::undoIn(const ContextSet& ctx)
{
    if (ctx.isEmpty())
        return undo();
    int k = -1;
    for (int i = index_ - 1; i >= 0; --i) {     // 向前搜索首条命中（跳过已中和）
        if (!neutralized_[size_t(i)] && matchesContext(cmds_[size_t(i)].get(), ctx)) {
            k = i;
            break;
        }
    }
    if (k < 0)
        return false;

    // 演示语义（设计精讲 §8 如实声明代价）：
    //   undo 目标命令 → 把它上方、游标下方的【未中和】命令升序重放。
    //   游标不动、历史形状不变，净效果 = 精准挖掉 k 的效果。
    //   两条前提写进命令契约：
    //   · 命令必须幂等（redo 重放安全）——相对增量式命令不满足，会被重放放大
    //   · k 之后若有依赖 k 的命令（如"删除了 k 修改的对象"），其重放按
    //     各自的目标安全规则 no-op（findShape 找不到即跳过）
    //   k 被标记"已中和"：不再被子序列搜索命中，也不参与后续重放。
    const bool wasClean = isClean();
    const SelectionMemento selNow = captureSelection();

    cmds_[size_t(k)]->undo();
    neutralized_[size_t(k)] = 1;
    for (int i = k + 1; i < index_; ++i) {
        if (!neutralized_[size_t(i)])
            cmds_[size_t(i)]->redo();
    }

    applySelection(selNow);             // 选择集恢复原状（游标未动）
    emit indexChanged(index_);          // 内容变了，历史面板要刷新
    emitCleanIfChanged(wasClean);
    return true;
}

bool CommandStack::matchesContext(const Command* c, const ContextSet& ctx)
{
    if (!c)
        return false;
    const ContextSet cs = c->contexts();    // Compound 穿透：聚合子命令标签
    for (const QString& s : cs) {
        if (ctx.contains(s))
            return true;
    }
    return false;
}

// ── 历史导航 ───────────────────────────────────────────────────────────
void CommandStack::setIndex(int i)
{
    i = qBound(0, i, count());
    if (i == index_)
        return;
    const bool wasClean = isClean();
    while (index_ > i)
        stepUndoInternal(false);        // 批量：中间步不应用选择快照
    while (index_ < i)
        stepRedoInternal(false);
    // 选择快照只在终点应用一次（避免中间态闪烁与信号风暴）
    if (i > 0)
        applySelection(cmds_[size_t(i - 1)]->selAfter);
    else
        applySelection(SelectionMemento());
    emit indexChanged(index_);          // 跨 N 步只发一次
    emitCleanIfChanged(wasClean);
}

void CommandStack::clear()
{
    const bool wasClean = isClean();
    cmds_.clear();
    entries_.clear();
    neutralized_.clear();
    index_ = 0;
    totalBytes_ = 0;
    cleanIndex_ = -1;
    macroDepth_ = 0;
    macroBuf_.clear();
    macroText_.clear();
    emit indexChanged(0);
    emitCleanIfChanged(wasClean);
}

// ── 预算与检查点 ───────────────────────────────────────────────────────
void CommandStack::setUndoLimit(int steps)
{
    undoLimit_ = steps;
    const bool wasClean = isClean();
    if (trimBudgetQuiet()) {
        emit indexChanged(index_);
        emitCleanIfChanged(wasClean);
    }
}

void CommandStack::setMemoryBudget(qint64 bytes)
{
    memoryBudget_ = bytes;
    const bool wasClean = isClean();
    if (trimBudgetQuiet()) {
        emit indexChanged(index_);
        emitCleanIfChanged(wasClean);
    }
}

// 双预算裁剪：条数或字节任一超限 → 从栈底（最老、离游标最远）弹出。
// 游标与 clean 检查点随之下移；检查点弹出栈底即"永久丢失"（-1），
// 与 Qt 的"clean 状态不可恢复"语义一致。极端情况：单条命令就超预算 →
// 一直弹到空栈（诚实语义：预算是硬承诺，不做"至少留一条"的含糊）。
bool CommandStack::trimBudgetQuiet()
{
    bool changed = false;
    while (!cmds_.empty()) {
        const bool overSteps = undoLimit_ > 0 && int(cmds_.size()) > undoLimit_;
        const bool overBytes = memoryBudget_ > 0 && totalBytes_ > memoryBudget_;
        if (!overSteps && !overBytes)
            break;
        totalBytes_ -= entries_.front().bytes;
        cmds_.erase(cmds_.begin());
        entries_.removeFirst();
        neutralized_.erase(neutralized_.begin());
        index_ = qMax(0, index_ - 1);
        if (cleanIndex_ >= 0) {
            --cleanIndex_;
            if (cleanIndex_ < 0)
                cleanIndex_ = -1;       // 检查点被裁掉 → 丢失
        }
        changed = true;
    }
    return changed;
}

void CommandStack::setClean()
{
    cleanIndex_ = index_;
    emit cleanChanged(true);
}

// ── 策略注入 ───────────────────────────────────────────────────────────
void CommandStack::setMergePolicy(std::unique_ptr<MergePolicy> p)
{
    policy_ = std::move(p);
}

// ── 宏原语 ─────────────────────────────────────────────────────────────
void CommandStack::beginMacro(const QString& text)
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
        return;                         // 空事务自动丢弃：不占栈位、不发信号
    const bool wasClean = isClean();
    const qint64 now = nowMs();
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
    mark = qBound(0, mark, int(macroBuf_.size()));
    for (int i = int(macroBuf_.size()) - 1; i >= mark; --i)
        macroBuf_[size_t(i)]->undo();   // 逆序回滚已执行的子命令
    macroBuf_.resize(size_t(mark));
}

// 已执行命令入账（不再执行 redo、不做合并判定——宏是原子历史步）
void CommandStack::appendExecuted(std::unique_ptr<Command> cmd, qint64 timestampMs,
                                  bool wasClean)
{
    truncateRedo();
    cmds_.push_back(std::move(cmd));
    Entry e = makeEntry(cmds_.back().get(), timestampMs);
    entries_.append(e);
    neutralized_.push_back(0);
    index_ = int(cmds_.size());
    totalBytes_ += e.bytes;
    trimBudgetQuiet();
    emit indexChanged(index_);
    emitCleanIfChanged(wasClean);
}

// ── 内部工具 ───────────────────────────────────────────────────────────
qint64 CommandStack::nowMs() const
{
    return clock_ ? clock_() : QDateTime::currentMSecsSinceEpoch();
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
        entries_.removeLast();
        neutralized_.pop_back();
    }
}

CommandStack::Entry CommandStack::makeEntry(const Command* c, qint64 timestampMs) const
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
        emit cleanChanged(isClean());
}

} // namespace ur
