#include "app/HistoryDock.h"

#include <QBrush>
#include <QDateTime>
#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include "app/StackBridge.h"
#include "core/CommandStack.h"
#include "core/Context.h"

namespace cd {

namespace {
// 一行的展示文本。格式串只有 5 个占位符，逐个 .arg 时参数顺序必须与 %n 对齐：
// 旧版写的是 "#%1 %2 [%3] %4B"，参数却是 (序号, 文本, ctx, 字节, 时间)——
// 时间参数根本没有对应的占位符，被 QString::arg 静默丢弃，面板上永远看不到
// 时间戳（而时间正是"合并刷新时间戳"这一机制的可见证据）。
QString rowText(const ur::CommandStack::Entry& e, int ordinal)
{
    QStringList ctxs;
    for (const std::string& c : e.contexts)
        ctxs << QString::fromStdString(c);      // 边界纪律：fromStdString 上 UI
    ctxs.sort();
    const QString time = QDateTime::fromMSecsSinceEpoch(qint64(e.timestampMs))
                             .toString(QStringLiteral("HH:mm:ss"));
    QString line = QStringLiteral("#%1  %2   [%3]  %4  %5B")
                       .arg(ordinal)
                       .arg(QString::fromStdString(e.text),
                            ctxs.join(QLatin1Char('+')),
                            time)
                       .arg(qint64(e.bytes));
    if (e.merged)
        line += QStringLiteral("  <merged>");
    return line;
}

QColor rowColor(const ur::CommandStack::Entry& e)
{
    if (ur::containsCtx(e.contexts, ur::kCtxInspector))
        return QColor(0x9b, 0x59, 0xb6);        // 属性通道紫色
    if (ur::containsCtx(e.contexts, ur::kCtxCanvas))
        return QColor(0x2c, 0x6e, 0xb3);        // 画布通道蓝色
    return QColor();
}
} // namespace

HistoryDock::HistoryDock(ur::CommandStack* stack, ur::StackBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , stack_(stack)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);

    auto* caption = new QLabel(QStringLiteral("历史（点击任意行跳转）"), this);
    QFont f = caption->font();
    f.setBold(true);
    caption->setFont(f);
    root->addWidget(caption);

    list_ = new QListWidget(this);
    list_->setAlternatingRowColors(true);
    connect(list_, &QListWidget::currentRowChanged, this, &HistoryDock::onRowActivated);
    root->addWidget(list_, 1);

    stats_ = new QLabel(this);
    stats_->setStyleSheet(QStringLiteral("color:#666;"));
    root->addWidget(stats_);

    undoInsp_ = new QPushButton(QStringLiteral("只撤销一步属性改动 (undoIn inspector)"), this);
    undoCanvas_ = new QPushButton(QStringLiteral("只撤销一步画布改动 (undoIn canvas)"), this);
    connect(undoInsp_, &QPushButton::clicked, this, &HistoryDock::onUndoInspector);
    connect(undoCanvas_, &QPushButton::clicked, this, &HistoryDock::onUndoCanvas);
    root->addWidget(undoInsp_);
    root->addWidget(undoCanvas_);

    // 刷新触发接 StackBridge 的 Qt 信号（core 的 Observer 回调已被桥重发）；
    // bridge 先于本 dock 析构时 QObject 自动断连，无悬垂信号
    if (bridge) {
        connect(bridge, &ur::StackBridge::indexChanged, this, [this](int) { refresh(); });
        connect(bridge, &ur::StackBridge::cleanChanged, this, [this](bool) { refresh(); });
    }
    refresh();
}

void HistoryDock::refresh()
{
    if (!stack_ || refreshing_)
        return;
    refreshing_ = true;                 // 闸住行点击：下面 setCurrentItem 会发信号

    // ★ 增量刷新：条数没变时【不重建 item】。
    //   合并（拖拽/滑条）只改栈顶 Entry 的文本/字节/时间戳，游标也可能不动；
    //   旧版每次 indexChanged 都 clear() + 重建整表 —— 一次拖拽 10 个事件就是
    //   10 次全量重建（N 个 item 的 new/delete + 文本重排），而实际只有一行
    //   变了。现在只有"条数变化"（push 新命令 / 裁剪 / clear）才重建。
    const std::vector<ur::CommandStack::Entry>& h = stack_->history();
    if (list_->count() != int(h.size()) + 1)
        rebuildRows();
    else
        updateRows();
    highlightCursor();
    updateStats();

    refreshing_ = false;
}

