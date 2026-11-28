#pragma once
#include "live/Camera.h"
#include "live/Ndi.h"
#include "live/WindowCapture.h"
#include <QDialog>
#include <atomic>
#include <memory>
#include <thread>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace lc {

// Chon nguon NDI tren mang (liet ke o thread rieng, khong dong bang giao dien) hoac nhap tay ten/dia chi.
class NdiSourceDialog : public QDialog {
    Q_OBJECT
public:
    explicit NdiSourceDialog(QWidget* parent = nullptr);
    ~NdiSourceDialog() override;
    QString name() const;               // ten nguon NDI da chon/nhap
    QString url() const;
    QString label() const;
    void refresh();                     // liet ke lai (bat dong bo)
    bool refreshing() const { return busy_; }
signals:
    void listed(const QStringList& names, const QStringList& urls);
private:
    void onListed(const QStringList& names, const QStringList& urls);
    QLabel* note_;
    QListWidget* list_;
    QLineEdit* nameEdit_;
    QLineEdit* labelEdit_;
    QPushButton* refreshBtn_;
    QPushButton* ok_;
    QStringList urls_;
    std::atomic<bool> busy_{false};
    std::thread th_;
};

// Chon camera/card capture + che do (do phan giai @ fps).
class CameraSourceDialog : public QDialog {
    Q_OBJECT
public:
    explicit CameraSourceDialog(QWidget* parent = nullptr);
    QString deviceId() const;
    QString deviceName() const;
    CameraFormat format() const;        // {0,0,0} = de thiet bi chon
    QString label() const;
    void refresh();                     // liet ke lai thiet bi (UI thread: nhanh; che do duoc liet ke khi chon thiet bi)
private:
    void onDeviceChanged();
    QLabel* note_;
    QComboBox* dev_;
    QComboBox* fmt_;
    QLineEdit* labelEdit_;
    QPushButton* ok_;
    std::vector<CameraDevice> devs_;
    std::vector<CameraFormat> fmts_;
};

// Them Output NDI (OUT-13).
class NdiOutputDialog : public QDialog {
    Q_OBJECT
public:
    NdiOutputDialog(const QStringList& channelNames, QWidget* parent = nullptr);
    QString name() const;
    int channel() const;
    QSize size() const;
    bool audio() const;
private:
    QLabel* note_;
    QLineEdit* name_;
    QComboBox* channel_;
    QSpinBox *w_, *h_;
    QCheckBox* audio_;
};

// Chon cua so can bat (SRC-09). Muc dau tien la slideshow PowerPoint tu nhan; cac muc sau la cua so dang mo.
class WindowSourceDialog : public QDialog {
    Q_OBJECT
public:
    explicit WindowSourceDialog(QWidget* parent = nullptr);
    WindowTarget target() const;        // app rong = chua chon
    QString label() const;
    void refresh();
private:
    QLabel* note_;
    QListWidget* list_;
    QLineEdit* labelEdit_;
    QPushButton* ok_;
    std::vector<WindowTarget> targets_;
};

} // namespace lc
