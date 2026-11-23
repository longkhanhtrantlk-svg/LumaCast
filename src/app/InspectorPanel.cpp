#include "InspectorPanel.h"
#include "ParamEditor.h"
#include <QAbstractItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
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
    addTab(buildEffectTab(), "Effect");
    addTab(buildOutputTab(), "Output");
}

void InspectorPanel::editLayers(int si, std::vector<Layer> after, const QString& text, int mergeId) {
    auto before = layerCache_[si];
    layerCache_[si] = after;
    pushLayers(si);
    undo_->push(new LayersCmd(this, si, std::move(before), std::move(after), text, mergeId));
    emit layerDataChanged();
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
    propBox_->setToolTip("Ô viền vàng = thuộc tính đang có keyframe: chỉnh giá trị sẽ tự đặt/sửa keyframe tại vị trí playhead.");
    auto* g = new QGridLayout(propBox_);
    struct PS { AnimProp p; const char* prefix; const char* name; double lo, hi, step; int row, col; };
    const PS props[kAnimPropCount] = {
        {AnimProp::X, "x ", "layerX", -4, 8, 0.01, 0, 0}, {AnimProp::Y, "y ", "layerY", -4, 8, 0.01, 0, 1},
        {AnimProp::W, "w ", "layerW", 0, 8, 0.01, 1, 0}, {AnimProp::H, "h ", "layerH", 0, 8, 0.01, 1, 1},
        {AnimProp::Opacity, "alpha ", "layerAlpha", 0, 1, 0.05, 2, 0}, {AnimProp::Rotation, "xoay ", "layerRot", -3600, 3600, 1, 3, 0}};
    for (const PS& ps : props) {
        auto* sp = spin(ps.lo, ps.hi, ps.step, ps.p == AnimProp::Opacity ? 1 : 0);
        sp->setPrefix(ps.prefix);
        sp->setObjectName(ps.name);
        if (ps.p == AnimProp::Rotation) sp->setSuffix("°");
        g->addWidget(sp, ps.row, ps.col);
        propSpin_[int(ps.p)] = sp;
        connect(sp, &QDoubleSpinBox::valueChanged, this, [this, p = ps.p](double val) { setLayerProp(p, val); });
    }
    fitCombo_ = new QComboBox;
    fitCombo_->addItems({"Stretch", "Fit", "Fill"});
    g->addWidget(fitCombo_, 2, 1);
    connect(fitCombo_, &QComboBox::currentIndexChanged, this, [this](int i) {
        editCurrentLayer([i](Layer& l) { l.fit = LayerFit(i); }, "Đổi kiểu fit layer");
    });
    blendCombo_ = new QComboBox;
    blendCombo_->setObjectName("layerBlend");
    blendCombo_->addItems({"Normal", "Add (cộng sáng)", "Multiply (nhân)", "Screen (màn lọc)"});
    g->addWidget(blendCombo_, 3, 1);
    connect(blendCombo_, &QComboBox::currentIndexChanged, this, [this](int i) {
        editCurrentLayer([i](Layer& l) { l.blend = BlendMode(i); }, "Đổi chế độ hoà trộn layer");
    });
    const char* cropPrefix[4] = {"cắt x ", "cắt y ", "cắt w ", "cắt h "};
    const char* cropName[4] = {"layerCropX", "layerCropY", "layerCropW", "layerCropH"};
    for (int k = 0; k < 4; ++k) {
        cropSpin_[k] = spin(0, 1, 0.01, k < 2 ? 0 : 1, 3);
        cropSpin_[k]->setPrefix(cropPrefix[k]);
        cropSpin_[k]->setObjectName(cropName[k]);
        cropSpin_[k]->setToolTip("Vùng của nguồn được dùng, tính theo hình đã xoay (0–1, gốc trên-trái)");
        g->addWidget(cropSpin_[k], 4 + k / 2, k % 2);
        connect(cropSpin_[k], &QDoubleSpinBox::valueChanged, this, [this, k](double val) {
            editCurrentLayer([k, val](Layer& l) {
                QRectF r = l.crop;
                if (k == 0) r.moveLeft(val); else if (k == 1) r.moveTop(val);
                else if (k == 2) r.setWidth(std::max(0.001, val)); else r.setHeight(std::max(0.001, val));
                if (r.right() > 1) { if (k == 2) r.setWidth(1 - r.x()); else r.moveLeft(1 - r.width()); }
                if (r.bottom() > 1) { if (k == 3) r.setHeight(1 - r.y()); else r.moveTop(1 - r.height()); }
                l.crop = r.intersected(QRectF(0, 0, 1, 1));
            }, "Sửa vùng cắt layer", 1000000 + 50 + k);
        });
    }
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
    emit layerDataChanged();
}

