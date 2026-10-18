#pragma once
// ════════════════════════════════════════════════════════════════════════
// CdCommandId —— demo 业务命令的类型 id 注册表
//
// 框架保留 0..99（ur::kCmdCompound=0 / kCmdInvalid=-1），业务从 100 起。
// id 是合并策略的分派键 + 历史面板分组依据（GIMP 的 GimpUndoType 思路：
// 字符串 text 不足以做策略，类型系统才有表达力）。
// ════════════════════════════════════════════════════════════════════════

#include <QList>
#include <algorithm>
#include <string>
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

// mergeTag 工具：目标 id 集合 → 确定性字符串（core 的 mergeTag 是
// std::string，这里纯数字拼接不需要 Qt）。
// tag 相同 = "同一批目标的同一段连续操作"，是合并的必要条件；
// 选中集合一变 tag 就变 → 合并被阻断（极性/换目标防线，设计精讲 §7）。
inline std::string idsTag(const QList<ShapeId>& ids)
{
    QList<ShapeId> sorted = ids;
    std::sort(sorted.begin(), sorted.end());
    std::string out;
    for (ShapeId id : sorted) {
        if (!out.empty())
            out += ':';
        out += std::to_string(id);
    }
    return out;
}

} // namespace cd
