#include "InspectorPanel.h"
#include <QAbstractItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>

namespace lc {

static QDoubleSpinBox* spin(double lo, double hi, double step, double v, int dec = 2) {
    auto* s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setDecimals(dec);
    s->setValue(v);
    return s;
}

// ---------------- Undo/Redo ----------------
// Lenh dau tien (redo() khi push) bo qua vi UI da ap dung thay doi roi; cac lan sau moi goi applyXxx.
namespace {

class LayersCmd : public QUndoCommand {
public:
    LayersCmd(InspectorPanel* p, int si, std::vector<Layer> before, std::vector<Layer> after, const QString& text, int mergeId)
        : QUndoCommand(text), panel_(p), si_(si), before_(std::move(before)), after_(std::move(after)), mergeId_(mergeId) {}
    int id() const override { return mergeId_; }
    void undo() override { panel_->applyLayers(si_, before_); }
    void redo() override { if (first_) { first_ = false; return; } panel_->applyLayers(si_, after_); }
    bool mergeWith(const QUndoCommand* other) override {
        if (other->id() != id() || id() < 0) return false;
        after_ = static_cast<const LayersCmd*>(other)->after_;
        return true;
    }
private:
    InspectorPanel* panel_;
    int si_;
    std::vector<Layer> before_, after_;
    int mergeId_;
    bool first_ = true;
};

class OutputCmd : public QUndoCommand {
public:
    OutputCmd(InspectorPanel* p, int i, OutputConfig before, OutputConfig after, const QString& text, int mergeId)
        : QUndoCommand(text), panel_(p), i_(i), before_(std::move(before)), after_(std::move(after)), mergeId_(mergeId) {}
    int id() const override { return mergeId_; }
    void undo() override { panel_->applyOutput(i_, before_); }
    void redo() override { if (first_) { first_ = false; return; } panel_->applyOutput(i_, after_); }
    bool mergeWith(const QUndoCommand* other) override {
        if (other->id() != id() || id() < 0) return false;
        after_ = static_cast<const OutputCmd*>(other)->after_;
        return true;
    }
private:
    InspectorPanel* panel_;
    int i_;
    OutputConfig before_, after_;
    int mergeId_;
    bool first_ = true;
};

} // namespace

InspectorPanel::InspectorPanel(Project& project, RenderEngine& engine, QUndoStack* undo, QWidget* parent)
    : QTabWidget(parent), project_(project), engine_(engine), undo_(undo) {
    for (const auto& s : project_.scenes) layerCache_.push_back(s->layers);
    for (int i = 0; i < engine_.outputCount(); ++i) outCache_.push_back(engine_.output(i)->config());
    addTab(buildSceneTab(), "Scene / Layer");
    addTab(buildOutputTab(), "Output");
}

void InspectorPanel::editLayers(int si, std::vector<Layer> after, const QString& text, int mergeId) {
    auto before = layerCache_[si];
    layerCache_[si] = after;
    pushLayers(si);
    undo_->push(new LayersCmd(this, si, std::move(before), std::move(after), text, mergeId));
}

void InspectorPanel::editOutput(int i, OutputConfig after, const QString& text, int mergeId) {
    OutputConfig before = outCache_[i];
    outCache_[i] = after;
    pushOutput(i);
    undo_->push(new OutputCmd(this, i, before, after, text, mergeId));
}

void InspectorPanel::applyLayers(int si, const std::vector<Layer>& layers) {
    if (si < 0 || si >= int(layerCache_.size())) return;
    layerCache_[si] = layers;
    pushLayers(si);
    if (currentScene() == si) syncLayerList(currentLayerIdx());
}

void InspectorPanel::applyOutput(int i, const OutputConfig& cfg) {
    if (i < 0 || i >= int(outCache_.size())) return;
    outCache_[i] = cfg;
    pushOutput(i);
    syncOutputWidgets(i);
}

// ---------------- Scene / Layer ----------------

int InspectorPanel::currentScene() const { return sceneCombo_ ? sceneCombo_->currentIndex() : -1; }

int InspectorPanel::currentLayerIdx() const {
    const int si = currentScene(), row = layerList_->currentRow();
    if (si < 0 || si >= int(layerCache_.size()) || row < 0) return -1;
    return int(layerCache_[si].size()) - 1 - row;           // dau danh sach = layer tren cung (ve sau cung)
}

QWidget* InspectorPanel::buildSceneTab() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);

    sceneCombo_ = new QComboBox;
    v->addWidget(sceneCombo_);

    layerList_ = new QListWidget;
    layerList_->setObjectName("layerList");
    layerList_->setDragDropMode(QAbstractItemView::InternalMove);
    layerList_->setDefaultDropAction(Qt::MoveAction);
    layerList_->setToolTip("Dau danh sach = layer o tren cung. Keo-tha de doi thu tu, tick de an/hien.");
    v->addWidget(layerList_, 1);

    auto* add = new QHBoxLayout;
    addSrcCombo_ = new QComboBox;
    auto* addB = new QPushButton("Thêm layer");
    auto* upB = new QPushButton("Lên"), * downB = new QPushButton("Xuống"), * delB = new QPushButton("Xoá");
    add->addWidget(addSrcCombo_, 1);
    add->addWidget(addB);
    v->addLayout(add);
    auto* ops = new QHBoxLayout;
    ops->addWidget(upB);
    ops->addWidget(downB);
    ops->addWidget(delB);
    v->addLayout(ops);

    propBox_ = new QGroupBox("Thuộc tính layer đang chọn");
    auto* g = new QGridLayout(propBox_);
    const char* names[4] = {"x ", "y ", "w ", "h "};
    for (int k = 0; k < 4; ++k) {
        rectSpin_[k] = spin(-4, 8, 0.01, 0);
        rectSpin_[k]->setPrefix(names[k]);
        g->addWidget(rectSpin_[k], k / 2, k % 2);
        connect(rectSpin_[k], &QDoubleSpinBox::valueChanged, this, [this, k](double val) {
            const int si = currentScene(), li = currentLayerIdx();
            if (li < 0) return;
            auto after = layerCache_[si];
            QRectF& r = after[li].rect;
            if (k == 0) r.moveLeft(val); else if (k == 1) r.moveTop(val);
            else if (k == 2) r.setWidth(val); else r.setHeight(val);
            editLayers(si, after, "Sửa vị trí/kích thước layer", 1000000 + si * 10000 + li * 100 + k);
        });
    }
    alphaSpin_ = spin(0, 1, 0.05, 1);
    alphaSpin_->setPrefix("alpha ");
    g->addWidget(alphaSpin_, 2, 0);
    connect(alphaSpin_, &QDoubleSpinBox::valueChanged, this, [this](double val) {
        const int si = currentScene(), li = currentLayerIdx();
        if (li < 0) return;
        auto after = layerCache_[si];
        after[li].opacity = float(val);
        editLayers(si, after, "Sửa độ mờ layer", 1000000 + si * 10000 + li * 100 + 4);
    });
    fitCombo_ = new QComboBox;
    fitCombo_->addItems({"Stretch", "Fit", "Fill"});
    g->addWidget(fitCombo_, 2, 1);
    connect(fitCombo_, &QComboBox::currentIndexChanged, this, [this](int i) {
        const int si = currentScene(), li = currentLayerIdx();
        if (li < 0) return;
        auto after = layerCache_[si];
        after[li].fit = LayerFit(i);
        editLayers(si, after, "Đổi kiểu fit layer");
    });
    v->addWidget(propBox_);

    connect(sceneCombo_, &QComboBox::currentIndexChanged, this, [this] { syncLayerList(-1); });
    connect(layerList_, &QListWidget::currentRowChanged, this, [this] { syncProps(); });
    connect(layerList_, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        const int si = currentScene(), li = it->data(Qt::UserRole).toInt();
        if (si < 0 || li < 0 || li >= int(layerCache_[si].size())) return;
        auto after = layerCache_[si];
        after[li].visible = it->checkState() == Qt::Checked;
        editLayers(si, after, after[li].visible ? "Hiện layer" : "Ẩn layer");
    });
    connect(layerList_->model(), &QAbstractItemModel::rowsMoved, this,
            [this] { QTimer::singleShot(0, this, [this] { onLayersReordered(); }); });
    connect(addB, &QPushButton::clicked, this, [this] {
        const int si = currentScene(), k = addSrcCombo_->currentIndex();
        if (si < 0 || k < 0) return;
        auto after = layerCache_[si];
        Layer l;
        l.source = project_.sources[k].source;
        after.push_back(l);
        editLayers(si, after, "Thêm layer");
        syncLayerList(int(after.size()) - 1);
    });
    auto swapLayer = [this](int dir) {
        const int si = currentScene(), li = currentLayerIdx();
        if (li < 0 || li + dir < 0 || li + dir >= int(layerCache_[si].size())) return;
        auto after = layerCache_[si];
        std::swap(after[li], after[li + dir]);
        editLayers(si, after, "Đổi thứ tự layer");
        syncLayerList(li + dir);
    };
    connect(upB, &QPushButton::clicked, this, [swapLayer] { swapLayer(+1); });
    connect(downB, &QPushButton::clicked, this, [swapLayer] { swapLayer(-1); });
    connect(delB, &QPushButton::clicked, this, [this] {
        const int si = currentScene(), li = currentLayerIdx();
        if (li < 0) return;
        auto after = layerCache_[si];
        after.erase(after.begin() + li);
        editLayers(si, after, "Xoá layer");
        syncLayerList(std::min(li, int(after.size()) - 1));
    });
    refreshFromProject();
    return page;
}

