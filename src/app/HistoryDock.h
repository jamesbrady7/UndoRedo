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
#include <QWidget>

class QListWidget;
class QPushButton;
class QLabel;

namespace ur { class CommandStack; }

namespace cd {

class HistoryDock : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryDock(ur::CommandStack* stack, QWidget* parent = nullptr);

public slots:
    void refresh();                     // indexChanged → 整表重建（demo 规模够用）

private slots:
    void onRowActivated(int row);       // row 0 = "(初始状态)"；row j → setIndex(j)
    void onUndoInspector();
    void onUndoCanvas();

private:
    ur::CommandStack* stack_;
    QListWidget* list_;
    QPushButton* undoInsp_;
    QPushButton* undoCanvas_;
    QLabel* stats_;
    bool refreshing_ = false;           // 重建期间闸住行点击（防递归）
};

} // namespace cd
