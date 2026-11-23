#include "ControlPanel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

namespace lc {

ControlPanel::ControlPanel(Project& project, RenderEngine& engine) : project_(project), engine_(engine) {
    setWindowTitle("LumaCast - dieu khien");
    auto* root = new QVBoxLayout(this);

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

        // chon Preview: nut + phim 1..9 (kenh dau tien)
        auto* scenes = new QHBoxLayout;
        for (size_t si = 0; si < ch->scenes.size(); ++si) {
            auto* b = new QPushButton(QString("%1  %2").arg(si + 1).arg(ch->scenes[si]->name));
            const int idx = int(si);
            QObject::connect(b, &QPushButton::clicked, this, [this, ch, idx] {
                engine_.post([ch, idx](double) { ch->setPreview(idx); });
            });
            scenes->addWidget(b);
            if (ci == 0 && si < 9) {
                auto* sc = new QShortcut(QKeySequence(Qt::Key_1 + int(si)), this);
                QObject::connect(sc, &QShortcut::activated, b, &QPushButton::click);
            }
        }
        v->addLayout(scenes);

        auto* tr = new QHBoxLayout;
        auto* type = new QComboBox;
        for (auto t : {TransitionType::Fade, TransitionType::DipToBlack, TransitionType::WipeLeft,
                       TransitionType::WipeRight, TransitionType::WipeUp, TransitionType::WipeDown})
            type->addItem(transitionName(t), int(t));
        auto* dur = new QDoubleSpinBox;
        dur->setRange(0.1, 10);
        dur->setSingleStep(0.1);
        dur->setValue(1.0);
        dur->setSuffix(" s");
        auto* cut = new QPushButton("CUT");
        auto* autoB = new QPushButton("AUTO");
        tr->addWidget(type);
        tr->addWidget(dur);
        tr->addWidget(cut);
        tr->addWidget(autoB);
        v->addLayout(tr);
        root->addWidget(box);

        QObject::connect(cut, &QPushButton::clicked, this, [this, ch] {
            engine_.post([ch](double) { ch->cut(); });
        });
        QObject::connect(autoB, &QPushButton::clicked, this, [this, ch, type, dur] {
            const auto tt = TransitionType(type->currentData().toInt());
            const double d = dur->value();
            engine_.post([ch, tt, d](double now) { ch->startTransition(tt, d, now); });
        });
        if (ci == 0) {
            QObject::connect(new QShortcut(QKeySequence(Qt::Key_Space), this), &QShortcut::activated, autoB,
                             &QPushButton::click);
            QObject::connect(new QShortcut(QKeySequence(Qt::Key_Return), this), &QShortcut::activated, cut,
                             &QPushButton::click);
        }
    }

    auto* outs = new QGroupBox("Output");
    auto* ov = new QVBoxLayout(outs);
    for (int i = 0; i < engine_.outputCount(); ++i) {
        OutputSink* w = engine_.output(i);
        const OutputConfig& c = w->config();
        auto* cb = new QCheckBox(QString("Den: %1  (%2x%3, kenh %4)")
                                     .arg(c.name).arg(c.size.width()).arg(c.size.height()).arg(c.channel + 1));
        QObject::connect(cb, &QCheckBox::toggled, this, [w](bool on) { w->black = on; });
        ov->addWidget(cb);
    }
    fpsLabel_ = new QLabel;
    ov->addWidget(fpsLabel_);
    root->addWidget(outs);

    connect(&timer_, &QTimer::timeout, this, [this] { refresh(); });
    timer_.start(100);
    refresh();
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
