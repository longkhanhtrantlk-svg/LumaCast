// Phan "dau vao" cua MainWindow: phim tat tu dat (UI-07) va MIDI (CTL-01). Tach rieng de MainWindow.cpp gon.
#include "MainWindow.h"
#include "core/I18n.h"
#include "InputDialog.h"
#include "LockButton.h"
#include "Text.h"
#include "live/SlideRemote.h"
#include "core/AppSettings.h"
#include <QAction>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QMenuBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolButton>

namespace lc {

void MainWindow::setupInput(QMenu* settingsMenu) {
    midi_ = new MidiService(*ctlApi_, createSystemMidiBackend(), this);
    midi_->setTargetProvider([this] { return MidiService::Target{channelIdx() + 1, currentSpec(), durSpin_->value()}; });
    connect(midi_, &MidiService::statusChanged, this, [this] {
        updateControlStatus();
        if (midi_->config().enabled) showMessage("MIDI: " + midi_->statusText());
    });
    slides_ = new SlideRemote(this);
    connect(slides_, &SlideRemote::stepped, this, [this](int, bool ok, int position, int count, const QString& error) {
        if (!ok) showMessage(error);
        else if (position > 0 && count > 0) showMessage(QString("PowerPoint: slide %1/%2").arg(position).arg(count));
    });
    binder_ = new ShortcutBinder(this, [this](const LiveAction& a) { runKeyAction(a); });
    QSettings st = appSettings();
    applyKeys(shortcuts::load(st));
    midi_->apply(MidiConfig::load(st));
    inputAct_ = settingsMenu->addAction(LC_TR("Phím tắt và MIDI…"), this, [this] { openInputDialog(); });
}

void MainWindow::applyKeys(const KeyMap& keys) {
    binder_->apply(keys);
    lockBtn_->setShortcutText(keys.value("lock").toString(QKeySequence::NativeText));
    updateKeyHints();
}

void MainWindow::updateKeyHints() {
    const KeyMap& keys = binder_->map();
    auto hint = [&](QWidget* w, const QString& tip, const QString& id, const QString& fixed = {}) {
        QStringList parts;
        if (!fixed.isEmpty()) parts << fixed;
        const QString k = keys.value(id).toString(QKeySequence::NativeText);
        if (!k.isEmpty()) parts << k;
        w->setToolTip(parts.isEmpty() ? tip : text::withKey().arg(tip, parts.join(" / ")));
    };
    hint(cutBtn_, text::cutTip(), "cut", "Enter");
    hint(takeBtn_, text::takeTip(), "take", "Space");
    hint(blackAll_, text::blackAllTip(), "blackAll");
    hint(ftb_, text::ftbTip(), "ftb");
    for (int i = 0; i < int(outRows_.size()); ++i) {
        hint(outRows_[i].black, text::outBlack(), QString("output.%1.black").arg(i + 1));
        hint(outRows_[i].freeze, text::outFreeze(), QString("output.%1.freeze").arg(i + 1));
        hint(outRows_[i].pattern, text::outPattern(), QString("output.%1.pattern").arg(i + 1));
    }
}

void MainWindow::openInputDialog() {
    if (locked_) { showMessage(LC_TR("Đang khoá vận hành: không đổi phím tắt/MIDI được")); return; }
    QStringList names;
    for (int i = 0; i < engine_.outputCount(); ++i) names << engine_.output(i)->config().name;
    InputDialog dlg(*midi_, names, binder_->map(), this);
    if (dlg.exec() == QDialog::Accepted) applyKeys(dlg.keys());
    updateControlStatus();
}

// Phim tat: giong bam nut tren giao dien (ke ca AUTO khi chon Preview). MIDI KHONG di qua day ma qua ControlApi.
void MainWindow::runKeyAction(const LiveAction& a) {
    switch (a.kind) {
    case LiveAction::Take: take(); break;
    case LiveAction::Cut: cut(); break;
    case LiveAction::Ftb: toggleFtb(); break;
    case LiveAction::BlackAll: setBlackAll(!blackAll_->isChecked()); break;
    case LiveAction::Lock: if (!locked_) setLocked(true); break;          // chi khoa; mo khoa = giu chuot
    case LiveAction::Preview: {
        const int i = a.index - 1;
        if (i < sceneList_->count()) { sceneList_->setCurrentRow(i); emit sceneList_->itemClicked(sceneList_->item(i)); }
        break;
    }
    case LiveAction::OutputBlack:
    case LiveAction::OutputFreeze:
    case LiveAction::OutputPattern: {
        const int i = a.index - 1;
        if (i >= engine_.outputCount() || i >= int(outRows_.size())) break;
        OutputSink* sink = engine_.output(i);
        std::atomic<bool>& flag = a.kind == LiveAction::OutputBlack ? sink->black : a.kind == LiveAction::OutputFreeze ? sink->freeze : sink->testPattern;
        QToolButton* button = a.kind == LiveAction::OutputBlack ? outRows_[i].black : a.kind == LiveAction::OutputFreeze ? outRows_[i].freeze : outRows_[i].pattern;
        const bool on = !flag.load();
        flag = on;
        QSignalBlocker block(button);
        button->setChecked(on);
        break;
    }
    case LiveAction::SlideNext: slides_->step(+1); break;       // lenh live: dung duoc ca khi khoa van hanh
    case LiveAction::SlidePrev: slides_->step(-1); break;
    case LiveAction::None: break;
    }
}

} // namespace lc
