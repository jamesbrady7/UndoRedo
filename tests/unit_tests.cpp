// ════════════════════════════════════════════════════════════════════════
// unit_tests —— undoredo_core 框架层单元测试（纯 C++17，零 Qt 依赖）
//
// 约定（照 NavTreeGroup）：
//   · 14 块自包含测试，每块打印 职责/输入/输出/校验，[OK]/[FAIL] 标记
//   · 退出码 = 失败断言数（0 = 全绿）；不依赖 CTest
//   · printf 格式串里只用 ASCII 符号（装饰性 Unicode 会触发 C4474
//     并破坏 MSVC 的参数检查）；中文正文本身 GBK 可表示，安全
//   · 每块自建栈与状态，不跨块共享（"别让测试测它自己"）
//   · 时间相关一律用注入的假时钟，不用真 sleep（慢机/CI 不翻车）
//   · 本文件与 core 一样不 include 任何 Qt 头——它是"core 零依赖"的活证据
// ════════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/Command.h"
#include "core/CommandStack.h"
#include "core/CompoundCommand.h"
#include "core/CommandFactory.h"
#include "core/MergePolicy.h"
#include "core/TransactionScope.h"

// ── 失败计数与断言 ─────────────────────────────────────────────────────
static int g_fail = 0;
static int g_ok = 0;

static void checkOk(bool cond, const char* what)
{
    if (cond) {
        printf("  [OK]   %s\n", what);
        ++g_ok;
    } else {
        printf("  [FAIL] %s\n", what);
        ++g_fail;
    }
}

// ── 假时钟（时间窗合并测试的可控时间源）────────────────────────────────
static int64_t g_fakeNow = 0;
static int64_t fakeClock() { return g_fakeNow; }

// ── 计数观察者（替代旧版 QObject::connect lambda 计数）──────────────────
// 纪律示范：Observer 声明在 stack 之后（析构序上先于 stack 死亡），
// 且在测试尾部显式 removeObserver——调用方代码应当照这个样子写。
namespace {
class CountingObserver : public ur::CommandStack::Observer
{
public:
    int idxSignals = 0;
    int cleanSignals = 0;
    void onIndexChanged(int) override { ++idxSignals; }
    void onCleanChanged(bool) override { ++cleanSignals; }
};
} // namespace

// ════════════════════════════════════════════════════════════════════════
// 测试命令：对共享 int 施加 delta（redo=+，undo=-）。
// 注意它是"相对增量"命令——mergeWith 把 next 的 delta 折进自己，
// 因此合并链 undo 一步回到整段起点（from 语义由累加 delta 承载）。
// ════════════════════════════════════════════════════════════════════════
namespace {

class TestCommand : public ur::Command
{
public:
    TestCommand(int& target, int delta, int id = 100,
                std::string tag = std::string(), ur::ContextSet ctx = ur::ContextSet(),
                int64_t bytes = 64)
        : target_(target), delta_(delta), id_(id), tag_(std::move(tag)),
          ctx_(std::move(ctx)), bytes_(bytes) {}

    void redo() override { target_ += delta_; }
    void undo() override { target_ -= delta_; }
    int  id() const override { return id_; }
    std::string text() const override { return "delta" + std::to_string(delta_); }
    std::string mergeTag() const override { return tag_; }
    ur::ContextSet contexts() const override { return ctx_; }
    int64_t estimatedMemory() const override { return bytes_; }

