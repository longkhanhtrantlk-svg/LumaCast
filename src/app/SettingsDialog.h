#pragma once
#include "control/ControlService.h"
#include "core/Startup.h"
#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace lc {

// Hop thoai cai dat van hanh: dieu khien ngoai (HTTP/SSE, OSC). Luu bang QSettings va ap dung ngay (khong can khoi dong lai).
// Loi (vd. LAN ma khong co token, cong dang bi chiem) hien ngay tren hop thoai va hop thoai KHONG dong.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    // runKey/exePath: chi de test (QSettings giả thay cho registry Windows). Mặc định: registry HKCU\...\Run (chỉ Windows) và exe hiện tại.
    SettingsDialog(ControlService& control, QWidget* parent = nullptr, QSettings* runKey = nullptr, QString exePath = {});

    // Ap dung + luu; tra ve loi (rong = thanh cong). Cung la diem vao cho test.
    QString applyAndSave();

public slots:
    void accept() override;

private:
    void loadFromSettings();
    void updateUi();
    ControlSettings collect() const;

    ControlService& control_;
    std::unique_ptr<QSettings> runKeyOwned_;
    QSettings* runKey_ = nullptr;
    QString exe_;
    QCheckBox *autostart_ = nullptr, *reopen_ = nullptr;
    QLabel* autoHint_ = nullptr;
    QCheckBox *httpOn_ = nullptr, *httpLan_ = nullptr, *oscOn_ = nullptr, *oscLan_ = nullptr, *showTok_ = nullptr;
    QSpinBox *httpPort_ = nullptr, *oscPort_ = nullptr;
    QLineEdit* token_ = nullptr;
    QPushButton* genTok_ = nullptr;
    QLabel *httpHint_ = nullptr, *oscHint_ = nullptr, *err_ = nullptr;
};

} // namespace lc
