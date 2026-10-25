#pragma once
// ════════════════════════════════════════════════════════════════════════
// HistoryDock —— 历史面板（"栈可遍历"卖点的可视化）
//
// QUndoStack 只给你两条 action 文本；这里把整条历史摊开：
//   · 每行 = 一条 Entry（文本/时间/字节/context/合并标记）
//   · 点击任意行 → setIndex 一步跳转（Krita undo docker 同款体验）
//   · 「只撤销属性改动 / 只撤销画布改动」→ undoIn(ctx)
//     Eclipse IUndoContext 式子序列撤销（游标不动，精准挖除）
// ════════════════════════════════════════════════════════════════════════
#include <QString>
#include <QWidget>
#include <vector>

class QListWidget;
class QPushButton;
class QLabel;

namespace ur { class CommandStack; class StackBridge; }

namespace cd {

class HistoryDock : public QWidget
{
    Q_OBJECT
public:
    // stack=命令 API（setIndex/undoIn/history），bridge=Qt 信号源（core 零 Qt，
    // 刷新触发从桥上来）——两者都不持有，生命周期由 MainWindow 保证
    explicit HistoryDock(ur::CommandStack* stack, ur::StackBridge* bridge,
                         QWidget* parent = nullptr);

public slots:
    void refresh();                     // indexChanged → 增量刷新（见 .cpp）

private slots:
    void onRowActivated(int row);       // row 0 = "(初始状态)"；row j → setIndex(j)
    void onUndoInspector();
    void onUndoCanvas();

private:
    void rebuildRows();                 // 条数变了：唯一会 new/delete item 的路径
    void updateRows();                  // 条数没变：只改文本真的变了的行
    void highlightCursor();             // 游标底色/加粗（跨刷新复用旧 item）
    void updateStats();                 // 统计文本 + 两个 undoIn 按钮可用态

    ur::CommandStack* stack_;
    QListWidget* list_;
    QPushButton* undoInsp_;
    QPushButton* undoCanvas_;
    QLabel* stats_;
    bool refreshing_ = false;           // 重建期间闸住行点击（防递归）
    int  highlighted_ = -1;             // 当前高亮行（-1 = 无）；用于撤销上一次高亮
    std::vector<QString> shown_;        // 各行已显示的文本（增量刷新的比对基准）
};

} // namespace cd