    bool mergeWith(const ur::Command* next) override
    {
        const auto* n = dynamic_cast<const TestCommand*>(next);
        if (!n)
            return false;
        delta_ += n->delta_;        // 只吸收效果，不改自身"起点"
        return true;
    }
    int delta() const { return delta_; }

private:
    int& target_;
    int delta_;
    int id_;
    std::string tag_;
    ur::ContextSet ctx_;
    int64_t bytes_;
};

std::unique_ptr<TestCommand> tc(int& target, int delta, int id = 100,
                                std::string tag = std::string(),
                                ur::ContextSet ctx = ur::ContextSet(),
                                int64_t bytes = 64)
{
    return std::make_unique<TestCommand>(target, delta, id, std::move(tag),
                                         std::move(ctx), bytes);
}

// 幂等测试命令：把变量设为【绝对终态】（redo 重放 N 次 == 1 次）。
// undoIn(ctx) 的"挖除+重放"算法【依赖】这条幂等纪律（Command.h 已写明），
// T13 用它验证重放不变式；T14 用它验证序列化往返。
class SetVarCommand : public ur::Command
{
public:
    SetVarCommand(int& var, int to, int id = 300, ur::ContextSet ctx = ur::ContextSet())
        : var_(var), to_(to), id_(id), ctx_(std::move(ctx)) {}
    void redo() override
    {
        if (!captured_) { from_ = var_; captured_ = true; }   // 初态只捕获一次
        var_ = to_;                                           // 设置绝对终态
    }
    void undo() override { var_ = from_; }
    int  id() const override { return id_; }
    std::string text() const override { return "set " + std::to_string(to_); }
    ur::ContextSet contexts() const override { return ctx_; }
    std::string payloadType() const override { return "test.setvar"; }
    // payload 对 core 是不透明文本（约定 JSON）——测试里手写手读即可，
    // 这正说明 core 完全不认识 JSON：格式知识在调用方。
    std::string toPayload() const override
    {
        return "{\"to\":" + std::to_string(to_) + "}";
    }
    static int parseTo(const std::string& data)   // creator 侧的解析（演示用）
    {
        const size_t p = data.find("\"to\":");
        if (p == std::string::npos)
            return 0;
        return std::stoi(data.substr(p + 5));     // stoi 自动停在 '}' 前
    }
private:
    int& var_;
    int to_;
    int from_ = 0;
    bool captured_ = false;
    int id_;
    ur::ContextSet ctx_;
};

std::unique_ptr<SetVarCommand> sv(int& var, int to, int id = 300,
                                  ur::ContextSet ctx = ur::ContextSet())
{
    return std::make_unique<SetVarCommand>(var, to, id, std::move(ctx));
}

} // namespace

// ════════════════════════════════════════════════════════════════════════
// T1 push 即执行 / undo / redo / 幂等性
// ════════════════════════════════════════════════════════════════════════
static void t1_basic()
{
    printf("== T1 基本语义: push 即执行 / undo / redo / 幂等 ==\n");
    int v = 0;
    ur::CommandStack stack;
    stack.push(tc(v, 5));
    checkOk(v == 5, "push 立即执行(redo), 无需手动调用");
    checkOk(stack.count() == 1 && stack.index() == 1, "入账: count=1, index=1");
    checkOk(stack.canUndo() && !stack.canRedo(), "canUndo=true, canRedo=false");
    stack.undo();
    checkOk(v == 0 && stack.index() == 0, "undo 回滚, 游标归 0");
    stack.redo();
    checkOk(v == 5, "redo 重放 == 首次 push 效果(幂等)");
    stack.redo();
    checkOk(v == 5 && stack.index() == 1, "redo 越界为 no-op");
    stack.undo(); stack.undo();
    checkOk(v == 0 && stack.index() == 0, "undo 越界为 no-op");
}

// ════════════════════════════════════════════════════════════════════════
// T2 redo 段截断 + 选择快照恢复
// ════════════════════════════════════════════════════════════════════════
static void t2_truncate_and_selection()
{
    printf("== T2 redo 段截断 + SelectionMemento 恢复 ==\n");
    int v = 0;
    ur::CommandStack stack;
    stack.push(tc(v, 1));
    stack.push(tc(v, 2));
    stack.undo();                       // 游标=1, redo 段有 1 条
    stack.push(tc(v, 10));              // 新命令杀死 redo 段(线性历史语义)
    checkOk(stack.count() == 2 && !stack.canRedo(), "push 截断 redo 段");
    checkOk(v == 11, "状态 = 命令1 + 命令10");

    // 选择快照：手填 selBefore/selAfter(未配置 provider 时栈尊重手填值)
    ur::SelectionMemento applied;
    int applyCount = 0;
    stack.setSelectionApplier([&](const ur::SelectionMemento& m) {
        applied = m; ++applyCount;
    });
    auto cmd = tc(v, 7);
    cmd->selBefore.ids = {1, 2};
    cmd->selAfter.ids = {3};
    stack.push(std::move(cmd));
    // 注：旧版这里的 QVector<quint64>({3}) 单元素花括号列表坑（MSVC 绑定到
    // QVector(int size) 构造出 {0,0,0}）随 std::vector 化已消失——
    // std::vector 的 initializer_list 构造优先于 size_type 构造，无窄化歧义。
    stack.undo();
    checkOk(applied.ids == std::vector<uint64_t>({1ull, 2ull}), "undo 恢复 selBefore");
    stack.redo();
    checkOk(applied.ids == std::vector<uint64_t>({3ull}), "redo 恢复 selAfter");
    checkOk(applyCount == 2, "applier 只在步内调用");
}