void InspectorPanel::refreshFromProject() {
    while (layerCache_.size() < project_.scenes.size()) layerCache_.push_back(project_.scenes[layerCache_.size()]->layers);
    const int keep = sceneCombo_->currentIndex();
    {
        QSignalBlocker b(sceneCombo_);
        sceneCombo_->clear();
        for (const auto& s : project_.scenes)
            sceneCombo_->addItem(QString("%1  (%2x%3)").arg(s->name).arg(s->size.width()).arg(s->size.height()));
        sceneCombo_->setCurrentIndex(keep >= 0 && keep < sceneCombo_->count() ? keep : (sceneCombo_->count() ? 0 : -1));
    }
    addSrcCombo_->clear();
    for (const auto& d : project_.sources)
        addSrcCombo_->addItem(QString("%1 [%2]").arg(d.source ? d.source->name() : d.id, d.type));
    syncLayerList(-1);
}

void InspectorPanel::syncLayerList(int selectIdx) {
    const int si = currentScene();
    {
        QSignalBlocker b(layerList_);
        layerList_->clear();
        if (si >= 0 && si < int(layerCache_.size())) {
            const auto& layers = layerCache_[si];
            const int n = int(layers.size());
            for (int r = 0; r < n; ++r) {
                const int idx = n - 1 - r;
                auto* it = new QListWidgetItem(QString("%1. %2").arg(idx + 1).arg(layers[idx].source ? layers[idx].source->name() : "?"));
                it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
                it->setCheckState(layers[idx].visible ? Qt::Checked : Qt::Unchecked);
                it->setData(Qt::UserRole, idx);
                layerList_->addItem(it);
            }
            if (n > 0) layerList_->setCurrentRow(selectIdx >= 0 && selectIdx < n ? n - 1 - selectIdx : 0);
        }
    }
    syncProps();
}

