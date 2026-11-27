// Phan "dau vao" cua MainWindow: phim tat tu dat (UI-07) va MIDI (CTL-01). Tach rieng de MainWindow.cpp gon.
#include "MainWindow.h"
#include "InputDialog.h"
#include "LockButton.h"
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
    binder_ = new ShortcutBinder(this, [this](const LiveAction& a) { runKeyAction(a); });
    QSettings st = appSettings();
    applyKeys(shortcuts::load(st));
    midi_->apply(MidiConfig::load(st));
    inputAct_ = settingsMenu->addAction("Phím tắt và MIDI…", this, [this] { openInputDialog(); });
}

void MainWindow::applyKeys(const KeyMap& keys) {
    binder_->apply(keys);
    lockBtn_->setShortcutText(keys.value("lock").toString(QKeySequence::NativeText));
}

void MainWindow::openInputDialog() {
    if (locked_) { showMessage("Đang khoá vận hành: không đổi phím tắt/MIDI được"); return; }
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
    case LiveAction::None: break;
    }
}

} // namespace lc