// ════════════════════════════════════════════════════════════════════════
// T3 条数上限截断（含游标位移）
// ════════════════════════════════════════════════════════════════════════
static void t3_step_limit()
{
    printf("== T3 setUndoLimit 条数预算: 栈底裁剪 + 游标随迁 ==\n");
    int v = 0;
    ur::CommandStack stack;
    stack.setUndoLimit(3);
    for (int i = 1; i <= 5; ++i)
        stack.push(tc(v, i));           // tag 空 → 不合并, 5 条独立命令
    checkOk(stack.count() == 3, "超限从栈底弹: count=3");
    checkOk(v == 15, "全部效果仍在(1+2+3+4+5)");
    checkOk(stack.index() == 3, "游标随栈底裁剪下移: index=3");
    stack.undo(); stack.undo(); stack.undo();
    checkOk(v == 1 + 2, "最老两条已被裁掉不可 undo, 剩 3+4+5 可回滚");
    checkOk(stack.index() == 0 && !stack.canUndo(), "undo 到底");
}

// ════════════════════════════════════════════════════════════════════════
// T4 字节预算裁剪
// ════════════════════════════════════════════════════════════════════════
static void t4_memory_budget()
{
    printf("== T4 setMemoryBudget 字节预算(Krita/Blender 经验) ==\n");
    int v = 0;
    ur::CommandStack stack;
    stack.setMemoryBudget(250);
    stack.push(tc(v, 1, 100, std::string(), ur::ContextSet(), 100));
    stack.push(tc(v, 2, 100, std::string(), ur::ContextSet(), 100));
    checkOk(stack.totalBytes() == 200 && stack.count() == 2, "200B < 250B 预算, 保留");
    stack.push(tc(v, 3, 100, std::string(), ur::ContextSet(), 100));
    checkOk(stack.count() == 2 && stack.totalBytes() == 200, "300B 超限 -> 弹栈底至 200B");
    checkOk(v == 6, "效果不回滚(裁的是历史, 不是状态)");
    stack.setMemoryBudget(50);          // 收紧预算立即生效
    checkOk(stack.count() == 0 && stack.index() == 0, "预算收紧: 立即裁到空栈(硬承诺)");
}

// ════════════════════════════════════════════════════════════════════════
// T5 clean 检查点
// ════════════════════════════════════════════════════════════════════════
static void t5_clean()
{
    printf("== T5 clean 检查点: setClean/isClean 随游标 + 裁剪丢失 ==\n");
    int v = 0;
    ur::CommandStack stack;
    CountingObserver obs;               // 声明在 stack 之后 → 先于 stack 析构
    stack.addObserver(&obs);
    stack.push(tc(v, 1));
    stack.push(tc(v, 2));
    stack.setClean();
    checkOk(stack.isClean(), "setClean 后 isClean=true");
    stack.push(tc(v, 3));
    checkOk(!stack.isClean(), "越过检查点 -> dirty");
    stack.undo();
    checkOk(stack.isClean(), "undo 回到检查点 -> clean 恢复(Qt 同语义)");

    // 检查点随栈底裁剪【同步位移】: 裁掉的是"回滚能力"而非状态,
    // 游标与检查点一起下移, 当前状态仍处在检查点上 → isClean 语义正确
    stack.setUndoLimit(2);              // 弹栈底 1 条: index 2->1, cleanIndex 2->1
    checkOk(stack.isClean(), "检查点随裁剪下移, 游标仍在检查点(同 Qt 位移规则)");
    stack.undo();
    checkOk(!stack.isClean(), "再 undo 越过检查点 -> dirty");
    stack.redo();
    checkOk(stack.isClean(), "redo 回检查点 -> clean 恢复");

    // cleanIndex==0 特判(Qt 同款): 检查点在栈底又被弹 → 永久丢失。
    // 道理: clean 状态="空文档", 被弹命令的效果已永久烧进当前状态,
    // 那个状态再也回不去了, 不能靠 index==cleanIndex==0 假装 clean。
    ur::CommandStack s2;
    int w = 0;
    s2.setClean();                      // 空栈即保存点: cleanIndex=0
    s2.push(tc(w, 1));
    s2.push(tc(w, 2));
    s2.setUndoLimit(1);                 // 弹栈底 → cleanIndex 0 -> -1(丢失)
    s2.undo();
    checkOk(!s2.isClean(), "零位检查点随栈底弹出永久丢失(不假装 clean)");
    checkOk(obs.cleanSignals >= 3, "cleanChanged 只在变位时发");
    stack.removeObserver(&obs);         // 纪律示范: Observer 析构前显式 remove
}