void InspectorPanel::syncProps() {
    const int si = currentScene(), li = currentLayerIdx();
    const bool ok = li >= 0;
    propBox_->setEnabled(ok);
    QSignalBlocker b0(rectSpin_[0]), b1(rectSpin_[1]), b2(rectSpin_[2]), b3(rectSpin_[3]), b4(alphaSpin_), b5(fitCombo_);
    if (!ok) return;
    const Layer& l = layerCache_[si][li];
    rectSpin_[0]->setValue(l.rect.x());
    rectSpin_[1]->setValue(l.rect.y());
    rectSpin_[2]->setValue(l.rect.width());
    rectSpin_[3]->setValue(l.rect.height());
    alphaSpin_->setValue(l.opacity);
    fitCombo_->setCurrentIndex(int(l.fit));
}

void InspectorPanel::onLayersReordered() {
    const int si = currentScene();
    if (si < 0) return;
    const auto before = layerCache_[si];
    const int n = int(before.size());
    if (layerList_->count() != n) return;
    std::vector<Layer> after(n);
    for (int r = 0; r < n; ++r) {
        const int old = layerList_->item(r)->data(Qt::UserRole).toInt();
        if (old < 0 || old >= n) return;
        after[n - 1 - r] = before[old];
    }
    const int sel = n - 1 - std::max(0, layerList_->currentRow());
    editLayers(si, after, "Sắp xếp layer");
    syncLayerList(sel);
}

void InspectorPanel::pushLayers(int si) {
    auto scene = project_.scenes[si];
    auto layers = layerCache_[si];
    engine_.post([scene, layers](double) { scene->layers = layers; });
}

// ---------------- Output ----------------

void InspectorPanel::pushOutput(int i) {
    OutputSink* sink = engine_.output(i);
    OutputConfig cfg = outCache_[i];
    engine_.post([sink, cfg](double) { sink->mutableConfig() = cfg; });
}

