#include "app/SelfTest.h"

#include <QByteArray>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/MainWindow.h"
#include "canvas/CanvasScene.h"
#include "commands/EditCommands.h"
#include "commands/GeometryCommands.h"
#include "commands/PropertyCommands.h"
#include "commands/StructureCommands.h"
#include "core/CommandFactory.h"
#include "core/CommandStack.h"
#include "core/Context.h"
#include "core/TransactionScope.h"

// ── 断言助手（同 unit_tests 约定：ASCII 符号 + 失败计数）────────────────
namespace {
int g_fail = 0;
int g_ok = 0;
void checkOk(bool cond, const char* what)
{
    if (cond) {
        printf("  [OK]   %s\n", what);
        ++g_ok;
    } else {
        printf("  [FAIL] %s\n", what);
        ++g_fail;
    }
}
int64_t g_fakeNow = 1000000;
int64_t fakeClock() { return g_fakeNow; }
} // namespace

namespace cd {

void registerDemoPayloads()
{
    // creator 只认"类型 + 上下文"，不认实例：注册表可以在场景存在之前就绪，
    // 也不会因为某个窗口销毁而留下悬垂捕获（见 CommandFactory.h 的 ★ 段）。
    ur::CommandFactory::instance().registerType("cd.addshape",
        [](const std::string& data, ur::CommandContext& ctx)
            -> std::unique_ptr<ur::Command> {
            auto* c = dynamic_cast<CanvasCommandContext*>(&ctx);
            if (!c || !c->scene)
                return nullptr;         // 上下文不对 = 优雅降级（同"未注册类型"）
            return AddShapeCommand::fromPayload(c->scene, data);
        });
}

// ════════════════════════════════════════════════════════════════════════
int runCanvasSelfTest()
{
    // 无缓冲：崩溃/断言失败时已打印的进度不会随缓冲一起丢掉（输出被重定向到
    // 文件时 stdout 是块缓冲，abort 一次就能把整份日志吞掉，排障时最恨这个）。
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("UndoRedoDemo canvas selftest —— demo 层集成电池(offscreen)\n");
    printf("(真实 MainWindow + 真实命令; 退出码 = 失败断言数)\n\n");

    auto w = std::make_unique<MainWindow>();   // 不 show()；offscreen 平台下安全
    CanvasScene* scene = w->scene();
    ur::CommandStack* stack = w->stack();
    stack->setClock(fakeClock);         // 假时钟：时间窗合并可确定性驱动
    registerDemoPayloads();

    // ── S1 增删全遍历：undo 到底 → redo 回来，元数据逐一还原 ──
    printf("== S1 增/删全遍历 + 元数据还原 ==\n");
    w->addShape(ShapeKind::Rect);
    w->addShape(ShapeKind::Ellipse);
    w->addShape(ShapeKind::Triangle);
    checkOk(scene->shapeCount() == 3 && stack->count() == 3, "3 图形 3 历史(添加不合并)");
    QList<ShapeId> ids = scene->allIds();
    QHash<ShapeId, ShapeMeta> baseline;
    for (ShapeId id : ids) {
        ShapeMeta m;
        scene->getMeta(id, &m);
        baseline.insert(id, m);
    }
    while (stack->canUndo())
        stack->undo();
    checkOk(scene->shapeCount() == 0, "undo 到底: 场景清空");
    while (stack->canRedo())
        stack->redo();
    bool metasEqual = scene->shapeCount() == 3;
    for (ShapeId id : ids) {
        ShapeMeta m;
        metasEqual = metasEqual && scene->getMeta(id, &m) && m == baseline[id];
    }
    checkOk(metasEqual, "redo 回来: 按原 id 重建且元数据逐一还原");

    // ── S2 拖拽合并：时间窗内 10 连发折叠成一条 ──
    printf("== S2 拖拽合并(TimeWindow + 手势号) ==\n");
    scene->selectIds(scene->allIds());
    const int cntBefore = stack->count();
    const ShapeId probe = ids.first();
    ShapeMeta pm;
    scene->getMeta(probe, &pm);
    const QPointF startPos = pm.pos;
    for (int i = 0; i < 10; ++i) {
        QHash<ShapeId, QPointF> from, to;
        for (ShapeId id : scene->selectedIds()) {
            ShapeMeta m;
            if (!scene->getMeta(id, &m))
                continue;
            from.insert(id, m.pos);
            to.insert(id, m.pos + QPointF(5, 0));
        }
        g_fakeNow += 40;                // 40ms/步：全部落在 500ms 窗口内
        stack->push(std::make_unique<MoveShapesCommand>(scene, from, to, 77));
    }
    checkOk(stack->count() == cntBefore + 1, "10 连发折叠成 1 条历史");
    scene->getMeta(probe, &pm);
    checkOk(qFuzzyCompare(pm.pos.x(), startPos.x() + 50), "净位移 = 10 x 5 = 50");
    stack->undo();
    scene->getMeta(probe, &pm);
    checkOk(pm.pos == startPos, "undo 一步回到整段拖拽起点");
    stack->redo();
    scene->getMeta(probe, &pm);
    checkOk(qFuzzyCompare(pm.pos.x(), startPos.x() + 50), "redo 一步重放整段");
    g_fakeNow += 5000;                  // 跨窗：下一次拖拽另起一条

    // ── S3 选择快照：删除 → undo → 对象与选择集一起回来 ──
    printf("== S3 删除 undo: 原 id 重建 + 选择集恢复 ==\n");
    scene->selectIds({probe});
    w->deleteSelected();
    checkOk(scene->shapeCount() == 2 && scene->findShape(probe) == nullptr, "删除生效");
    stack->undo();
    checkOk(scene->findShape(probe) != nullptr, "undo 按原 id 重建(老命令重放仍可达)");
    checkOk(scene->selectedIds() == QList<ShapeId>{probe}, "选择集随 undo 恢复(selBefore)");

    // ── S4 事务：左对齐多条移动 → 一条 Compound ──
    printf("== S4 TransactionScope: 左对齐折叠成一条 ==\n");
    scene->selectIds(scene->allIds());
    // 注意基准取【游标】而非 count：S3 以 undo 收尾，redo 段还挂着一条"删除"。
    // 新命令 push 按线性历史语义先截断 redo 段再入账 —— count 净变化为 0，
    // 游标 +1 才是"事务折叠成一条"的正确断言（这个细节本身就是被测行为）。
    const int idxS4 = stack->index();
    QList<qreal> xsBefore;
    for (ShapeId id : scene->selectedIds()) {
        ShapeMeta m;
        scene->getMeta(id, &m);
        xsBefore.push_back(m.pos.x());
    }
    w->alignLeft();
    checkOk(stack->index() == idxS4 + 1 && stack->count() == idxS4 + 1,
            "多条移动折叠成一条事务历史(并按线性语义截断 redo 段)");
    qreal minX = 1e9;
    bool allAligned = true;
    for (ShapeId id : scene->allIds()) {
        ShapeMeta m;
        scene->getMeta(id, &m);
        minX = qMin(minX, m.pos.x());
    }
    for (ShapeId id : scene->allIds()) {
        ShapeMeta m;
        scene->getMeta(id, &m);
        allAligned = allAligned && qFuzzyCompare(m.pos.x() + 1.0, minX + 1.0);
    }
    checkOk(allAligned, "对齐生效: 全部 x == minX");
    stack->undo();
    bool restored = true;
    const QList<ShapeId> sortedIds = scene->allIds();
    QList<qreal> xsAfter;
    for (ShapeId id : sortedIds) {
        ShapeMeta m;
        scene->getMeta(id, &m);
        xsAfter.push_back(m.pos.x());
    }
    QList<qreal> xsSorted = xsBefore;
    std::sort(xsSorted.begin(), xsSorted.end());
    std::sort(xsAfter.begin(), xsAfter.end());
    for (int i = 0; i < xsSorted.size() && i < xsAfter.size(); ++i)
        restored = restored && qFuzzyCompare(xsSorted[i] + 1.0, xsAfter[i] + 1.0);
    checkOk(restored, "事务 undo: 一步还原全部 x");

    // ── S5 异常 abort：事务内抛异常 → 净零 ──
    printf("== S5 事务异常 abort: 净零 ==\n");
    const int cntS5 = stack->count();
    const int shapesS5 = scene->shapeCount();
    try {
        ur::TransactionScope tx(stack, "会中止的操作");
        ShapeMeta ghost;
        ghost.name = QStringLiteral("幽灵");
        ghost.pos = QPointF(-300, -300);
        stack->push(std::make_unique<AddShapeCommand>(scene, ghost));
        throw std::runtime_error("模拟业务异常");
    } catch (...) {
    }
    checkOk(scene->shapeCount() == shapesS5, "已执行的添加被回滚");
    checkOk(stack->count() == cntS5, "中止事务不留历史");

    // ── S6 context 子序列撤销：只挖属性改动，画布净效果不变 ──
    printf("== S6 undoIn(inspector): 精准挖除 + 重放不变式 ==\n");
    {
        scene->selectIds({probe});
        ShapeMeta m0;
        scene->getMeta(probe, &m0);
        const QColor fill0 = m0.fill;

        QHash<ShapeId, QPointF> from{{probe, m0.pos}}, to{{probe, m0.pos + QPointF(7, 0)}};
        stack->push(std::make_unique<MoveShapesCommand>(scene, from, to, 88));
        const QPointF posAfterMove = to.value(probe);

        QHash<ShapeId, QColor> cfrom{{probe, fill0}};
        stack->push(std::make_unique<SetFillCommand>(scene, QList<ShapeId>{probe},
                                                     cfrom, QColor(Qt::red)));
        stack->push(std::make_unique<MoveShapesCommand>(
            scene, QHash<ShapeId, QPointF>{{probe, posAfterMove}},
            QHash<ShapeId, QPointF>{{probe, posAfterMove + QPointF(3, 0)}}, 89));

        const int idxBefore = stack->index();
        checkOk(stack->undoIn({ur::kCtxInspector}), "undoIn(inspector) 命中");
        ShapeMeta m1;
        scene->getMeta(probe, &m1);
        checkOk(m1.fill == fill0, "填充色回滚到改动前");
        checkOk(m1.pos == posAfterMove + QPointF(3, 0), "画布通道净效果不变(重放)");
        checkOk(stack->index() == idxBefore, "游标不动(线性历史完好)");
    }

    // ── S7 历史遍历 + setIndex 任意跳转 ──
    printf("== S7 history() 遍历 + setIndex 跳转 ==\n");
    {
        const std::vector<ur::CommandStack::Entry>& h = stack->history();
        checkOk(int(h.size()) == stack->count(), "history() 与栈等长");
        bool entriesSane = !h.empty();
        for (const auto& e : h)
            entriesSane = entriesSane && !e.text.empty() && e.bytes > 0;
        checkOk(entriesSane, "每条 Entry 有文本/字节(面板与预算的数据源)");
        const int full = stack->count();
        stack->setIndex(0);
        checkOk(scene->shapeCount() == 0, "setIndex(0): 全部添加被撤销");
        stack->setIndex(full);
        checkOk(scene->shapeCount() == 3, "setIndex(count): 全部重放回来, 无崩溃");
    }

    // ── S8 clean 检查点 ──
    printf("== S8 clean 检查点(保存语义) ==\n");
    {
        stack->setClean();
        checkOk(stack->isClean(), "setClean 即 isClean");
        w->addShape(ShapeKind::Rect);
        checkOk(!stack->isClean(), "新命令 -> dirty");
        stack->undo();
        checkOk(stack->isClean() && scene->shapeCount() == 3, "undo 回检查点 -> clean");
    }

    // ── S9 目标安全：目标被删后子序列撤销不崩、不误建 ──
    printf("== S9 目标安全: 对象已删时 undoIn 优雅降级 ==\n");
    {
        const ShapeId victim = scene->allIds().last();
        ShapeMeta vm;
        scene->getMeta(victim, &vm);
        stack->push(std::make_unique<SetFillCommand>(
            scene, QList<ShapeId>{victim},
            QHash<ShapeId, QColor>{{victim, vm.fill}}, QColor(Qt::green)));
        stack->push(std::make_unique<RemoveShapesCommand>(scene, QList<ShapeId>{victim}));
        checkOk(scene->shapeCount() == 2, "victim 已删除");
        const bool hit = stack->undoIn({ur::kCtxInspector});
        checkOk(hit, "向前搜索命中已删目标的改色命令");
        checkOk(scene->findShape(victim) == nullptr, "undo no-op + 重放后仍保持删除(不崩不误建)");
        checkOk(scene->shapeCount() == 2, "场景计数不变");
    }

    // ── S10 payload 往返：AddShape 序列化 -> 工厂重建 -> 回放等价 ──
    printf("== S10 payload 往返(cd.addshape) ==\n");
    {
        ShapeMeta m;
        m.kind = ShapeKind::Rect;
        m.name = QStringLiteral("序列化验证");
        m.pos = QPointF(10, 20);
        m.size = QSizeF(90, 60);
        m.fill = QColor(0x33, 0xaa, 0x66);
        m.z = scene->nextTopZ();
        AddShapeCommand original(scene, m);
        const ur::Payload doc = ur::CommandFactory::toPayloadDoc(original);
        checkOk(doc.type == "cd.addshape" && !doc.data.empty(), "toPayloadDoc 产出 {type,data}");
        // 外层文档 {"type":…,"data":{…}} 的拼装是【demo 层】的格式知识（core 不认识
        // JSON）。⚠ data 是内嵌 JSON 文本：必须先解回对象再塞进外层——当字符串
        // 直接塞会被双重转义，读回时 fromJson 拿到的是 string 不是 object。
        QJsonObject envelope;
        envelope.insert(QStringLiteral("type"), QString::fromStdString(doc.type));
        envelope.insert(QStringLiteral("data"),
                        QJsonDocument::fromJson(QByteArray::fromStdString(doc.data)).object());
        // JSON 文本往返（模拟落盘/网络）
        const QByteArray json = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
        // 拆回 Payload → 工厂重建（type 查注册表，data 原样透传给 creator）
        const QJsonObject reparsed = QJsonDocument::fromJson(json).object();
        const ur::Payload wire{
            reparsed.value(QStringLiteral("type")).toString().toStdString(),
            QString::fromUtf8(QJsonDocument(reparsed.value(QStringLiteral("data")).toObject())
                                  .toJson(QJsonDocument::Compact)).toStdString()
        };
        // 重建上下文：告诉 creator"这次回放落在哪个画布上"
        CanvasCommandContext ctx(scene);
        auto rebuilt = ur::CommandFactory::instance().fromPayload(wire, ctx);
        checkOk(rebuilt != nullptr, "工厂按类型名重建 AddShapeCommand");
        // 反例：换成基类上下文（类型不匹配）→ 优雅降级为 nullptr，不崩
        ur::CommandContext noCtx;
        checkOk(ur::CommandFactory::instance().fromPayload(wire, noCtx) == nullptr,
                "上下文类型不匹配: 返回 nullptr(与未注册类型同一条路)");
        const int cntBefore = stack->count();
        stack->push(std::move(rebuilt));
        checkOk(scene->shapeCount() == 3 && stack->count() == cntBefore + 1, "重建命令回放生效");
        bool found = false;
        for (ShapeId id : scene->allIds()) {
            ShapeMeta got;
            if (scene->getMeta(id, &got) && got.name == m.name)
                found = got.pos == m.pos && got.size == m.size && got.fill == m.fill;
        }
        checkOk(found, "回放后的图形与原 payload 完全一致");
        stack->undo();
        checkOk(scene->shapeCount() == 2, "重建命令的 undo 同样正确");
    }

    // ── S11 选择集差量：同集合重复设置 = 零 selectionChanged ──
    printf("== S11 selectIds 差量: 同集合重复设置不通知 ==\n");
    {
        const QList<ShapeId> keep = scene->allIds();
        scene->selectIds(keep);                     // 先落到目标集合
        int selSignals = 0;   // 注意: 不能叫 signals —— Qt 把它 #define 成 public
        const auto conn = QObject::connect(scene, &QGraphicsScene::selectionChanged,
                                           [&selSignals] { ++selSignals; });
        scene->selectIds(keep);                     // 同一集合再来一次（undo/redo 恢复快照的常态）
        checkOk(selSignals == 0, "同集合重复 selectIds: 零 selectionChanged(属性面板不白刷)");
        scene->selectIds({keep.first()});           // 真的变了
        checkOk(selSignals >= 1, "集合真的变化: 有通知");
        // 故意保持"有选中"的状态退场：teardown 顺序错了就会在这里现形
        scene->selectIds(keep);
        QObject::disconnect(conn);
    }

    printf("\n== 汇总: %d OK, %d FAIL ==\n", g_ok, g_fail);
    // 显式先销毁窗口：把"UI/栈/场景的析构顺序"也纳入回归（崩溃会在这里现形，
    // 而不是拖到进程退出的静态析构里，连日志都留不下）。
    w.reset();
    printf("== teardown: MainWindow 析构完成 ==\n");
    printf("== canvas selftest %s ==\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail;
}

} // namespace cd
