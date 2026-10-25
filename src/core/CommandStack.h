#pragma once
// ════════════════════════════════════════════════════════════════════════
// CommandStack —— 撤销栈本体："历史数据库"，不是链表
//
// 与 QUndoStack 的六处关键差异（设计精讲 §1 逐条对照）：
//  1. 平行 Entry 元数据表：text/类型/时间戳/字节/context 在 push 时冻结，
//     history() 只读遍历 + setIndex() 任意跳转（Qt 栈不可查询）
//  2. 双预算截断：条数上限 + 字节预算，超限从栈底弹（Krita/Blender 经验）
//  3. 最小通知面：onIndexChanged 为主回调；setIndex 跨 N 步只发一次
//     （Qt 的 canUndoChanged/undoTextChanged/indexChanged 多信号风暴）
//  4. undoIn(ctx)：Eclipse IUndoContext 式子序列撤销（向前搜索 + 上层重放）
//  5. 合并策略可插拔（setMergePolicy），且时间戳来自可注入时钟（可测性）
//  6. 宏是 RAII 的（TransactionScope 包装 beginMacro/endMacro），
//     支持 abort 逆序回滚与空事务自动丢弃
//
// 所有权：栈接管 push 进来的命令（unique_ptr），clear()/析构时释放。
// 线程模型：单线程（GUI 线程），与 QUndoStack 相同；不做锁。
//
// ★ applied_ 位图（与 cmds_ 严格平行）：每条命令"此刻是否真的生效"。
//   正常线性历史里它恒等于 [0,index_) 全 1、其余全 0；undoIn() 会把某条
//   单独置 0（游标不动），于是"已执行区"里出现一个空洞。undo/redo 步进
//   因此必须查这个位图：
//     · 已经是 0 的命令，线性 undo 不再调 undo()（否则就是【撤销两次】，
//       相对增量式命令会当场算错——这是旧版用单向 neutralized_ 标记时的真 bug）
//     · 线性 redo 会把它重新置 1（Ctrl+Z 后再 Ctrl+Y 应回到原状态）
//   推论：canUndo() 只表示"游标左侧还有历史"，不代表"按下去一定有效果"。
//
// 纯 C++17 零依赖：不继承 QObject，通知走内嵌 Observer 接口（Qt 侧
// 由 demo 层的 StackBridge 把回调重发为信号）。Observer 三条纪律：
//  · Observer 析构前必须 removeObserver（栈持裸指针，不做自动讣告）
//  · 回调内禁止增删 Observer（发射遍历的是指针快照，增删要下一轮才生效）
//  · stack 必须 outlive 所有 Observer（成员声明序：bridge 在 stack 之后）
// ════════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "core/Command.h"
#include "core/MergePolicy.h"

namespace ur {

class CommandStack
{
public:
    // 历史条目元数据：push 时冻结；合并只更新 bytes/timestampMs/merged
    struct Entry {
        std::string text;
        int         id = kCmdInvalid;
        int64_t     timestampMs = 0;
        int64_t     bytes = 0;
        ContextSet  contexts;
        bool        merged = false;      // 曾吸收过后续命令
    };

    // 通知接口（替代 Qt signals）：空虚函数默认实现，只覆写关心的回调。
    struct Observer {
        virtual ~Observer() = default;
        virtual void onIndexChanged(int index) { (void)index; }
        virtual void onCleanChanged(bool clean) { (void)clean; }
    };

    using ClockFn           = std::function<int64_t()>;
    using SelectionApplier  = std::function<void(const SelectionMemento&)>;
    using SelectionProvider = std::function<SelectionMemento()>;

    CommandStack();
    ~CommandStack();

    // 观察者/时钟按地址语义工作——栈不可拷贝也不可搬移（搬走会静默坏掉）
    CommandStack(const CommandStack&) = delete;
    CommandStack& operator=(const CommandStack&) = delete;
    CommandStack(CommandStack&&) = delete;
    CommandStack& operator=(CommandStack&&) = delete;

    // ── 通知面 ──────────────────────────────────────────────────
    void addObserver(Observer* o);       // 空/重复添加忽略
    void removeObserver(Observer* o);

    // ── 主入口：push 即执行（先 cmd->redo()，再记账）──────────────
    void push(std::unique_ptr<Command> cmd);

    bool canUndo() const { return index_ > 0; }
    bool canRedo() const { return index_ < int(cmds_.size()); }
    bool undo();
    bool redo();

    // ── Eclipse 式 context 过滤撤销（子序列 undo，游标不动）────────
    // 从 index_-1 向前搜索首条"仍生效且 contexts 与 ctx 相交"的命令，
    // 只撤它一条（applied=false），再把它上方、游标下方仍生效的命令升序
    // 重放一遍——因为那条命令的 undo 可能覆盖掉上层命令对同一目标的写入
    // （例如 B 把 X 移到 10、C 再把 X 移到 20；撤掉 B 会把 X 拽回 0，
    // 必须重放 C 才能让"画布通道的净效果不变"）。重放安全的前提就是
    // Command.h 的 redo 幂等纪律。
    // ctx 为空等价于普通 undo()。代价与边界如实声明于设计精讲 §8。
    bool canUndoIn(const ContextSet& ctx) const;
    bool undoIn(const ContextSet& ctx);

