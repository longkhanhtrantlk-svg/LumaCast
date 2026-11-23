#include "ControlPanel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QTabWidget>

namespace lc {

static QDoubleSpinBox* spin(double lo, double hi, double step, double v, int dec = 2) {
    auto* s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setDecimals(dec);
    s->setValue(v);
    return s;
}

ControlPanel::ControlPanel(Project& project, RenderEngine& engine) : project_(project), engine_(engine) {
    setWindowTitle("LumaCast - dieu khien");
    for (const auto& s : project_.scenes) layerCache_.push_back(s->layers);
    for (int i = 0; i < engine_.outputCount(); ++i) outCache_.push_back(engine_.output(i)->config());

    auto* root = new QVBoxLayout(this);
    auto* tabs = new QTabWidget;
    tabs->addTab(buildChannelsTab(), "Kenh");
    tabs->addTab(buildSceneTab(), "Scene / Layer");
    tabs->addTab(buildOutputTab(), "Output");
    root->addWidget(tabs);

    auto* bar = new QHBoxLayout;
    auto* saveB = new QPushButton("Luu project");
    auto* saveAs = new QPushButton("Luu thanh...");
    fpsLabel_ = new QLabel;
    status_ = new QLabel;
    bar->addWidget(saveB);
    bar->addWidget(saveAs);
    bar->addWidget(fpsLabel_);
    bar->addStretch();
    root->addLayout(bar);
    root->addWidget(status_);
    connect(saveB, &QPushButton::clicked, this, [this] { save(false); });
    connect(saveAs, &QPushButton::clicked, this, [this] { save(true); });
    new QShortcut(QKeySequence::Save, this, [this] { save(false); });

    connect(&timer_, &QTimer::timeout, this, [this] { refresh(); });
    timer_.start(100);
    refresh();
}

// ---------------- tab Kenh ----------------

QWidget* ControlPanel::buildChannelsTab() {
    auto* page = new QWidget;
    auto* root = new QVBoxLayout(page);
    for (size_t ci = 0; ci < project_.channels.size(); ++ci) {
        std::shared_ptr<Channel> ch = project_.channels[ci];
        auto* box = new QGroupBox(QString("Kenh %1: %2").arg(ci + 1).arg(ch->name));
        auto* v = new QVBoxLayout(box);

        auto* status = new QHBoxLayout;
        auto* prog = new QLabel, * prev = new QLabel;
        prog->setStyleSheet("color:#ff5555;font-weight:bold");
        prev->setStyleSheet("color:#55dd55;font-weight:bold");
        status->addWidget(prog);
        status->addWidget(prev);
        v->addLayout(status);
        chanUi_.push_back({prog, prev});

        auto* scenes = new QHBoxLayout;
        for (size_t si = 0; si < ch->scenes.size(); ++si) {
            auto* b = new QPushButton(QString("%1  %2").arg(si + 1).arg(ch->scenes[si]->name));
            const int idx = int(si);
            connect(b, &QPushButton::clicked, this, [this, ch, idx] {
                engine_.post([ch, idx](double) { ch->setPreview(idx); });
            });
            scenes->addWidget(b);
            if (ci == 0 && si < 9) {
                auto* sc = new QShortcut(QKeySequence(Qt::Key_1 + int(si)), this);
                connect(sc, &QShortcut::activated, b, &QPushButton::click);
            }
        }
        v->addLayout(scenes);

        auto* tr = new QHBoxLayout;
        auto* type = new QComboBox;
        for (auto t : {TransitionType::Fade, TransitionType::DipToBlack, TransitionType::WipeLeft,
                       TransitionType::WipeRight, TransitionType::WipeUp, TransitionType::WipeDown})
            type->addItem(transitionName(t), int(t));
        auto* dur = spin(0.1, 10, 0.1, 1.0, 1);
        dur->setSuffix(" s");
        auto* cut = new QPushButton("CUT");
        auto* autoB = new QPushButton("AUTO");
        tr->addWidget(type);
        tr->addWidget(dur);
        tr->addWidget(cut);
        tr->addWidget(autoB);
        v->addLayout(tr);
        root->addWidget(box);

        connect(cut, &QPushButton::clicked, this, [this, ch] { engine_.post([ch](double) { ch->cut(); }); });
        connect(autoB, &QPushButton::clicked, this, [this, ch, type, dur] {
            const auto tt = TransitionType(type->currentData().toInt());
            const double d = dur->value();
            engine_.post([ch, tt, d](double now) { ch->startTransition(tt, d, now); });
        });
        if (ci == 0) {
            connect(new QShortcut(QKeySequence(Qt::Key_Space), this), &QShortcut::activated, autoB, &QPushButton::click);
            connect(new QShortcut(QKeySequence(Qt::Key_Return), this), &QShortcut::activated, cut, &QPushButton::click);
        }
    }
    root->addStretch();
    return page;
}

// ---------------- tab Scene / Layer ----------------

QWidget* ControlPanel::buildSceneTab() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);

    sceneCombo_ = new QComboBox;
    for (const auto& s : project_.scenes)
        sceneCombo_->addItem(QString("%1  (%2x%3)").arg(s->name).arg(s->size.width()).arg(s->size.height()));
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
    for (const auto& d : project_.sources)
        addSrcCombo_->addItem(QString("%1 [%2]").arg(d.source ? d.source->name() : d.id, d.type));
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
    rebuildLayerRows();
    return page;
}

