#pragma once
// ════════════════════════════════════════════════════════════════════════
// CdCommandId —— demo 业务命令的类型 id 注册表
//
// 框架保留 0..99（ur::kCmdCompound=0 / kCmdInvalid=-1），业务从 100 起。
// id 是合并策略的分派键 + 历史面板分组依据（GIMP 的 GimpUndoType 思路：
// 字符串 text 不足以做策略，类型系统才有表达力）。
// ════════════════════════════════════════════════════════════════════════

#include <QList>
#include <QString>
#include <algorithm>
#include "canvas/ShapeMeta.h"

namespace cd {

enum CdCommandId : int {
    kCdAddShape    = 100,
    kCdRemoveShape = 101,
    kCdMove        = 102,
    kCdResize      = 103,
    kCdSetFill     = 104,
    kCdSetStroke   = 105,
    kCdReorderZ    = 106,
    kCdSetGroup    = 107,
};

// mergeTag 工具：目标 id 集合 → 确定性字符串。
// tag 相同 = "同一批目标的同一段连续操作"，是合并的必要条件；
// 选中集合一变 tag 就变 → 合并被阻断（极性/换目标防线，设计精讲 §7）。
inline QString idsTag(const QList<ShapeId>& ids)
{
    QList<ShapeId> sorted = ids;
    std::sort(sorted.begin(), sorted.end());
    QStringList parts;
    parts.reserve(sorted.size());
    for (ShapeId id : sorted)
        parts << QString::number(id);
    return parts.join(QLatin1Char(':'));
}

} // namespace cd
