#pragma once
// ════════════════════════════════════════════════════════════════════════
// InspectorPanel —— 属性面板（inspector context 命令的生产者）
//
// 滑条的 valueChanged 【每次都 push 命令】——不是"松手才 push"：
// push 即执行让命令的 redo() 直接充当实时预览，TimeWindow 合并策略
// 负责把拖动风暴折叠成一条历史（这正是 Krita 笔触/文本编辑器输入
// 的标准形态）。防回流：程序化刷新 UI 时用 syncing_ 闸门。
// ════════════════════════════════════════════════════════════════════════
#include <QWidget>

class QLabel;
class QSlider;
class QPushButton;

namespace ur { class CommandStack; }

namespace cd {

class CanvasScene;

class InspectorPanel : public QWidget
{
    Q_OBJECT
public:
    InspectorPanel(CanvasScene* scene, ur::CommandStack* stack, QWidget* parent = nullptr);

private slots:
    void onSelectionChanged();
    void onHueSlider(int value);        // 连续 push → 时间窗合并演示
    void onStrokeSlider(int value);
    void onFillButton();                // QColorDialog 一次性命令

private:
    void pushFill(const QColor& to);
    void pushStroke(qreal to);

    CanvasScene* scene_;
    ur::CommandStack* stack_;
    bool syncing_ = false;              // UI 程序化刷新闸门（防信号回流）

    QLabel* title_;
    QSlider* hue_;
    QSlider* stroke_;
    QPushButton* fillBtn_;
    QLabel* hint_;
};

} // namespace cd