void ControlPanel::pushLayers(int si) {
    auto scene = project_.scenes[si];
    auto layers = layerCache_[si];
    engine_.post([scene, layers](double) { scene->layers = layers; });
}

void ControlPanel::rebuildLayerRows() {
    while (QLayoutItem* it = layerRows_->takeAt(0)) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    const int si = sceneCombo_->currentIndex();
    if (si < 0) return;
    const auto& layers = layerCache_[si];
    for (int li = 0; li < int(layers.size()); ++li) {
        const Layer& l = layers[li];
        const SourceDef* d = project_.findSource(l.source.get());
        auto* row = new QGroupBox(QString("%1. %2").arg(li + 1).arg(d ? (l.source->name()) : "?"));
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
        connect(up, &QPushButton::clicked, this, [=] { reorder(li, li - 1); });       // "len" = ve sau, hien tren
        connect(down, &QPushButton::clicked, this, [=] { reorder(li, li + 1); });
        connect(del, &QPushButton::clicked, this, [this, si, li] {
            layerCache_[si].erase(layerCache_[si].begin() + li);
            pushLayers(si);
            QTimer::singleShot(0, this, [this] { rebuildLayerRows(); });
        });
        layerRows_->addWidget(row);
    }
}

// ---------------- tab Output ----------------

void ControlPanel::pushOutput(int i) {
    OutputSink* sink = engine_.output(i);
    OutputConfig cfg = outCache_[i];
    engine_.post([sink, cfg](double) { sink->mutableConfig() = cfg; });
}

QWidget* ControlPanel::buildOutputTab() {
    auto* host = new QWidget;
    auto* root = new QVBoxLayout(host);
    for (int i = 0; i < engine_.outputCount(); ++i) {
        const OutputConfig& c = outCache_[i];
        auto* box = new QGroupBox(QString("%1  (%2x%3, kenh %4)").arg(c.name).arg(c.size.width()).arg(c.size.height()).arg(c.channel + 1));
        auto* g = new QGridLayout(box);
        OutputSink* sink = engine_.output(i);

        auto* black = new QCheckBox("Den");
        connect(black, &QCheckBox::toggled, this, [sink](bool on) { sink->black = on; });
        g->addWidget(black, 0, 0);

        auto* scale = new QComboBox;
        scale->addItems({"Fit", "Fill", "Stretch", "Pixel 1:1"});
        scale->setCurrentIndex(int(c.scale));
        connect(scale, &QComboBox::currentIndexChanged, this, [this, i](int k) { outCache_[i].scale = ScaleMode(k); pushOutput(i); });
        g->addWidget(scale, 0, 1);

        auto* rot = new QComboBox;
        rot->addItems({"0", "90", "180", "270"});
        rot->setCurrentIndex(c.rotation / 90);
        connect(rot, &QComboBox::currentIndexChanged, this, [this, i](int k) { outCache_[i].rotation = k * 90; pushOutput(i); });
        g->addWidget(rot, 0, 2);

        // Mapping: vung cua Channel hien tren output nay
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
            g->addWidget(sp, 1, k);
        }

        // Hieu chinh mau
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
            g->addWidget(sp, 2 + col / 3, col % 3);
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

// ---------------- chung ----------------

void ControlPanel::save(bool saveAs) {
    QString path = project_.filePath;
    if (path.isEmpty() || saveAs) {
        path = QFileDialog::getSaveFileName(this, "Luu project", path.isEmpty() ? "project.lumacast.json" : path,
                                            "LumaCast project (*.json)");
        if (path.isEmpty()) return;
    }
    project_.filePath = path;
    engine_.post([this, path](double) {
        engine_.syncOutputsToProject();                     // lay cau hinh Output hien tai, tren render thread
        QString err;
        const bool ok = saveProject(project_, path, &err);
        QMetaObject::invokeMethod(this, [this, ok, err, path] {
            status_->setText(ok ? "Da luu: " + path : "Loi luu: " + err);
        }, Qt::QueuedConnection);
    });
}

void ControlPanel::refresh() {
    for (size_t i = 0; i < chanUi_.size(); ++i) {
        const Channel& c = *project_.channels[i];
        auto nameOf = [&](int idx) { return idx >= 0 && idx < int(c.scenes.size()) ? c.scenes[idx]->name : "-"; };
        chanUi_[i].program->setText("PROGRAM: " + nameOf(c.program) + (c.inTransition() ? "  (dang chuyen)" : ""));
        chanUi_[i].preview->setText("PREVIEW: " + nameOf(c.preview));
    }
    fpsLabel_->setText(QString("Render: %1 fps").arg(engine_.fps(), 0, 'f', 1));
}

} // namespace lc