// ════════════════════════════════════════════════════════════════════════
// T6 最小通知面：批量操作只发一次 indexChanged
// ════════════════════════════════════════════════════════════════════════
static void t6_signals()
{
    printf("== T6 最小通知面: setIndex 跨 N 步只发一次 ==\n");
    int v = 0;
    ur::CommandStack stack;
    CountingObserver obs;
    stack.addObserver(&obs);
    stack.push(tc(v, 1));
    stack.push(tc(v, 2));
    stack.push(tc(v, 3));
    checkOk(obs.idxSignals == 3, "push 每次一发");
    obs.idxSignals = 0;
    stack.setIndex(0);                  // 跨 3 步批量回退
    checkOk(obs.idxSignals == 1, "setIndex(3->0) 只发一次 indexChanged");
    checkOk(v == 0, "批量 undo 效果正确");
    obs.idxSignals = 0;
    stack.setIndex(3);
    checkOk(obs.idxSignals == 1 && v == 6, "setIndex(0->3) 只发一次且效果正确");
    obs.idxSignals = 0;
    stack.setIndex(3);                  // 原地跳转
    checkOk(obs.idxSignals == 0, "setIndex 到当前位: 零通知");
    stack.removeObserver(&obs);
}

// ════════════════════════════════════════════════════════════════════════
// T7 history() 只读遍历（超越 QUndoStack 的关键卖点）
// ════════════════════════════════════════════════════════════════════════
static void t7_history()
{
    printf("== T7 history(): 文本/时间戳/字节/context/merged 全可查 ==\n");
    int v = 0;
    ur::CommandStack stack;
    g_fakeNow = 1000;
    stack.setClock(fakeClock);
    stack.setMergePolicy(std::make_unique<ur::TimeWindowMergePolicy>(500));
    ur::ContextSet ctxCanvas; ctxCanvas.insert(ur::kCtxCanvas);
    stack.push(tc(v, 1, 100, "m", ctxCanvas));
    g_fakeNow = 1200;
    stack.push(tc(v, 2, 100, "m", ctxCanvas));    // 窗口内 → 合并
    g_fakeNow = 5000;
    stack.push(tc(v, 3, 101, std::string(), ctxCanvas));  // 不合并

    const std::vector<ur::CommandStack::Entry> h = stack.history();
    checkOk(h.size() == 2, "合并后历史 2 条(可遍历, Qt 栈无此 API)");
    checkOk(h[0].text == "delta3", "合并条文本反映最终 delta(1+2)");
    checkOk(h[0].merged, "merged 标记 = 曾吸收后续命令");
    checkOk(h[0].timestampMs == 1200, "合并刷新时间戳到最新");
    checkOk(ur::containsCtx(h[0].contexts, ur::kCtxCanvas), "context 冻结在 Entry 里");
    checkOk(h[1].timestampMs == 5000 && !h[1].merged, "第二条独立");
    checkOk(h[0].bytes > 0 && h[1].bytes > 0, "字节数入账(预算/面板展示用)");
    checkOk(stack.totalBytes() == h[0].bytes + h[1].bytes, "totalBytes == sum(entry.bytes)");
}

