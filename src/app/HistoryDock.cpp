#include "app/HistoryDock.h"

#include <QDateTime>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include "app/StackBridge.h"
#include "core/CommandStack.h"
#include "core/Context.h"

namespace cd {

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
    refreshing_ = true;
    list_->clear();

    // row 0 = 初始状态；row j (>=1) = 第 j 条命令执行后的状态
    auto* zero = new QListWidgetItem(QStringLiteral("(初始状态)"), list_);
    zero->setForeground(QColor(0x88, 0x88, 0x88));

    const std::vector<ur::CommandStack::Entry> h = stack_->history();
    for (int j = 0; j < int(h.size()); ++j) {
        const ur::CommandStack::Entry& e = h[size_t(j)];
        QStringList ctxs;
        for (const std::string& c : e.contexts)
            ctxs << QString::fromStdString(c);   // 边界纪律：fromStdString 上 UI
        ctxs.sort();
        const QString time = QDateTime::fromMSecsSinceEpoch(qint64(e.timestampMs))
                                 .toString(QStringLiteral("HH:mm:ss"));
        QString line = QStringLiteral("#%1  %2   [%3]  %4B")
                           .arg(j + 1)
                           .arg(QString::fromStdString(e.text), ctxs.join(QLatin1Char('+')))
                           .arg(qint64(e.bytes))
                           .arg(time);
        if (e.merged)
            line += QStringLiteral("  <merged>");
        auto* item = new QListWidgetItem(line, list_);
        if (ur::containsCtx(e.contexts, ur::kCtxInspector))
            item->setForeground(QColor(0x9b, 0x59, 0xb6));   // 属性通道紫色
        else if (ur::containsCtx(e.contexts, ur::kCtxCanvas))
            item->setForeground(QColor(0x2c, 0x6e, 0xb3));   // 画布通道蓝色
    }

    const int cur = stack_->index();
    if (cur >= 0 && cur < list_->count()) {
        QListWidgetItem* it = list_->item(cur);
        it->setBackground(QColor(0xff, 0xf3, 0xc4));         // 当前状态高亮
        QFont bf = it->font();
        bf.setBold(true);
        it->setFont(bf);
        list_->setCurrentItem(it);
    }
    stats_->setText(QStringLiteral("游标 %1 / %2 条 · 内存 %3 B · %4")
                        .arg(stack_->index())
                        .arg(stack_->count())
                        .arg(qint64(stack_->totalBytes()))
                        .arg(stack_->isClean() ? QStringLiteral("已保存")
                                               : QStringLiteral("未保存")));
    undoInsp_->setEnabled(stack_->canUndoIn({ur::kCtxInspector}));
    undoCanvas_->setEnabled(stack_->canUndoIn({ur::kCtxCanvas}));
    refreshing_ = false;
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