void HistoryDock::rebuildRows()
{
    const std::vector<ur::CommandStack::Entry>& h = stack_->history();
    list_->clear();
    highlighted_ = -1;                  // 旧 item 已销毁，高亮记录作废
    shown_.clear();
    shown_.reserve(h.size() + 1);

    // row 0 = 初始状态；row j (>=1) = 第 j 条命令执行后的状态
    auto* zero = new QListWidgetItem(QStringLiteral("(初始状态)"), list_);
    zero->setForeground(QColor(0x88, 0x88, 0x88));
    shown_.push_back(zero->text());

    for (int j = 0; j < int(h.size()); ++j) {
        const ur::CommandStack::Entry& e = h[size_t(j)];
        auto* item = new QListWidgetItem(rowText(e, j + 1), list_);
        const QColor c = rowColor(e);
        if (c.isValid())
            item->setForeground(c);
        shown_.push_back(item->text());
    }
}

void HistoryDock::updateRows()
{
    const std::vector<ur::CommandStack::Entry>& h = stack_->history();
    for (int j = 0; j < int(h.size()); ++j) {
        const QString want = rowText(h[size_t(j)], j + 1);
        if (shown_[size_t(j) + 1] == want)
            continue;                   // 这一行没变：一次 setText 都不做
        list_->item(j + 1)->setText(want);
        shown_[size_t(j) + 1] = want;
    }
}

void HistoryDock::highlightCursor()
{
    const int cur = stack_->index();
    if (highlighted_ == cur)
        return;                         // 游标没动：连字体都不用碰
    if (highlighted_ >= 0 && highlighted_ < list_->count()) {
        QListWidgetItem* old = list_->item(highlighted_);
        old->setBackground(QBrush());   // 恢复默认底色（默认构造的 QBrush）
        QFont f = old->font();
        f.setBold(false);
        old->setFont(f);
    }
    highlighted_ = -1;
    if (cur >= 0 && cur < list_->count()) {
        QListWidgetItem* it = list_->item(cur);
        it->setBackground(QColor(0xff, 0xf3, 0xc4));   // 当前状态高亮
        QFont bf = it->font();
        bf.setBold(true);
        it->setFont(bf);
        list_->setCurrentItem(it);      // 会让行"看起来"跳过去（不触发 setIndex）
        highlighted_ = cur;
    }
}

void HistoryDock::updateStats()
{
    stats_->setText(QStringLiteral("游标 %1 / %2 条 · 内存 %3 B · %4")
                        .arg(stack_->index())
                        .arg(stack_->count())
                        .arg(qint64(stack_->totalBytes()))
                        .arg(stack_->isClean() ? QStringLiteral("已保存")
                                               : QStringLiteral("未保存")));
    // 静态 ContextSet：这两个集合每次刷新都要用，旧版现场构造 {kCtxInspector}
    // 会在每次 indexChanged 时分配一个 unordered_set（含一个 std::string）。
    static const ur::ContextSet kInspectorCtx{ur::kCtxInspector};
    static const ur::ContextSet kCanvasCtx{ur::kCtxCanvas};
    undoInsp_->setEnabled(stack_->canUndoIn(kInspectorCtx));
    undoCanvas_->setEnabled(stack_->canUndoIn(kCanvasCtx));
}

void HistoryDock::onRowActivated(int row)
{
    if (!stack_ || refreshing_ || row < 0)
        return;
    if (row != stack_->index())
        stack_->setIndex(row);          // row j = "前 j 条已执行" = setIndex(j)
}

void HistoryDock::onUndoInspector()
{
    if (stack_)
        stack_->undoIn({ur::kCtxInspector});
}

void HistoryDock::onUndoCanvas()
{
    if (stack_)
        stack_->undoIn({ur::kCtxCanvas});
}

} // namespace cd
