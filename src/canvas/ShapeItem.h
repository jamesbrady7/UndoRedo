#pragma once
// ════════════════════════════════════════════════════════════════════════
// ShapeItem —— 画布图形项（纯视图对象）
//
// 纪律：item 只是 ShapeMeta 的渲染化身，不承载业务逻辑，不自己发命令。
// 所有状态改动都从"命令 redo() → scene->setShapeMeta()"进来，
// 保证任何状态变化必然对应一条历史（没有旁路 = 没有不可撤销的操作）。
// 交互（拖动/缩放手柄命中）由 CanvasView 统一处理——item 不做
// ItemIsMovable（默认拖拽会绕过命令栈，是 undo 架构的头号旁路）。
// ════════════════════════════════════════════════════════════════════════
#include <QGraphicsObject>
#include "canvas/ShapeMeta.h"

namespace cd {

class ShapeItem : public QGraphicsObject
{
    Q_OBJECT
public:
    enum { Type = UserType + 101 };

    explicit ShapeItem(ShapeId id, const ShapeMeta& meta, QGraphicsItem* parent = nullptr);

    int type() const override { return Type; }

    ShapeId id() const { return id_; }
    const ShapeMeta& meta() const { return meta_; }
    void setMeta(const ShapeMeta& meta);      // 唯一的状态写入口

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* opt, QWidget* w) override;

    // 右下角缩放手柄（仅单选时由 View 命中检测；绘制在选中态出现）
    QRectF handleRectLocal() const;
    QRectF handleRectScene() const { return mapToScene(handleRectLocal()).boundingRect(); }

private:
    QPainterPath kindPath() const;            // 按 kind 构造路径（局部坐标）

    ShapeId id_;
    ShapeMeta meta_;
};

} // namespace cd