// ════════════════════════════════════════════════════════════════════════
// T8 宏：折叠、嵌套、单条直入
// ════════════════════════════════════════════════════════════════════════
static void t8_macro()
{
    printf("== T8 宏: 多条折叠 Compound / 嵌套归外层 / 单条直入 ==\n");
    int v = 0;
    ur::CommandStack stack;
    stack.beginMacro("组合操作");        // /utf-8 下窄字面量即 UTF-8（core 约定编码）
    stack.push(tc(v, 1));
    stack.push(tc(v, 2));
    stack.beginMacro("内层");           // 嵌套: 折叠进外层
    stack.push(tc(v, 3));
    stack.endMacro();
    stack.push(tc(v, 4));
    stack.endMacro();
    checkOk(stack.count() == 1, "宏对外呈现单条历史");
    checkOk(v == 10, "子命令全部已执行(push 即执行, 宏内也不例外)");
    checkOk(stack.history()[0].text == "组合操作", "最外层文本生效");
    checkOk(!stack.isMacroActive(), "宏已结算");
    stack.undo();
    checkOk(v == 0, "Compound undo 逆序回滚全部子命令");
    stack.redo();
    checkOk(v == 10, "Compound redo 正序重放");

    // 单条宏: 不包 Compound, 直接入账(保留子命令自己的文本)
    ur::CommandStack s2;
    s2.beginMacro("不该出现的宏名");
    int w = 0;
    s2.push(tc(w, 9));
    s2.endMacro();
    checkOk(s2.count() == 1 && s2.history()[0].text == "delta9",
            "单条宏直入, 保留子命令文本");
}

// ════════════════════════════════════════════════════════════════════════
// T9 空事务自动丢弃
// ════════════════════════════════════════════════════════════════════════
static void t9_empty_transaction()
{
    printf("== T9 空事务: commit/未 commit 都自动丢弃, 零栈位零通知 ==\n");
    ur::CommandStack stack;
    CountingObserver obs;
    stack.addObserver(&obs);
    {
        ur::TransactionScope tx(&stack, "空事务A");
        tx.commit();
    }
    {
        ur::TransactionScope tx(&stack, "空事务B");
        (void)tx;                       // 未 commit 的空事务
    }
    checkOk(stack.count() == 0, "空事务不占栈位");
    checkOk(obs.idxSignals == 0, "空事务零通知(不污染历史面板)");
    checkOk(!stack.isMacroActive(), "宏深度已结算归零");
    stack.removeObserver(&obs);
}

// ════════════════════════════════════════════════════════════════════════
// T10 异常 abort：逆序回滚，净效果为零
// ════════════════════════════════════════════════════════════════════════
static void t10_exception_abort()
{
    printf("== T10 RAII abort: 异常栈展开 -> 逆序回滚 -> 净零 ==\n");
    int v = 0;
    int undoOrder = 0;
    ur::CommandStack stack;
    // 记录 undo 调用顺序的命令(验证"逆序")
    class OrderProbe : public TestCommand {
    public:
        OrderProbe(int& t, int d, int& order, int stamp)
            : TestCommand(t, d), order_(order), stamp_(stamp) {}
        void undo() override { TestCommand::undo(); order_ = order_ * 10 + stamp_; }
    private:
        int& order_; int stamp_;
    };
    try {
        ur::TransactionScope tx(&stack, "会失败的操作");
        stack.push(std::make_unique<OrderProbe>(v, 5, undoOrder, 1));
        stack.push(std::make_unique<OrderProbe>(v, 3, undoOrder, 2));
        throw std::runtime_error("模拟业务异常");
        // tx.commit() 永远不会执行
    } catch (...) {
        // 异常穿过作用域: TransactionScope 析构 → rollbackMacroTo(mark)
    }
    checkOk(v == 0, "已执行的子命令全部回滚, 目标净零");
    checkOk(undoOrder == 21, "回滚顺序 = 逆序(先 2 后 1)");
    checkOk(stack.count() == 0, "中止的事务不留历史条目");
    checkOk(!stack.isMacroActive(), "宏深度归零, 栈可继续使用");
    stack.push(tc(v, 1));
    checkOk(v == 1 && stack.count() == 1, "abort 之后栈功能完好");
}

