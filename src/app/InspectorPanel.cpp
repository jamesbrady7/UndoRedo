#include "app/InspectorPanel.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include "canvas/CanvasScene.h"
#include "commands/PropertyCommands.h"
#include "core/CommandStack.h"

namespace cd {

InspectorPanel::InspectorPanel(CanvasScene* scene, ur::CommandStack* stack, QWidget* parent)
    : QWidget(parent)
    , scene_(scene)
    , stack_(stack)
{
    auto* root = new QVBoxLayout(this);

    title_ = new QLabel(QStringLiteral("未选中图形"), this);
    QFont f = title_->font();
    f.setBold(true);
    title_->setFont(f);
    root->addWidget(title_);

    // ── 填充色相滑条（连续 push + 时间窗合并的主角）──
    root->addWidget(new QLabel(QStringLiteral("填充色相"), this));
    hue_ = new QSlider(Qt::Horizontal, this);
    hue_->setRange(0, 359);
    connect(hue_, &QSlider::valueChanged, this, &InspectorPanel::onHueSlider);
    root->addWidget(hue_);

    fillBtn_ = new QPushButton(QStringLiteral("调色板…（一次性命令）"), this);
    connect(fillBtn_, &QPushButton::clicked, this, &InspectorPanel::onFillButton);
    root->addWidget(fillBtn_);

    // ── 描边宽度滑条 ──
    root->addWidget(new QLabel(QStringLiteral("描边宽度"), this));
    stroke_ = new QSlider(Qt::Horizontal, this);
    stroke_->setRange(1, 20);
    connect(stroke_, &QSlider::valueChanged, this, &InspectorPanel::onStrokeSlider);
    root->addWidget(stroke_);

    hint_ = new QLabel(
        QStringLiteral("拖动滑条：每个刻度 push 一条命令，\n"
                       "500ms 时间窗内自动折叠成一条历史。\n"
                       "试试历史面板的「只撤销属性改动」。"), this);
    hint_->setStyleSheet(QStringLiteral("color:#666;"));
    root->addWidget(hint_);
    root->addStretch();

    if (scene_)
        connect(scene_, &QGraphicsScene::selectionChanged,
                this, &InspectorPanel::onSelectionChanged);
    onSelectionChanged();
}

// ── 选择变化 → 程序化刷新 UI（syncing_ 闸门防回流）────────────────────
void InspectorPanel::onSelectionChanged()
{
    if (!scene_)
        return;
    const QList<ShapeId> ids = scene_->selectedIds();
    syncing_ = true;
    if (ids.isEmpty()) {
        title_->setText(QStringLiteral("未选中图形"));
        hue_->setEnabled(false);
        stroke_->setEnabled(false);
        fillBtn_->setEnabled(false);
    } else {
        title_->setText(ids.size() == 1
                        ? QStringLiteral("已选中 1 个图形")
                        : QStringLiteral("已选中 %1 个图形").arg(ids.size()));
        hue_->setEnabled(true);
        stroke_->setEnabled(true);
        fillBtn_->setEnabled(true);
        ShapeMeta m;
        if (scene_->getMeta(ids.first(), &m)) {
            int h, s, v, a;
            m.fill.getHsv(&h, &s, &v, &a);
            hue_->setValue(h < 0 ? 0 : h);
            stroke_->setValue(qRound(m.strokeWidth));
        }
    }
    syncing_ = false;
}

// ── 滑条：每个刻度一条命令（from=当下值, to=新值）─────────────────────
void InspectorPanel::onHueSlider(int value)
{
    if (syncing_ || !scene_ || !stack_)
        return;
    const QList<ShapeId> ids = scene_->selectedIds();
    if (ids.isEmpty())
        return;
    QHash<ShapeId, QColor> from;
    for (ShapeId id : ids) {
        ShapeMeta m;
        if (scene_->getMeta(id, &m))
            from.insert(id, m.fill);        // 当下值 = from（合并链保留首个）
    }
    const QColor to = QColor::fromHsv(value, 210, 225);
    stack_->push(std::make_unique<SetFillCommand>(scene_, ids, from, to));
}

void InspectorPanel::onStrokeSlider(int value)
{
    if (syncing_ || !scene_ || !stack_)
        return;
    const QList<ShapeId> ids = scene_->selectedIds();
    if (ids.isEmpty())
        return;
    QHash<ShapeId, qreal> from;
    for (ShapeId id : ids) {
        ShapeMeta m;
        if (scene_->getMeta(id, &m))
            from.insert(id, m.strokeWidth);
    }
    stack_->push(std::make_unique<SetStrokeWidthCommand>(scene_, ids, from, qreal(value)));
}

// ── 调色板对话框：一次性命令（对话框取消 = 零命令，天然无"空事务"）────
void InspectorPanel::onFillButton()
{
    if (!scene_ || !stack_)
        return;
    const QList<ShapeId> ids = scene_->selectedIds();
    if (ids.isEmpty())
        return;
    ShapeMeta first;
    if (!scene_->getMeta(ids.first(), &first))
        return;
    const QColor chosen = QColorDialog::getColor(first.fill, this,
                                                 QStringLiteral("选择填充色"));
    if (!chosen.isValid())
        return;
    pushFill(chosen);
}

void InspectorPanel::pushFill(const QColor& to)
{
    const QList<ShapeId> ids = scene_->selectedIds();
    QHash<ShapeId, QColor> from;
    for (ShapeId id : ids) {
        ShapeMeta m;
        if (scene_->getMeta(id, &m))
            from.insert(id, m.fill);
    }
    stack_->push(std::make_unique<SetFillCommand>(scene_, ids, from, to));
}

void InspectorPanel::pushStroke(qreal to)
{
    const QList<ShapeId> ids = scene_->selectedIds();
    QHash<ShapeId, qreal> from;
    for (ShapeId id : ids) {
        ShapeMeta m;
        if (scene_->getMeta(id, &m))
            from.insert(id, m.strokeWidth);
    }
    stack_->push(std::make_unique<SetStrokeWidthCommand>(scene_, ids, from, to));
}

} // namespace cd
