#pragma once
// ════════════════════════════════════════════════════════════════════════
// CommandStack —— 撤销栈本体："历史数据库"，不是链表
//
// 与 QUndoStack 的六处关键差异（设计精讲 §1 逐条对照）：
//  1. 平行 Entry 元数据表：text/类型/时间戳/字节/context 在 push 时冻结，
//     history() 只读遍历 + setIndex() 任意跳转（Qt 栈不可查询）
//  2. 双预算截断：条数上限 + 字节预算，超限从栈底弹（Krita/Blender 经验）
//  3. 最小信号面：indexChanged 为主信号；setIndex 跨 N 步只发一次
//     （Qt 的 canUndoChanged/undoTextChanged/indexChanged 多信号风暴）
//  4. undoIn(ctx)：Eclipse IUndoContext 式子序列撤销（向前搜索 + 重放）
//  5. 合并策略可插拔（setMergePolicy），且时间戳来自可注入时钟（可测性）
//  6. 宏是 RAII 的（TransactionScope 包装 beginMacro/endMacro），
//     支持 abort 逆序回滚与空事务自动丢弃
//
// 所有权：栈接管 push 进来的命令（unique_ptr），clear()/析构时释放。
// 线程模型：单线程（GUI 线程），与 QUndoStack 相同；不做锁。
// ════════════════════════════════════════════════════════════════════════
#include <QObject>
#include <QVector>
#include <QString>
#include <functional>
#include <memory>
#include <vector>
#include "core/Command.h"
#include "core/MergePolicy.h"

namespace ur {

class CommandStack : public QObject
{
    Q_OBJECT
public:
    // 历史条目元数据：push 时冻结；合并只更新 bytes/timestampMs/merged
    struct Entry {
        QString    text;
        int        id = kCmdInvalid;
        qint64     timestampMs = 0;
        qint64     bytes = 0;
        ContextSet contexts;
        bool       merged = false;      // 曾吸收过后续命令
    };

    using ClockFn           = std::function<qint64()>;
    using SelectionApplier  = std::function<void(const SelectionMemento&)>;
    using SelectionProvider = std::function<SelectionMemento()>;

    explicit CommandStack(QObject* parent = nullptr);
    ~CommandStack() override;

    // ── 主入口：push 即执行（先 cmd->redo()，再记账）──────────────
    void push(std::unique_ptr<Command> cmd);

    bool canUndo() const { return index_ > 0; }
    bool canRedo() const { return index_ < int(cmds_.size()); }
    bool undo();
    bool redo();

    // ── Eclipse 式 context 过滤撤销（子序列 undo，游标不动）────────
    // 从 index_-1 向前搜索首条 contexts 与 ctx 相交的命令，undo 它，
    // 再把它上方、游标下方的命令升序重放——其余 context 的净效果不变。
    // ctx 为空等价于普通 undo()。代价与边界如实声明于设计精讲 §8。
    bool canUndoIn(const ContextSet& ctx) const;
    bool undoIn(const ContextSet& ctx);

    // ── 历史导航 ────────────────────────────────────────────────
    void setIndex(int i);               // [0, count()]；批量执行只发一次信号
    int  index() const { return index_; }
    int  count() const { return int(cmds_.size()); }
    QVector<Entry> history() const { return entries_; }
    void clear();                       // 丢弃全部历史（不回滚效果，同 Qt）

    // ── 预算与检查点 ────────────────────────────────────────────
    void setUndoLimit(int steps);       // <=0 = 不限；立即生效（可能触发裁剪）
    int  undoLimit() const { return undoLimit_; }
    void setMemoryBudget(qint64 bytes); // <=0 = 不限
    qint64 memoryBudget() const { return memoryBudget_; }
    qint64 totalBytes() const { return totalBytes_; }
    void setClean();                    // 检查点 = 当前 index（"已保存"点）
    bool isClean() const { return cleanIndex_ >= 0 && cleanIndex_ == index_; }

    // ── 策略与环境注入 ──────────────────────────────────────────
    void setMergePolicy(std::unique_ptr<MergePolicy> p);
    MergePolicy* mergePolicy() const { return policy_.get(); }
    void setClock(ClockFn fn) { clock_ = std::move(fn); }   // 测试注入假时钟
    void setSelectionApplier(SelectionApplier fn) { selApplier_ = std::move(fn); }
    void setSelectionProvider(SelectionProvider fn) { selProvider_ = std::move(fn); }

    // ── 宏原语（日常请用 TransactionScope，这里是它的底层实现）───
    void beginMacro(const QString& text);   // 嵌套安全：最外层文本生效
    void endMacro();    // 最外层调用结算：空→丢弃 / 1条→直入 / ≥2→Compound
    int  macroMark() const { return int(macroBuf_.size()); }
    void rollbackMacroTo(int mark);         // 逆序 undo 并丢弃 mark 之后的缓冲
    bool isMacroActive() const { return macroDepth_ > 0; }
    int  macroDepth() const { return macroDepth_; }

signals:
    void indexChanged(int index);       // 主信号：历史/游标任何变化发且只发一次
    void cleanChanged(bool clean);      // 仅当 isClean() 变位时发

private:
    qint64 nowMs() const;
    SelectionMemento captureSelection() const;
    void applySelection(const SelectionMemento& m);

    void truncateRedo();                // 丢弃游标之后的 redo 段（含字节账）
    void appendExecuted(std::unique_ptr<Command> cmd, qint64 timestampMs,
                        bool wasClean); // 已执行命令入账（push 尾部/宏结算共用）
    bool trimBudgetQuiet();             // 双预算从栈底裁剪；返回是否有变动
    void stepUndoInternal(bool applySel);
    void stepRedoInternal(bool applySel);
    void emitCleanIfChanged(bool wasClean);
    Entry makeEntry(const Command* c, qint64 timestampMs) const;
    static bool matchesContext(const Command* c, const ContextSet& ctx);

    std::vector<std::unique_ptr<Command>> cmds_;    // 活动命令（所有权）
    QVector<Entry> entries_;                        // 与 cmds_ 严格平行
    // 与 cmds_ 严格平行：被 undoIn(ctx) 外科手术式撤销过的命令标记。
    // 已中和的命令不再被 canUndoIn/undoIn 搜索命中，也不参与 undoIn 重放；
    // 线性 undo/redo/setIndex 仍会正常执行它（历史保持线性完整）。
    // 这是"子序列撤销"与"线性历史"共存的边界，设计精讲 §8 如实声明。
    std::vector<char> neutralized_;
    int    index_ = 0;                              // 游标：已执行命令数
    qint64 totalBytes_ = 0;
    int    cleanIndex_ = -1;                        // -1 = 无检查点/已丢失
    int    undoLimit_ = 0;                          // 0 = 不限
    qint64 memoryBudget_ = 0;                       // 0 = 不限

    int     macroDepth_ = 0;
    QString macroText_;
    std::vector<std::unique_ptr<Command>> macroBuf_;

    std::unique_ptr<MergePolicy> policy_;           // 默认 AdjacentIdMergePolicy
    ClockFn           clock_;                       // 空 = 系统时钟
    SelectionApplier  selApplier_;
    SelectionProvider selProvider_;
};

} // namespace ur