// ════════════════════════════════════════════════════════════════════════
// T11 相邻同 id 合并 + undo 一步回整段起点
// ════════════════════════════════════════════════════════════════════════
static void t11_adjacent_merge()
{
    printf("== T11 AdjacentId 合并: 连续同类操作折成一条 ==\n");
    int v = 0;
    ur::CommandStack stack;             // 默认策略 = AdjacentIdMergePolicy
    stack.push(tc(v, 5, 100, "drag#1"));
    stack.push(tc(v, 3, 100, "drag#1"));
    stack.push(tc(v, 2, 100, "drag#1"));
    checkOk(stack.count() == 1, "同 id + 同 tag: 三条合并成一条");
    checkOk(v == 10, "效果全部生效(5+3+2)");
    stack.undo();
    checkOk(v == 0, "undo 一步回到整段连续操作的起点");
    stack.redo();
    checkOk(v == 10, "redo 一步重放整段");

    // tag 不同(换了手势/目标) → 阻断合并
    stack.push(tc(v, 1, 100, "drag#2"));
    checkOk(stack.count() == 2, "mergeTag 变化即阻断(极性/换目标防线)");
    // id 不同 → 阻断
    stack.push(tc(v, 1, 102, "drag#2"));
    checkOk(stack.count() == 3, "id 不同不合并");
    // 有 redo 段时不合并(只与"当前栈顶已执行命令"合并)
    stack.undo();
    const int before = stack.count();
    stack.push(tc(v, 7, 102, "drag#2"));
    checkOk(stack.count() == before - 1 + 1, "push 先截断 redo 段再入账");
}

// ════════════════════════════════════════════════════════════════════════
// T12 时间窗合并（假时钟）
// ════════════════════════════════════════════════════════════════════════
static void t12_time_window()
{
    printf("== T12 TimeWindow 合并: 窗口内折叠 / 超窗分段 / 空 tag 拒绝 ==\n");
    int v = 0;
    ur::CommandStack stack;
    stack.setClock(fakeClock);
    stack.setMergePolicy(std::make_unique<ur::TimeWindowMergePolicy>(500));

    g_fakeNow = 10000;
    stack.push(tc(v, 1, 200, "slider"));
    g_fakeNow = 10100;                  // +100ms, 窗口内
    stack.push(tc(v, 1, 200, "slider"));
    g_fakeNow = 10400;                  // 距上次 +300ms, 仍在窗口内
    stack.push(tc(v, 1, 200, "slider"));
    checkOk(stack.count() == 1 && v == 3, "窗口内连续滑条输入合并成一条");

    g_fakeNow = 11000;                  // 距上次 +600ms, 超窗
    stack.push(tc(v, 1, 200, "slider"));
    checkOk(stack.count() == 2, "超过 500ms 窗口 -> 新历史条目(一次停顿=一步)");

    // 空 tag = 未声明合并许可 → 时间窗内也拒绝
    g_fakeNow = 11100;
    stack.push(tc(v, 1, 201, std::string()));
    g_fakeNow = 11150;
    stack.push(tc(v, 1, 201, std::string()));
    checkOk(stack.count() == 4, "空 mergeTag 永不合并(保守默认, 比 Qt 更安全)");
}

// ════════════════════════════════════════════════════════════════════════
// T13 context 过滤撤销：向前搜索 + 重放不变式
// ════════════════════════════════════════════════════════════════════════
static void t13_context_filter()
{
    printf("== T13 undoIn(ctx): Eclipse 式子序列撤销 + 重放不变式 ==\n");
    printf("   (用幂等 SetVar 命令 —— undoIn 重放依赖幂等纪律)\n");
    int posX = 0, color = 0;
    ur::CommandStack stack;
    ur::ContextSet ctxCanvas;    ctxCanvas.insert(ur::kCtxCanvas);
    ur::ContextSet ctxInspector; ctxInspector.insert(ur::kCtxInspector);

    stack.push(sv(posX, 10, 101, ctxCanvas));     // A: 画布
    stack.push(sv(color, 1, 201, ctxInspector));  // B: 属性
    stack.push(sv(posX, 15, 101, ctxCanvas));     // C: 画布
    checkOk(posX == 15 && color == 1, "三条命令效果就位");

    checkOk(stack.canUndoIn(ctxInspector), "canUndoIn(inspector) 可见");
    const int idxBefore = stack.index();
    checkOk(stack.undoIn(ctxInspector), "undoIn(inspector) 执行成功");
    checkOk(color == 0, "属性改动被精准挖掉");
    checkOk(posX == 15, "重放不变式: 其余 context 的净效果不变(幂等重放)");
    checkOk(stack.index() == idxBefore, "游标不动(历史形状未变)");
    checkOk(!stack.canUndoIn(ctxInspector), "B 已中和: inspector 子序列清空");
    checkOk(stack.canUndoIn(ctxCanvas), "canvas 子序列仍在");

    // 向前搜索: 跳过不匹配的栈顶与已中和的旧命令, 撤到新的 inspector 命令
    stack.push(sv(color, 8, 201, ctxInspector));  // D: 属性(from=0)
    stack.push(sv(posX, 16, 101, ctxCanvas));     // E: 画布(栈顶)
    checkOk(stack.undoIn(ctxInspector), "栈顶不匹配时向前搜索命中 D");
    checkOk(color == 0 && posX == 16, "D 被挖除, 上方 E 重放, posX 不变");

    // 线性 undo 依然一路可用(已中和命令按原语义重放, 幂等命令收敛到初态)
    int guard = 0;
    while (stack.canUndo() && guard++ < 20)
        stack.undo();
    checkOk(posX == 0 && color == 0, "常规线性 undo 到起点, 状态归零");

    // 无命中返回 false
    checkOk(!stack.undoIn(ctxInspector), "无命中: undoIn 返回 false");
}

