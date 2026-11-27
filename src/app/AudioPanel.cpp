#include "AudioPanel.h"
#include "Text.h"
#include "Theme.h"
#include "Ui.h"
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace lc {

LevelMeter::LevelMeter(QWidget* parent) : QWidget(parent) { setMinimumSize(80, 18); }

double LevelMeter::toFraction(float lin) {
    if (!(lin > 1e-6f)) return 0.0;
    const double db = 20.0 * std::log10(double(lin));
    return std::clamp((db + 60.0) / 60.0, 0.0, 1.0);
}

void LevelMeter::setLevels(float pl, float pr, float rl, float rr) {
    const float np[2] = {pl, pr}, nr[2] = {rl, rr};
    bool changed = false;
    for (int k = 0; k < 2; ++k) {
        changed = changed || std::abs(toFraction(np[k]) - toFraction(peak_[k])) > 0.004 || std::abs(toFraction(nr[k]) - toFraction(rms_[k])) > 0.004;
        peak_[k] = np[k]; rms_[k] = nr[k];
    }
    if (changed) update();
}

void LevelMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#14161c"));
    const int h = (height() - 3) / 2;
    for (int k = 0; k < 2; ++k) {
        const QRect row(1, 1 + k * (h + 1), width() - 2, h);
        p.fillRect(row, QColor("#20232b"));
        const int rw = int(toFraction(rms_[k]) * row.width());
        const int pw = int(toFraction(peak_[k]) * row.width());
        auto colorAt = [&](double frac) { const double db = frac * 60 - 60; return db >= -3 ? QColor(colors::kProgram) : db >= -12 ? QColor(colors::kWarn) : QColor(colors::kPreview); };
        // thanh RMS: to theo vung mau; vach dinh sang hon
        for (int x = 0; x < rw; x += 2) p.fillRect(QRect(row.left() + x, row.top(), 2, row.height()), colorAt(double(x) / row.width()).darker(130));
        if (pw > 0) p.fillRect(QRect(row.left() + std::max(0, pw - 2), row.top(), 2, row.height()), colorAt(double(pw) / row.width()));
    }
    p.setPen(QColor("#3a3f4b"));
    for (int db : {-48, -36, -24, -12, -3}) {                      // vach chia
        const int x = 1 + int((db + 60.0) / 60.0 * (width() - 2));
        p.drawLine(x, 0, x, height());
    }
}

AudioPanel::AudioPanel(Project& project, RenderEngine& engine, QWidget* parent) : QWidget(parent), project_(project), engine_(engine) {
    setObjectName("audioPanel");
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(6, 4, 6, 4);
    v->setSpacing(3);
    auto* top = new QHBoxLayout;
    device_ = new QComboBox;
    device_->setObjectName("audioDevice");
    device_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);     // ten thiet bi dai khong ep cot rong ra
    device_->setMinimumContentsLength(6);
    device_->setToolTip("Thiết bị phát âm thanh của toàn bộ các Channel (mix chung). Lưu trong project; máy khác không có thiết bị này sẽ dùng mặc định.");
    top->addWidget(device_, 1);
    auto* refresh = ui::iconButton(Glyph::Refresh, "audioDevRefresh", text::audioRefresh());
    top->addWidget(refresh);
    v->addLayout(top);
    status_ = new QLabel;
    status_->setObjectName("audioStatus");
    status_->setWordWrap(true);
    v->addWidget(status_);

    auto* grid = new QGridLayout;                 // moi kenh hai hang (nut + thanh, roi meter) de vua cot hep
    grid->setColumnStretch(2, 1);
    for (size_t i = 0; i < project_.channels.size(); ++i) {
        Strip s;
        const int row = int(i) * 2;
        s.name = new QLabel(QString("Kênh %1").arg(i + 1));
        s.name->setToolTip(project_.channels[i]->name);
        s.mute = new QToolButton;
        s.mute->setObjectName(QString("muteBtn%1").arg(i));
        s.mute->setCheckable(true);
        s.mute->setIcon(glyphIcon(Glyph::Speaker));
        s.mute->setFocusPolicy(Qt::NoFocus);
        s.mute->setToolTip(text::audioMute());
        s.vol = new QSlider(Qt::Horizontal);
        s.vol->setObjectName(QString("volSlider%1").arg(i));
        s.vol->setRange(0, 150);
        s.vol->setValue(int(std::lround(project_.channels[i]->volume.load() * 100)));
        s.vol->setToolTip("Âm lượng Channel (100% = nguyên gốc, tối đa 150%)");
        s.volText = new QLabel;
        s.volText->setMinimumWidth(40);
        s.meter = new LevelMeter;
        s.meter->setObjectName(QString("meter%1").arg(i));
        grid->addWidget(s.name, row, 0);
        grid->addWidget(s.mute, row, 1);
        grid->addWidget(s.vol, row, 2);
        grid->addWidget(s.volText, row, 3);
        grid->addWidget(s.meter, row + 1, 0, 1, 4);
        strips_.push_back(s);
        auto ch = project_.channels[i];
        connect(s.mute, &QToolButton::toggled, this, [this, ch, b = s.mute](bool on) {
            b->setIcon(glyphIcon(on ? Glyph::Muted : Glyph::Speaker));
            engine_.post([ch, on](double) { ch->mute = on; });
        });
        connect(s.vol, &QSlider::valueChanged, this, [this, ch, i](int pct) {
            strips_[i].volText->setText(QString("%1%").arg(pct));
            const float g = float(pct) / 100.0f;
            engine_.post([ch, g](double) { ch->volume = g; });
        });
    }
    v->addLayout(grid);
    auto* mrow = new QHBoxLayout;
    mrow->addWidget(new QLabel("Tổng (master)"));
    master_ = new LevelMeter;
    master_->setObjectName("meterMaster");
    mrow->addWidget(master_, 1);
    v->addLayout(mrow);
    v->addStretch();

    reloadDevices();
    connect(refresh, &QPushButton::clicked, this, [this] { reloadDevices(); });
    connect(device_, &QComboBox::activated, this, [this](int i) {
        if (building_) return;
        const QString id = device_->itemData(i).toString();
        if (id == project_.audioDevice) return;
        const QString e = applyDevice(id);
        if (!e.isEmpty()) status_->setText("Thiết bị lỗi, dùng Null: " + e);
    });
    building_ = false;
    refreshMeters();
}

