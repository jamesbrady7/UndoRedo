#pragma once
// ════════════════════════════════════════════════════════════════════════
// ShapeMeta —— 图形纯数据快照（POD）
//
// "命令只认 id + 快照，不持对象指针"是本框架的目标安全纪律：
// 画布对象会被删除/重建（RemoveShapes 的 undo 就靠快照重建），
// 指针只是对象当下的住处，id 才是身份。命令里存 ShapeMeta，
// 悬垂指针在结构上不可能出现（设计精讲 §9）。
// toJson/fromJson 同时服务 payload 序列化（§10）。
// ════════════════════════════════════════════════════════════════════════
#include <QColor>
#include <QJsonObject>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QtGlobal>

namespace cd {

using ShapeId = quint64;    // 0 = 无效 id

enum class ShapeKind : int {
    Rect = 0,
    Ellipse,
    Triangle,
};

inline QString shapeKindName(ShapeKind k)
{
    switch (k) {
    case ShapeKind::Rect:     return QStringLiteral("矩形");
    case ShapeKind::Ellipse:  return QStringLiteral("椭圆");
    case ShapeKind::Triangle: return QStringLiteral("三角形");
    }
    return QStringLiteral("图形");
}

struct ShapeMeta {
    ShapeKind kind = ShapeKind::Rect;
    QPointF   pos;                          // 场景坐标（左上角）
    QSizeF    size{120.0, 80.0};
    QColor    fill{0x4f, 0x8c, 0xd9};       // 默认蓝
    QColor    stroke{0x2b, 0x2b, 0x33};
    qreal     strokeWidth = 2.0;
    qreal     z = 0.0;
    ShapeId   groupId = 0;                  // 0 = 未分组
    QString   name;                         // "矩形 3" 等展示名

    QJsonObject toJson() const
    {
        QJsonObject o;
        o.insert(QStringLiteral("kind"), int(kind));
        o.insert(QStringLiteral("x"), pos.x());
        o.insert(QStringLiteral("y"), pos.y());
        o.insert(QStringLiteral("w"), size.width());
        o.insert(QStringLiteral("h"), size.height());
        o.insert(QStringLiteral("fill"), fill.name());
        o.insert(QStringLiteral("stroke"), stroke.name());
        o.insert(QStringLiteral("strokeWidth"), strokeWidth);
        o.insert(QStringLiteral("z"), z);
        o.insert(QStringLiteral("groupId"), double(groupId));   // quint64 走 double 通道
        o.insert(QStringLiteral("name"), name);
        return o;
    }

    static ShapeMeta fromJson(const QJsonObject& o)
    {
        ShapeMeta m;
        m.kind = ShapeKind(o.value(QStringLiteral("kind")).toInt(0));
        m.pos = QPointF(o.value(QStringLiteral("x")).toDouble(),
                        o.value(QStringLiteral("y")).toDouble());
        m.size = QSizeF(o.value(QStringLiteral("w")).toDouble(120.0),
                        o.value(QStringLiteral("h")).toDouble(80.0));
        m.fill = QColor(o.value(QStringLiteral("fill")).toString(QStringLiteral("#4f8cd9")));
        m.stroke = QColor(o.value(QStringLiteral("stroke")).toString(QStringLiteral("#2b2b33")));
        m.strokeWidth = o.value(QStringLiteral("strokeWidth")).toDouble(2.0);
        m.z = o.value(QStringLiteral("z")).toDouble(0.0);
        m.groupId = ShapeId(o.value(QStringLiteral("groupId")).toDouble(0.0));
        m.name = o.value(QStringLiteral("name")).toString();
        return m;
    }

    bool operator==(const ShapeMeta& o) const
    {
        return kind == o.kind && pos == o.pos && size == o.size
               && fill == o.fill && stroke == o.stroke
               && qFuzzyCompare(strokeWidth, o.strokeWidth)
               && qFuzzyCompare(z, o.z) && groupId == o.groupId && name == o.name;
    }
};

} // namespace cd
