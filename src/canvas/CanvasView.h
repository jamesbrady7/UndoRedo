#pragma once
// ════════════════════════════════════════════════════════════════════════
// CanvasView —— 输入层：把手势翻译成命令（"每个事件都是一条命令"）
//
// 拖拽期间【每个 mouseMove 都 push 一条命令】：push 即执行 → 命令的
// redo() 就是实时反馈本身，不存在"先改视图、松手再补命令"的旁路
// （旁路 = 不可撤销状态的温床）。连续 push 由 TimeWindow 策略折叠成
// 一条历史；手势号（gestureId，每次按下递增）进 mergeTag，保证
// "松手再拖"必然是新历史条目。
//
// item 故意不设 ItemIsMovable —— 默认拖拽会绕过命令栈（见 ShapeItem.h）。
// ════════════════════════════════════════════════════════════════════════
#include <QGraphicsView>
#include "canvas/ShapeMeta.h"

namespace ur { class CommandStack; }

namespace cd {

class CanvasScene;

class CanvasView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit CanvasView(CanvasScene* scene, QWidget* parent = nullptr);
    void setCommandStack(ur::CommandStack* stack) { stack_ = stack; }

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    enum class Gesture { None, Move, Resize, Pan };

    bool beginResizeIfHandleHit(const QPointF& scenePos);   // 命中右下角手柄？
    void startMoveGesture(const QPointF& scenePos);
    void pushMoveStep(const QPointF& scenePos);
    void pushResizeStep(const QPointF& scenePos);

    CanvasScene* scene_;
    ur::CommandStack* stack_ = nullptr;

    Gesture gesture_ = Gesture::None;
    qint64  gestureId_ = 0;             // 每次按下递增 → mergeTag 分段键
    QPointF lastScenePos_;              // 移动手势：上一事件场景坐标
    QPointF startScenePos_;             // 缩放手势：起点场景坐标
    QRectF  resizeOrigRect_;            // 缩放手势：初始 pos+size
    ShapeId resizeId_ = 0;
    QPoint  lastPanPos_;                // 中键平移：上一视口坐标
};

} // namespace cd