void AudioPanel::reloadDevices() {
    const bool was = building_;
    building_ = true;
    device_->clear();
    int sel = 0;
    const auto devs = listAudioDevices();
    for (size_t i = 0; i < devs.size(); ++i) {
        device_->addItem(devs[i].name + (devs[i].isDefault && !devs[i].id.isEmpty() ? "  (mặc định)" : ""), devs[i].id);
        if (devs[i].id == project_.audioDevice) sel = int(i);
    }
    if (!project_.audioDevice.isEmpty() && device_->itemData(sel).toString() != project_.audioDevice) {      // thiet bi cua project khong co tren may nay
        device_->addItem("(không có trên máy này) " + project_.audioDevice.left(40), project_.audioDevice);
        sel = device_->count() - 1;
    }
    device_->setCurrentIndex(sel);
    building_ = was;
    updateStatus();
}

QString AudioPanel::applyDevice(const QString& id) {
    project_.audioDevice = id;
    QString err;
    engine_.audio().start(id, &err);
    updateStatus();
    emit deviceChanged(id);
    return err;
}

void AudioPanel::updateStatus() {
    AudioMixer& m = engine_.audio();
    status_->setText(m.running() ? QString("Đang phát: %1").arg(m.deviceName()) : QString("Âm thanh chưa chạy"));
}

void AudioPanel::setEditLocked(bool on) {
    device_->setEnabled(!on);
    findChild<QPushButton*>("audioDevRefresh")->setEnabled(!on);
}

void AudioPanel::setMuted(int ch, bool on) { if (ch >= 0 && ch < int(strips_.size())) strips_[size_t(ch)].mute->setChecked(on); }
void AudioPanel::setVolumePercent(int ch, int pct) { if (ch >= 0 && ch < int(strips_.size())) strips_[size_t(ch)].vol->setValue(pct); }

void AudioPanel::refreshMeters() {
    AudioMixer& m = engine_.audio();
    for (size_t i = 0; i < strips_.size(); ++i) {
        Strip& s = strips_[i];
        const AudioMeter& mt = m.channelMeter(int(i));
        s.meter->setLevels(mt.peak[0], mt.peak[1], mt.rms[0], mt.rms[1]);
        // dong bo voi trang thai that (co the doi tu HTTP/OSC): khong ghi de khi nguoi dung dang keo
        const Channel& ch = *project_.channels[i];
        const bool mu = ch.mute.load();
        if (s.mute->isChecked() != mu) {
            QSignalBlocker b(s.mute);
            s.mute->setChecked(mu);
            s.mute->setIcon(glyphIcon(mu ? Glyph::Muted : Glyph::Speaker));
        }
        const int pct = int(std::lround(ch.volume.load() * 100));
        if (!s.vol->isSliderDown() && s.vol->value() != pct) { QSignalBlocker b(s.vol); s.vol->setValue(pct); }
        s.volText->setText(QString("%1%").arg(s.vol->value()));
        s.name->setStyleSheet(mu ? "color:#8a90a2;text-decoration:line-through" : QString());
    }
    const AudioMeter& mm = m.masterMeter();
    master_->setLevels(mm.peak[0], mm.peak[1], mm.rms[0], mm.rms[1]);
    static int tick = 0;
    if (++tick % 8 == 0) updateStatus();
}

} // namespace lc
