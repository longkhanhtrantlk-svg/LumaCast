#pragma once
#include "core/Project.h"
#include "render/Engine.h"
#include <QWidget>
#include <vector>

class QComboBox;
class QLabel;
class QSlider;
class QToolButton;

namespace lc {

// Dong ho muc (dB) hai kenh trai/phai: thanh RMS + vach dinh. Thang -60..0 dBFS; vang tu -12, do tu -3.
class LevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeter(QWidget* parent = nullptr);
    void setLevels(float peakL, float peakR, float rmsL, float rmsR);   // gia tri tuyen tinh
    QSize sizeHint() const override { return {160, 18}; }
    static double toFraction(float lin);                                  // tuyen tinh -> 0..1 tren thang dB
    float peak(int k) const { return peak_[k]; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    float peak_[2] = {0, 0}, rms_[2] = {0, 0};
};

// "Am thanh": mot dai cho moi Channel (nut tat tieng, thanh am luong, meter) + meter master + chon thiet bi ra.
// Tat tieng / am luong la dieu khien LIVE (van dung duoc khi khoa van hanh); doi thiet bi la thao tac SUA (bi khoa cung Cai dat).
// Moi thay doi di qua RenderEngine::post(); meter doc bang atomic cua mixer.
class AudioPanel : public QWidget {
    Q_OBJECT
public:
    AudioPanel(Project& project, RenderEngine& engine, QWidget* parent = nullptr);
    void refreshMeters();                  // goi theo timer UI (~15 lan/giay): meter + dong bo nut/thanh voi trang thai that
    void setEditLocked(bool on);           // khoa van hanh: vo hieu chon thiet bi
    void reloadDevices();
    // Dung thiet bi (id) cho project + mixer. Tra ve loi (rong = ok; thiet bi hong van chay bang Null va tra ve ly do).
    QString applyDevice(const QString& id);
    // De test
    void setMuted(int ch, bool on);
    void setVolumePercent(int ch, int pct);
signals:
    void deviceChanged(const QString& id);

private:
    struct Strip { QLabel* name; QToolButton* mute; QSlider* vol; QLabel* volText; LevelMeter* meter; };
    void updateStatus();
    Project& project_;
    RenderEngine& engine_;
    std::vector<Strip> strips_;
    LevelMeter* master_ = nullptr;
    QComboBox* device_ = nullptr;
    QLabel* status_ = nullptr;
    bool building_ = true;
};

} // namespace lc