    // ── 历史导航 ────────────────────────────────────────────────
    void setIndex(int i);               // [0, count()]；批量执行只发一次通知
    int  index() const { return index_; }
    int  count() const { return int(cmds_.size()); }
    // 返回【引用】：历史面板每次通知都要读它，按值返回等于每次通知都复制
    // 一整张表（含 N 个 std::string）。约定：引用只在栈下一次被修改前有效，
    // 别把它存成成员或跨 push/undo 使用。
    const std::vector<Entry>& history() const { return entries_; }
    void clear();                       // 丢弃全部历史（不回滚效果，同 Qt）

    // ── 预算与检查点 ────────────────────────────────────────────
    void setUndoLimit(int steps);       // <=0 = 不限；立即生效（可能触发裁剪）
    int  undoLimit() const { return undoLimit_; }
    void setMemoryBudget(int64_t bytes); // <=0 = 不限
    int64_t memoryBudget() const { return memoryBudget_; }
    int64_t totalBytes() const { return totalBytes_; }
    void setClean();                    // 检查点 = 当前 index（"已保存"点）
    bool isClean() const { return cleanIndex_ >= 0 && cleanIndex_ == index_; }

    // ── 策略与环境注入 ──────────────────────────────────────────
    void setMergePolicy(std::unique_ptr<MergePolicy> p);
    MergePolicy* mergePolicy() const { return policy_.get(); }
    void setClock(ClockFn fn) { clock_ = std::move(fn); }   // 测试注入假时钟
    void setSelectionApplier(SelectionApplier fn) { selApplier_ = std::move(fn); }
    void setSelectionProvider(SelectionProvider fn) { selProvider_ = std::move(fn); }

    // ── 宏原语（日常请用 TransactionScope，这里是它的底层实现）───
    void beginMacro(const std::string& text);   // 嵌套安全：最外层文本生效
    void endMacro();    // 最外层调用结算：空→丢弃 / 1条→直入 / ≥2→Compound
    int  macroMark() const { return int(macroBuf_.size()); }
    void rollbackMacroTo(int mark);         // 逆序 undo 并丢弃 mark 之后的缓冲
    bool isMacroActive() const { return macroDepth_ > 0; }
    int  macroDepth() const { return macroDepth_; }

private:
    // 通知发射：拷贝观察者快照后遍历（回调内增删观察者不影响本轮）。
    void notifyIndexChanged(int index);
    void notifyCleanChanged(bool clean);

    int64_t nowMs() const;
    SelectionMemento captureSelection() const;
    void applySelection(const SelectionMemento& m);

    void truncateRedo();                // 丢弃游标之后的 redo 段（含字节账）
    void appendExecuted(std::unique_ptr<Command> cmd, int64_t timestampMs,
                        bool wasClean); // 已执行命令入账（push 尾部/宏结算共用）
    bool trimBudgetQuiet();             // 双预算从栈底裁剪；返回是否有变动
    void stepUndoInternal(bool applySel);
    void stepRedoInternal(bool applySel);
    void emitCleanIfChanged(bool wasClean);
    Entry makeEntry(const Command* c, int64_t timestampMs) const;
    // 相交判定用的是 Entry 里【冻结】的 context（push 时快照），而不是
    // 现问 command：① 免掉每次搜索都构造一个 unordered_set；② 保证"历史
    // 面板显示的标签"与"能被 undoIn 命中的标签"永远是同一份数据。
    static bool contextIntersects(const ContextSet& frozen, const ContextSet& ctx);

    std::vector<std::unique_ptr<Command>> cmds_;    // 活动命令（所有权）
    std::vector<Entry> entries_;                    // 与 cmds_ 严格平行
    // 与 cmds_ 严格平行：每条命令此刻是否真的生效（1=生效，0=被 undoIn 挖掉）。
    // 语义与不变量见文件头 ★ 段。绝不能退化成"单向中和标记"——那样线性 undo
    // 会把同一条命令撤两次。
    std::vector<char> applied_;
    int    index_ = 0;                              // 游标：已执行命令数
    int64_t totalBytes_ = 0;
    int    cleanIndex_ = -1;                        // -1 = 无检查点/已丢失
    int    undoLimit_ = 0;                          // 0 = 不限
    int64_t memoryBudget_ = 0;                      // 0 = 不限

    int     macroDepth_ = 0;
    std::string macroText_;
    std::vector<std::unique_ptr<Command>> macroBuf_;

    std::unique_ptr<MergePolicy> policy_;           // 默认 AdjacentIdMergePolicy
    ClockFn           clock_;                       // 空 = 系统时钟
    SelectionApplier  selApplier_;
    SelectionProvider selProvider_;
    std::vector<Observer*> observers_;              // 裸指针，纪律见头注释
};

} // namespace ur