void InspectorPanel::syncProps() {
    const int si = currentScene(), li = currentLayerIdx();
    const bool ok = li >= 0;
    propBox_->setEnabled(ok);
    if (fxList_) syncEffectList(fxList_->currentRow());
    auto setv = [](QDoubleSpinBox* s, double v) { QSignalBlocker b(s); s->setValue(v); };
    if (!ok) return;
    const Layer& l = layerCache_[si][li];
    const LayerState st = evalLayer(l, playheadTime());               // gia tri hieu dung tai playhead (tinh + keyframe)
    setv(propSpin_[int(AnimProp::X)], st.rect.x());
    setv(propSpin_[int(AnimProp::Y)], st.rect.y());
    setv(propSpin_[int(AnimProp::W)], st.rect.width());
    setv(propSpin_[int(AnimProp::H)], st.rect.height());
    setv(propSpin_[int(AnimProp::Opacity)], st.opacity);
    setv(propSpin_[int(AnimProp::Rotation)], st.rotation);
    for (int p = 0; p < kAnimPropCount; ++p) {                         // vien vang = thuoc tinh dang co keyframe
        const Track* tr = l.track(AnimProp(p));
        propSpin_[p]->setStyleSheet(tr && !tr->keys.empty() ? "QDoubleSpinBox{border:1px solid #e0a526;}" : QString());
    }
    setv(cropSpin_[0], l.crop.x()); setv(cropSpin_[1], l.crop.y());
    setv(cropSpin_[2], l.crop.width()); setv(cropSpin_[3], l.crop.height());
    { QSignalBlocker b(fitCombo_); fitCombo_->setCurrentIndex(int(l.fit)); }
    { QSignalBlocker b(blendCombo_); blendCombo_->setCurrentIndex(int(l.blend)); }
}

std::shared_ptr<Scene> InspectorPanel::currentScenePtr() const {
    const int si = currentScene();
    return si >= 0 && si < int(project_.scenes.size()) ? project_.scenes[si] : nullptr;
}

const Layer* InspectorPanel::currentLayerData() const {
    const int si = currentScene(), li = currentLayerIdx();
    return si >= 0 && si < int(layerCache_.size()) && li >= 0 && li < int(layerCache_[si].size()) ? &layerCache_[si][li] : nullptr;
}

double InspectorPanel::playheadTime() const {
    auto sc = currentScenePtr();
    return sc ? double(sc->clock.time) : 0.0;
}

void InspectorPanel::editCurrentLayer(const std::function<void(Layer&)>& mutate, const QString& text, int mergeId) {
    const int si = currentScene(), li = currentLayerIdx();
    if (li < 0) return;
    auto after = layerCache_[si];
    mutate(after[li]);
    editLayers(si, after, text, mergeId);
}

