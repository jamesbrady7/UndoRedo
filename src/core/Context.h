#pragma once
// ════════════════════════════════════════════════════════════════════════
// Context —— Eclipse IUndoContext 式的上下文标签
//
// 每条命令可携带多个标签（"canvas"、"inspector"……），栈支持按标签
// 过滤出可见子序列做 undo（CommandStack::undoIn）。这是与 QUndoStack
// 差异最大的特性：Qt 的栈是全局单序列，"我只想撤销属性面板的改动"
// 这类需求在 Qt 里无解；Eclipse IOperationHistory 用 context 交集 +
// 向前搜索解决，本框架照搬该语义（代价与边界见 docs/设计精讲.md §8）。
//
// 纯 C++17 零依赖：标签是 std::string，集合是 unordered_set（只做
// 相交/并集判定，顺序无关——哈希容器正合适）。
// ════════════════════════════════════════════════════════════════════════
#include <string>
#include <unordered_set>

namespace ur {

using ContextSet = std::unordered_set<std::string>;

// C++17 没有 unordered_set::contains（C++20 才有）——统一的相交判定入口，
// core 内部与调用方都用它，避免各处手写 find()!=end() 或误用 count。
inline bool containsCtx(const ContextSet& set, const std::string& key)
{
    return set.find(key) != set.end();
}

// 内置 context 名（demo 的两个通道；业务可自行扩展字符串标签）
inline const std::string kCtxCanvas    = "canvas";     // 画布交互：增删/移动/缩放/结构
inline const std::string kCtxInspector = "inspector";  // 属性面板：颜色/描边

} // namespace ur
