#pragma once
// ════════════════════════════════════════════════════════════════════════
// CanvasScene —— 画布领域层：id 注册表 + 状态读写唯一门面
//
// 对 undo 架构的意义：命令不直接摸 QGraphicsScene，一切对象生命周期
// 与状态读写都经过这里 —— createShape / createShapeWithId / removeShape /
// setShapeMeta / findShape。这个"收口点"就是目标安全纪律的执行处：
//   · findShape(id) 找不到 → 命令按各自规则 no-op（悬垂不可能）
//   · createShapeWithId 复用原 id → RemoveShapes 的 undo 重建后，
//     历史里更早的命令仍按 id 找得到对象
// 选择集也在这里收口（selectedIds/selectIds），供栈的
// SelectionProvider/Applier 接线。
// ════════════════════════════════════════════════════════════════════════
#include <QGraphicsScene>
#include <QHash>
#include <QList>
#include "canvas/ShapeMeta.h"

namespace cd {

class ShapeItem;

class CanvasScene : public QGraphicsScene
{
    Q_OBJECT
public:
    explicit CanvasScene(QObject* parent = nullptr);

    // ── 对象生命周期（全部命令的必经之路）──
    ShapeId   createShape(const ShapeMeta& meta);                 // 分配新 id
    ShapeItem* createShapeWithId(ShapeId id, const ShapeMeta& m); // undo 重建复用原 id
    void      removeShape(ShapeId id);                            // 摘除并销毁
    ShapeItem* findShape(ShapeId id) const;                       // 找不到返回 nullptr
    bool      getMeta(ShapeId id, ShapeMeta* out) const;
    void      setShapeMeta(ShapeId id, const ShapeMeta& meta);    // 状态写入口

    QList<ShapeId> allIds() const;                                // 按 z 升序
    int  shapeCount() const { return shapes_.size(); }

    // ── z 序与分组辅助 ──
    qreal nextTopZ() const;
    qreal nextBottomZ() const;
    ShapeId nextGroupId();
    QList<ShapeId> groupMembers(ShapeId groupId) const;

    // ── 选择集收口（栈的 provider/applier 对接点）──
    QList<ShapeId> selectedIds() const;
    void selectIds(const QList<ShapeId>& ids);

protected:
    void drawBackground(QPainter* p, const QRectF& rect) override;   // 点阵网格

private:
    void registerItem(ShapeId id, ShapeItem* item);

    QHash<ShapeId, ShapeItem*> shapes_;
    ShapeId nextId_ = 1;
    ShapeId nextGroup_ = 1;
};

} // namespace cd