void InspectorPanel::setLayerProp(AnimProp p, double v) {
    const int si = currentScene(), li = currentLayerIdx();
    if (li < 0) return;
    const double t = playheadTime();
    editCurrentLayer([p, v, t](Layer& l) {
        if (Track* tr = l.track(p); tr && !tr->keys.empty()) {          // co keyframe: tu dat/sua key tai playhead
            const int k = findKey(*tr, t);
            setKey(*tr, t, v, k >= 0 ? tr->keys[k].interp : Interp::Linear);
            return;
        }
        switch (p) {
        case AnimProp::X: l.rect.moveLeft(v); break;
        case AnimProp::Y: l.rect.moveTop(v); break;
        case AnimProp::W: l.rect.setWidth(v); break;
        case AnimProp::H: l.rect.setHeight(v); break;
        case AnimProp::Opacity: l.opacity = float(v); break;
        case AnimProp::Rotation: l.rotation = float(v); break;
        }
    }, "Sửa thuộc tính layer", 1000000 + si * 10000 + li * 100 + int(p));
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


// ---------------- Effect ----------------

QWidget* InspectorPanel::buildEffectTab() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    fxTitle_ = new QLabel;
    fxTitle_->setWordWrap(true);
    v->addWidget(fxTitle_);

    fxList_ = new QListWidget;
    fxList_->setObjectName("effectList");
    fxList_->setToolTip("Chuỗi Effect của layer, chạy từ trên xuống. Tick để bật/tắt từng Effect.");
    v->addWidget(fxList_, 1);

    auto* add = new QHBoxLayout;
    fxAddCombo_ = new QComboBox;
    fxAddCombo_->setObjectName("effectAddCombo");
    auto* addB = new QPushButton("Thêm Effect");
    addB->setObjectName("effectAdd");
    add->addWidget(fxAddCombo_, 1);
    add->addWidget(addB);
    v->addLayout(add);
    auto* ops = new QHBoxLayout;
    auto* upB = new QPushButton("Effect lên"), * downB = new QPushButton("Effect xuống"), * delB = new QPushButton("Gỡ Effect");
    upB->setObjectName("effectUp"); downB->setObjectName("effectDown"); delB->setObjectName("effectDel");
    ops->addWidget(upB); ops->addWidget(downB); ops->addWidget(delB);
    v->addLayout(ops);

    fxNote_ = new QLabel;
    fxNote_->setWordWrap(true);
    fxNote_->setStyleSheet("color:#ff8a65");
    v->addWidget(fxNote_);

    fxParamBox_ = new QGroupBox("Tham số");
    new QVBoxLayout(fxParamBox_);
    v->addWidget(fxParamBox_);

    connect(fxList_, &QListWidget::currentRowChanged, this, [this] { syncEffectParams(); });
    connect(fxList_, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        const int si = currentScene(), li = currentLayerIdx(), ei = fxList_->row(it);
        if (li < 0 || ei < 0 || ei >= int(layerCache_[si][li].effects.size())) return;
        auto after = layerCache_[si];
        after[li].effects[ei].enabled = it->checkState() == Qt::Checked;
        editLayers(si, after, after[li].effects[ei].enabled ? "Bật Effect" : "Tắt Effect");
    });
    connect(addB, &QPushButton::clicked, this, [this] {
        const int si = currentScene(), li = currentLayerIdx();
        const QString id = fxAddCombo_->currentData().toString();
        auto def = engine_.registry().find(id);
        if (li < 0 || !def) return;
        auto after = layerCache_[si];
        EffectInstance e;
        e.effectId = id;
        e.params = defaultParams(*def);
        after[li].effects.push_back(e);
        editLayers(si, after, "Thêm Effect");
        syncEffectList(int(after[li].effects.size()) - 1);
    });
    auto move = [this](int dir) {
        const int si = currentScene(), li = currentLayerIdx(), ei = fxList_->currentRow();
        if (li < 0 || ei < 0) return;
        auto after = layerCache_[si];
        auto& fx = after[li].effects;
        if (ei + dir < 0 || ei + dir >= int(fx.size())) return;
        std::swap(fx[ei], fx[ei + dir]);
        editLayers(si, after, "Đổi thứ tự Effect");
        syncEffectList(ei + dir);
    };
    connect(upB, &QPushButton::clicked, this, [move] { move(-1); });
    connect(downB, &QPushButton::clicked, this, [move] { move(+1); });
    connect(delB, &QPushButton::clicked, this, [this] {
        const int si = currentScene(), li = currentLayerIdx(), ei = fxList_->currentRow();
        if (li < 0 || ei < 0) return;
        auto after = layerCache_[si];
        after[li].effects.erase(after[li].effects.begin() + ei);
        editLayers(si, after, "Gỡ Effect");
        syncEffectList(std::min(ei, int(after[li].effects.size()) - 1));
    });
    refreshEffectCatalog();
    syncEffectList(-1);
    return page;
}