// ════════════════════════════════════════════════════════════════════════
// T14 序列化就绪：payload 文本 → 工厂重建 → 回放等价
// ════════════════════════════════════════════════════════════════════════
static void t14_payload()
{
    printf("== T14 payload 序列化: 不透明文本 -> 工厂重建 -> 回放等价 ==\n");
    int var1 = 0, var2 = 100;
    ur::CommandStack stack;
    stack.push(std::make_unique<SetVarCommand>(var1, 42));
    checkOk(var1 == 42, "原命令执行");

    // 取栈内命令不可行(栈不暴露), 演示路径: 对"同型命令"做序列化往返。
    // 这正是审计日志/跨会话 undo 的真实形态: 执行时留档, 恢复时重建。
    SetVarCommand original(var1, 42);
    const ur::Payload doc = ur::CommandFactory::toPayloadDoc(original);
    checkOk(doc.type == "test.setvar" && !doc.data.empty(),
            "payload 文档 = {type, data}(data 对 core 不透明)");

    // "过线"模拟: 存档再读回(文本原样往返, 字节级相等)
    const ur::Payload wire{doc.type, doc.data};
    checkOk(wire.data == doc.data, "payload 文本往返字节级相等");

    int& captured = var2;
    ur::CommandFactory::instance().registerType("test.setvar",
        [&captured](const std::string& data) {
            return std::make_unique<SetVarCommand>(captured,
                                                   SetVarCommand::parseTo(data));
        });
    auto rebuilt = ur::CommandFactory::instance().fromPayload(wire);
    checkOk(rebuilt != nullptr, "工厂按类型名重建命令");

    ur::CommandStack stack2;
    stack2.push(std::move(rebuilt));
    checkOk(var2 == 42, "重建命令在新目标上回放 == 原效果");
    stack2.undo();
    checkOk(var2 == 100, "重建命令的 undo 同样正确(初态在首次 redo 时捕获)");

    // 未注册类型 → nullptr(不崩, 优雅降级)
    checkOk(ur::CommandFactory::instance().fromPayload(
                ur::Payload{"no.such", "{}"}) == nullptr,
            "未注册类型返回 nullptr");
    // 空 type(不可序列化命令的 toPayloadDoc 结果) → nullptr
    checkOk(ur::CommandFactory::instance().fromPayload(ur::Payload{}) == nullptr,
            "空 type 返回 nullptr");
    // 不可序列化命令(payloadType 空) → type 为空的 Payload
    int v = 0;
    TestCommand plain(v, 1);
    checkOk(ur::CommandFactory::toPayloadDoc(plain).type.empty(),
            "未声明 payloadType 的命令 toPayloadDoc 为空 type");
}

// ════════════════════════════════════════════════════════════════════════
int main()
{
    printf("UndoRedoDemo unit_tests —— undoredo_core 框架 14 块自检(零 Qt 依赖)\n");
    printf("(每块自包含; 退出码 = 失败断言数)\n\n");

    t1_basic();
    t2_truncate_and_selection();
    t3_step_limit();
    t4_memory_budget();
    t5_clean();
    t6_signals();
    t7_history();
    t8_macro();
    t9_empty_transaction();
    t10_exception_abort();
    t11_adjacent_merge();
    t12_time_window();
    t13_context_filter();
    t14_payload();

    printf("\n== 汇总: %d OK, %d FAIL ==\n", g_ok, g_fail);
    printf("== unit_tests %s ==\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail;
}
