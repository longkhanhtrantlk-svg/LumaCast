#include "InspectorPanel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>

namespace lc {

static QDoubleSpinBox* spin(double lo, double hi, double step, double v, int dec = 2) {
    auto* s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setDecimals(dec);
    s->setValue(v);
    return s;
}

InspectorPanel::InspectorPanel(Project& project, RenderEngine& engine, QWidget* parent)
    : QTabWidget(parent), project_(project), engine_(engine) {
    for (const auto& s : project_.scenes) layerCache_.push_back(s->layers);
    for (int i = 0; i < engine_.outputCount(); ++i) outCache_.push_back(engine_.output(i)->config());
    addTab(buildSceneTab(), "Scene / Layer");
    addTab(buildOutputTab(), "Output");
}

// ---------------- Scene / Layer ----------------

QWidget* InspectorPanel::buildSceneTab() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);

    sceneCombo_ = new QComboBox;
    v->addWidget(sceneCombo_);

    layerHost_ = new QWidget;
    layerRows_ = new QVBoxLayout(layerHost_);
    layerRows_->setAlignment(Qt::AlignTop);
    auto* sa = new QScrollArea;
    sa->setWidgetResizable(true);
    sa->setWidget(layerHost_);
    v->addWidget(sa, 1);

    auto* add = new QHBoxLayout;
    addSrcCombo_ = new QComboBox;
    auto* addB = new QPushButton("Them layer");
    add->addWidget(addSrcCombo_, 1);
    add->addWidget(addB);
    v->addLayout(add);

    connect(sceneCombo_, &QComboBox::currentIndexChanged, this, [this] { rebuildLayerRows(); });
    connect(addB, &QPushButton::clicked, this, [this] {
        const int si = sceneCombo_->currentIndex(), k = addSrcCombo_->currentIndex();
        if (si < 0 || k < 0) return;
        Layer l;
        l.source = project_.sources[k].source;
        layerCache_[si].push_back(l);
        pushLayers(si);
        rebuildLayerRows();
    });
    refreshFromProject();
    return page;
}

void InspectorPanel::refreshFromProject() {
    while (layerCache_.size() < project_.scenes.size()) layerCache_.push_back(project_.scenes[layerCache_.size()]->layers);
    const int keep = sceneCombo_->currentIndex();
    sceneCombo_->blockSignals(true);
    sceneCombo_->clear();
    for (const auto& s : project_.scenes)
        sceneCombo_->addItem(QString("%1  (%2x%3)").arg(s->name).arg(s->size.width()).arg(s->size.height()));
    sceneCombo_->setCurrentIndex(keep >= 0 && keep < sceneCombo_->count() ? keep : (sceneCombo_->count() ? 0 : -1));
    sceneCombo_->blockSignals(false);
    addSrcCombo_->clear();
    for (const auto& d : project_.sources)
        addSrcCombo_->addItem(QString("%1 [%2]").arg(d.source ? d.source->name() : d.id, d.type));
    rebuildLayerRows();
}

void InspectorPanel::pushLayers(int si) {
    auto scene = project_.scenes[si];
    auto layers = layerCache_[si];
    engine_.post([scene, layers](double) { scene->layers = layers; });
}

void InspectorPanel::rebuildLayerRows() {
    while (QLayoutItem* it = layerRows_->takeAt(0)) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    const int si = sceneCombo_->currentIndex();
    if (si < 0 || si >= int(layerCache_.size())) return;
    const auto& layers = layerCache_[si];
    for (int li = 0; li < int(layers.size()); ++li) {
        const Layer& l = layers[li];
        auto* row = new QGroupBox(QString("%1. %2").arg(li + 1).arg(l.source ? l.source->name() : "?"));
        auto* g = new QGridLayout(row);

        auto* vis = new QCheckBox("Hien");
        vis->setChecked(l.visible);
        connect(vis, &QCheckBox::toggled, this, [this, si, li](bool b) { layerCache_[si][li].visible = b; pushLayers(si); });
        g->addWidget(vis, 0, 0);

        const char* names[4] = {"x", "y", "w", "h"};
        const double vals[4] = {l.rect.x(), l.rect.y(), l.rect.width(), l.rect.height()};
        for (int k = 0; k < 4; ++k) {
            auto* sp = spin(-4, 8, 0.01, vals[k]);
            sp->setPrefix(QString(names[k]) + " ");
            connect(sp, &QDoubleSpinBox::valueChanged, this, [this, si, li, k](double v) {
                QRectF& r = layerCache_[si][li].rect;
                if (k == 0) r.moveLeft(v); else if (k == 1) r.moveTop(v);
                else if (k == 2) r.setWidth(v); else r.setHeight(v);
                pushLayers(si);
            });
            g->addWidget(sp, 0, 1 + k);
        }
        auto* op = spin(0, 1, 0.05, l.opacity);
        op->setPrefix("alpha ");
        connect(op, &QDoubleSpinBox::valueChanged, this, [this, si, li](double v) {
            layerCache_[si][li].opacity = float(v); pushLayers(si);
        });
        g->addWidget(op, 1, 0);

        auto* fit = new QComboBox;
        fit->addItems({"Stretch", "Fit", "Fill"});
        fit->setCurrentIndex(int(l.fit));
        connect(fit, &QComboBox::currentIndexChanged, this, [this, si, li](int i) {
            layerCache_[si][li].fit = LayerFit(i); pushLayers(si);
        });
        g->addWidget(fit, 1, 1);

        auto* up = new QPushButton("Len"), * down = new QPushButton("Xuong"), * del = new QPushButton("Xoa");
        g->addWidget(up, 1, 2);
        g->addWidget(down, 1, 3);
        g->addWidget(del, 1, 4);
        auto reorder = [this, si](int from, int to) {
            auto& v = layerCache_[si];
            if (to < 0 || to >= int(v.size())) return;
            std::swap(v[from], v[to]);
            pushLayers(si);
            QTimer::singleShot(0, this, [this] { rebuildLayerRows(); });   // khong xoa widget dang xu ly click
        };
        connect(up, &QPushButton::clicked, this, [=] { reorder(li, li - 1); });
        connect(down, &QPushButton::clicked, this, [=] { reorder(li, li + 1); });
        connect(del, &QPushButton::clicked, this, [this, si, li] {
            layerCache_[si].erase(layerCache_[si].begin() + li);
            pushLayers(si);
            QTimer::singleShot(0, this, [this] { rebuildLayerRows(); });
        });
        layerRows_->addWidget(row);
    }
}