void InspectorPanel::refreshEffectCatalog() {
    if (!fxAddCombo_) return;
    const QString keep = fxAddCombo_->currentData().toString();
    QSignalBlocker b(fxAddCombo_);
    fxAddCombo_->clear();
    for (const auto& d : engine_.registry().all(EffectDef::Effect)) fxAddCombo_->addItem(d->name, d->id);
    const int k = fxAddCombo_->findData(keep);
    fxAddCombo_->setCurrentIndex(k >= 0 ? k : 0);
    syncEffectList(fxList_ ? fxList_->currentRow() : -1);        // ten/tham so co the doi khi plug-in nap lai
}

void InspectorPanel::syncEffectList(int selectRow) {
    if (!fxList_) return;
    const int si = currentScene(), li = currentLayerIdx();
    const bool has = si >= 0 && si < int(layerCache_.size()) && li >= 0 && li < int(layerCache_[si].size());
    {
        QSignalBlocker b(fxList_);
        fxList_->clear();
        if (has) {
            for (const EffectInstance& e : layerCache_[si][li].effects) {
                auto def = engine_.registry().find(e.effectId);
                auto* it = new QListWidgetItem(def ? def->name : QString("⚠ thiếu plug-in: %1").arg(e.effectId));
                it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
                it->setCheckState(e.enabled ? Qt::Checked : Qt::Unchecked);
                if (!def) it->setForeground(QColor("#ff8a65"));
                fxList_->addItem(it);
            }
            const int n = fxList_->count();
            if (n > 0) fxList_->setCurrentRow(selectRow >= 0 && selectRow < n ? selectRow : 0);
        }
    }
    fxTitle_->setText(has ? QString("Effect của layer %1 (chạy từ trên xuống dưới)").arg(li + 1)
                          : "Chọn một layer ở tab Scene / Layer để thêm Effect.");
    syncEffectParams();
}

void InspectorPanel::syncEffectParams() {
    if (fxParams_) { fxParams_->hide(); fxParams_->deleteLater(); fxParams_ = nullptr; }
    const int si = currentScene(), li = currentLayerIdx(), ei = fxList_->currentRow();
    const bool ok = si >= 0 && li >= 0 && ei >= 0 && li < int(layerCache_[si].size()) && ei < int(layerCache_[si][li].effects.size());
    fxParamBox_->setVisible(ok);
    fxNote_->clear();
    if (!ok) return;
    const EffectInstance inst = layerCache_[si][li].effects[ei];
    auto def = engine_.registry().find(inst.effectId);
    if (!def) {
        fxNote_->setText(QString("Plug-in '%1' không có hoặc đã tắt. Dữ liệu vẫn được giữ trong project; Effect này đang bị bỏ qua.").arg(inst.effectId));
        fxParamBox_->setVisible(false);
        return;
    }
    updateEffectNote();
    if (def->params.isEmpty()) { fxParamBox_->setVisible(false); return; }
    fxParams_ = new ParamEditor(def, inst.params);
    fxParams_->setObjectName("effectParams");
    fxParamBox_->layout()->addWidget(fxParams_);
    connect(fxParams_, &ParamEditor::changed, this, [this](const QVariantMap& vals, int pi) {
        const int si = currentScene(), li = currentLayerIdx(), ei = fxList_->currentRow();
        if (li < 0 || ei < 0) return;
        auto after = layerCache_[si];
        after[li].effects[ei].params = vals;
        editLayers(si, after, "Sửa tham số Effect", 3000000 + (si * 100 + li) * 1000 + ei * 50 + pi);
    });
}

void InspectorPanel::updateEffectNote() {
    if (!fxNote_ || !fxList_) return;
    const int si = currentScene(), li = currentLayerIdx(), ei = fxList_->currentRow();
    if (si < 0 || li < 0 || ei < 0 || li >= int(layerCache_[si].size()) || ei >= int(layerCache_[si][li].effects.size())) return;
    const QString err = engine_.registry().compileError(layerCache_[si][li].effects[ei].effectId);
    fxNote_->setText(err.isEmpty() ? QString() : "Shader lỗi, Effect này đang bị bỏ qua:\n" + err);
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