namespace {
struct CalField { const char* label; double lo, hi, step; float Calibration::*field; };
const CalField kCal[6] = {
    {"sáng", 0, 2, 0.05, &Calibration::brightness}, {"tương phản", 0, 3, 0.05, &Calibration::contrast},
    {"gamma", 0.2, 4, 0.05, &Calibration::gamma}, {"R", 0, 2, 0.02, &Calibration::gainR},
    {"G", 0, 2, 0.02, &Calibration::gainG}, {"B", 0, 2, 0.02, &Calibration::gainB}};
}

void InspectorPanel::syncOutputWidgets(int i) {
    const OutputConfig& c = outCache_[i];
    OutW& w = outW_[i];
    QSignalBlocker a(w.scale), b(w.rot);
    w.scale->setCurrentIndex(int(c.scale));
    w.rot->setCurrentIndex(c.rotation / 90);
    const double cv[4] = {c.crop.x(), c.crop.y(), c.crop.width(), c.crop.height()};
    for (int k = 0; k < 4; ++k) { QSignalBlocker s(w.crop[k]); w.crop[k]->setValue(cv[k]); }
    for (int k = 0; k < 6; ++k) { QSignalBlocker s(w.cal[k]); w.cal[k]->setValue(c.cal.*(kCal[k].field)); }
}

QWidget* InspectorPanel::buildOutputTab() {
    auto* host = new QWidget;
    auto* root = new QVBoxLayout(host);
    outW_.resize(engine_.outputCount());
    for (int i = 0; i < engine_.outputCount(); ++i) {
        const OutputConfig& c = outCache_[i];
        OutW& w = outW_[i];
        auto* box = new QGroupBox(QString("%1  (%2x%3, kênh %4)").arg(c.name).arg(c.size.width()).arg(c.size.height()).arg(c.channel + 1));
        auto* g = new QGridLayout(box);

        w.scale = new QComboBox;
        w.scale->addItems({"Fit", "Fill", "Stretch", "Pixel 1:1"});
        connect(w.scale, &QComboBox::currentIndexChanged, this, [this, i](int k) {
            OutputConfig after = outCache_[i]; after.scale = ScaleMode(k);
            editOutput(i, after, "Đổi kiểu scale output");
        });
        g->addWidget(w.scale, 0, 0);

        w.rot = new QComboBox;
        w.rot->addItems({"Xoay 0°", "Xoay 90°", "Xoay 180°", "Xoay 270°"});
        connect(w.rot, &QComboBox::currentIndexChanged, this, [this, i](int k) {
            OutputConfig after = outCache_[i]; after.rotation = k * 90;
            editOutput(i, after, "Xoay output");
        });
        g->addWidget(w.rot, 0, 1);

        const char* cn[4] = {"crop x", "y", "w", "h"};
        for (int k = 0; k < 4; ++k) {
            w.crop[k] = spin(0, 1, 0.01, 0, 3);
            w.crop[k]->setPrefix(QString(cn[k]) + " ");
            connect(w.crop[k], &QDoubleSpinBox::valueChanged, this, [this, i, k](double v) {
                OutputConfig after = outCache_[i];
                QRectF& r = after.crop;
                if (k == 0) r.moveLeft(v); else if (k == 1) r.moveTop(v);
                else if (k == 2) r.setWidth(std::max(0.001, v)); else r.setHeight(std::max(0.001, v));
                editOutput(i, after, "Sửa vùng crop output", 2000000 + i * 100 + k);
            });
            g->addWidget(w.crop[k], 1 + k / 2, k % 2);
        }
        for (int k = 0; k < 6; ++k) {
            w.cal[k] = spin(kCal[k].lo, kCal[k].hi, kCal[k].step, 0);
            w.cal[k]->setPrefix(QString(kCal[k].label) + " ");
            connect(w.cal[k], &QDoubleSpinBox::valueChanged, this, [this, i, k](double v) {
                OutputConfig after = outCache_[i];
                after.cal.*(kCal[k].field) = float(v);
                editOutput(i, after, "Hiệu chỉnh màu output", 2000000 + i * 100 + 10 + k);
            });
            g->addWidget(w.cal[k], 3 + k / 2, k % 2);
        }
        syncOutputWidgets(i);
        root->addWidget(box);
    }
    root->addStretch();
    auto* sa = new QScrollArea;
    sa->setWidgetResizable(true);
    sa->setWidget(host);
    return sa;
}

} // namespace lc