// ---------------- Output ----------------

void InspectorPanel::pushOutput(int i) {
    OutputSink* sink = engine_.output(i);
    OutputConfig cfg = outCache_[i];
    engine_.post([sink, cfg](double) { sink->mutableConfig() = cfg; });
}

QWidget* InspectorPanel::buildOutputTab() {
    auto* host = new QWidget;
    auto* root = new QVBoxLayout(host);
    for (int i = 0; i < engine_.outputCount(); ++i) {
        const OutputConfig& c = outCache_[i];
        auto* box = new QGroupBox(QString("%1  (%2x%3, kenh %4)").arg(c.name).arg(c.size.width()).arg(c.size.height()).arg(c.channel + 1));
        auto* g = new QGridLayout(box);

        auto* scale = new QComboBox;
        scale->addItems({"Fit", "Fill", "Stretch", "Pixel 1:1"});
        scale->setCurrentIndex(int(c.scale));
        connect(scale, &QComboBox::currentIndexChanged, this, [this, i](int k) { outCache_[i].scale = ScaleMode(k); pushOutput(i); });
        g->addWidget(scale, 0, 0);

        auto* rot = new QComboBox;
        rot->addItems({"Xoay 0", "Xoay 90", "Xoay 180", "Xoay 270"});
        rot->setCurrentIndex(c.rotation / 90);
        connect(rot, &QComboBox::currentIndexChanged, this, [this, i](int k) { outCache_[i].rotation = k * 90; pushOutput(i); });
        g->addWidget(rot, 0, 1);

        const char* cn[4] = {"crop x", "y", "w", "h"};
        const double cv[4] = {c.crop.x(), c.crop.y(), c.crop.width(), c.crop.height()};
        for (int k = 0; k < 4; ++k) {
            auto* sp = spin(0, 1, 0.01, cv[k], 3);
            sp->setPrefix(QString(cn[k]) + " ");
            connect(sp, &QDoubleSpinBox::valueChanged, this, [this, i, k](double v) {
                QRectF& r = outCache_[i].crop;
                if (k == 0) r.moveLeft(v); else if (k == 1) r.moveTop(v);
                else if (k == 2) r.setWidth(std::max(0.001, v)); else r.setHeight(std::max(0.001, v));
                pushOutput(i);
            });
            g->addWidget(sp, 1 + k / 2, k % 2);
        }

        struct Item { const char* label; double lo, hi, step; float Calibration::*field; };
        const Item items[] = {
            {"sang", 0, 2, 0.05, &Calibration::brightness}, {"tuong phan", 0, 3, 0.05, &Calibration::contrast},
            {"gamma", 0.2, 4, 0.05, &Calibration::gamma}, {"R", 0, 2, 0.02, &Calibration::gainR},
            {"G", 0, 2, 0.02, &Calibration::gainG}, {"B", 0, 2, 0.02, &Calibration::gainB}};
        int col = 0;
        for (const Item& it : items) {
            auto* sp = spin(it.lo, it.hi, it.step, c.cal.*(it.field));
            sp->setPrefix(QString(it.label) + " ");
            auto field = it.field;
            connect(sp, &QDoubleSpinBox::valueChanged, this, [this, i, field](double v) {
                outCache_[i].cal.*field = float(v); pushOutput(i);
            });
            g->addWidget(sp, 3 + col / 2, col % 2);
            ++col;
        }
        root->addWidget(box);
    }
    root->addStretch();
    auto* sa = new QScrollArea;
    sa->setWidgetResizable(true);
    sa->setWidget(host);
    return sa;
}

} // namespace lc
