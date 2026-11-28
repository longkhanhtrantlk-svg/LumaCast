#include "SettingsDialog.h"
#include "core/I18n.h"
#include "core/AppSettings.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lc {

SettingsDialog::SettingsDialog(ControlService& control, QWidget* parent, QSettings* runKey, QString exePath)
    : QDialog(parent), control_(control), exe_(exePath.isEmpty() ? QCoreApplication::applicationFilePath() : std::move(exePath)) {
    if (runKey) runKey_ = runKey;
    else { runKeyOwned_ = openRunKey(); runKey_ = runKeyOwned_.get(); }
    setWindowTitle(LC_TR("Cài đặt vận hành"));
    setObjectName("settingsDialog");
    auto* root = new QVBoxLayout(this);

    // ---- Khoi dong (OPS-01) ----
    auto* sg = new QGroupBox(LC_TR("Khởi động"));
    auto* sl = new QVBoxLayout(sg);
    autostart_ = new QCheckBox(LC_TR("Khởi động LumaCast cùng Windows"));
    autostart_->setObjectName("autostart");
    reopen_ = new QCheckBox(LC_TR("Tự mở lại project gần nhất khi khởi động (nếu không chỉ định --project)"));
    reopen_->setObjectName("reopenLast");
    autoHint_ = new QLabel;
    autoHint_->setWordWrap(true);
    autoHint_->setObjectName("autostartHint");
    sl->addWidget(autostart_);
    sl->addWidget(reopen_);
    sl->addWidget(autoHint_);
    root->addWidget(sg);

    // ---- HTTP ----
    auto* hg = new QGroupBox(LC_TR("Điều khiển ngoài — HTTP / SSE (điện thoại, Companion, script)"));
    auto* hf = new QFormLayout(hg);
    httpOn_ = new QCheckBox(LC_TR("Bật máy chủ HTTP"));
    httpOn_->setObjectName("httpEnable");
    httpPort_ = new QSpinBox;
    httpPort_->setObjectName("httpPort");
    httpPort_->setRange(1, 65535);
    httpLan_ = new QCheckBox(LC_TR("Cho phép máy khác trong mạng LAN (BẮT BUỘC có token)"));
    httpLan_->setObjectName("httpLan");
    token_ = new QLineEdit;
    token_->setObjectName("httpToken");
    token_->setEchoMode(QLineEdit::Password);
    token_->setPlaceholderText(LC_TR("Token (gửi trong header Authorization: Bearer …)"));
    genTok_ = new QPushButton(LC_TR("Tạo token ngẫu nhiên"));
    genTok_->setObjectName("genToken");
    showTok_ = new QCheckBox(LC_TR("Hiện token"));
    auto* tokRow = new QHBoxLayout;
    tokRow->addWidget(token_, 1);
    tokRow->addWidget(showTok_);
    tokRow->addWidget(genTok_);
    httpHint_ = new QLabel;
    httpHint_->setWordWrap(true);
    hf->addRow(httpOn_);
    hf->addRow(LC_TR("Cổng"), httpPort_);
    hf->addRow(httpLan_);
    hf->addRow("Token", tokRow);
    hf->addRow(httpHint_);
    root->addWidget(hg);

    // ---- OSC ----
    auto* og = new QGroupBox(LC_TR("Điều khiển ngoài — OSC qua UDP"));
    auto* of = new QFormLayout(og);
    oscOn_ = new QCheckBox(LC_TR("Bật nhận OSC"));
    oscOn_->setObjectName("oscEnable");
    oscPort_ = new QSpinBox;
    oscPort_->setObjectName("oscPort");
    oscPort_->setRange(1, 65535);
    oscLan_ = new QCheckBox(LC_TR("Cho phép máy khác trong mạng LAN (OSC KHÔNG có xác thực)"));
    oscLan_->setObjectName("oscLan");
    oscHint_ = new QLabel;
    oscHint_->setWordWrap(true);
    of->addRow(oscOn_);
    of->addRow(LC_TR("Cổng"), oscPort_);
    of->addRow(oscLan_);
    of->addRow(oscHint_);
    root->addWidget(og);

    err_ = new QLabel;
    err_->setObjectName("settingsError");
    err_->setStyleSheet("color:#ff6b6b;font-weight:bold");
    err_->setWordWrap(true);
    root->addWidget(err_);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    root->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    connect(genTok_, &QPushButton::clicked, this, [this] { token_->setText(ControlSettings::generateToken()); showTok_->setChecked(true); });
    connect(showTok_, &QCheckBox::toggled, this, [this](bool on) { token_->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password); });
    connect(httpLan_, &QCheckBox::toggled, this, [this](bool on) { if (on && token_->text().isEmpty()) token_->setText(ControlSettings::generateToken()); updateUi(); });
    for (QCheckBox* c : {httpOn_, oscOn_, oscLan_}) connect(c, &QCheckBox::toggled, this, [this] { updateUi(); });
    connect(httpPort_, &QSpinBox::valueChanged, this, [this] { updateUi(); });
    connect(oscPort_, &QSpinBox::valueChanged, this, [this] { updateUi(); });
    loadFromSettings();
}

void SettingsDialog::loadFromSettings() {
    QSettings st = appSettings();
    reopen_->setChecked(reopenLastEnabled(st));
    const QString last = lastProjectPath(st);
    QString hint = last.isEmpty() ? LC_TR("Chưa có project nào được nhớ.") : LC_TR("Project gần nhất: ") + QDir::toNativeSeparators(last);
    if (!runKey_) {
        autostart_->setEnabled(false);
        hint += LC_TR("\nKhởi động cùng hệ điều hành chỉ hỗ trợ trên Windows.");
    } else {
        const AutostartState as = autostartState(*runKey_, exe_);
        autostart_->setChecked(as != AutostartState::Off);
        if (as == AutostartState::OnOtherPath) hint += LC_TR("\n⚠ Đang bật nhưng trỏ tới đường dẫn khác (đã di chuyển?). Bấm OK để cập nhật về: ") + QDir::toNativeSeparators(exe_);
    }
    autoHint_->setText(hint);
    const ControlSettings s = ControlSettings::load(st);
    httpOn_->setChecked(s.httpEnabled);
    httpPort_->setValue(s.httpPort);
    httpLan_->setChecked(s.httpLan);
    token_->setText(s.token);
    oscOn_->setChecked(s.oscEnabled);
    oscPort_->setValue(s.oscPort);
    oscLan_->setChecked(s.oscLan);
    updateUi();
}

void SettingsDialog::updateUi() {
    httpPort_->setEnabled(httpOn_->isChecked());
    httpLan_->setEnabled(httpOn_->isChecked());
    token_->setEnabled(httpOn_->isChecked());
    genTok_->setEnabled(httpOn_->isChecked());
    oscPort_->setEnabled(oscOn_->isChecked());
    oscLan_->setEnabled(oscOn_->isChecked());
    httpHint_->setText(httpLan_->isChecked()
                           ? QString(LC_TR("Máy khác truy cập qua <b>http://&lt;IP máy này&gt;:%1/</b>. Mọi yêu cầu phải có header <code>Authorization: Bearer &lt;token&gt;</code>. Hãy dùng mạng điều khiển riêng; HTTP không mã hoá.")).arg(httpPort_->value())
                           : QString(LC_TR("Chỉ máy này truy cập được: <b>http://127.0.0.1:%1/</b>. Token tuỳ chọn.")).arg(httpPort_->value()));
    oscHint_->setText(oscLan_->isChecked()
                          ? QString(LC_TR("<b>Cảnh báo:</b> OSC không có xác thực — bất kỳ máy nào trong mạng gửi tới UDP %1 đều điều khiển được. Chỉ bật trên mạng riêng, tốt nhất kèm tường lửa.")).arg(oscPort_->value())
                          : QString(LC_TR("Chỉ nhận từ máy này (127.0.0.1:%1). Địa chỉ: /lumacast/channel/1/preview 3 …")).arg(oscPort_->value()));
}

ControlSettings SettingsDialog::collect() const {
    ControlSettings s;
    s.httpEnabled = httpOn_->isChecked();
    s.httpPort = quint16(httpPort_->value());
    s.httpLan = httpLan_->isChecked();
    s.token = token_->text().trimmed();
    s.oscEnabled = oscOn_->isChecked();
    s.oscPort = quint16(oscPort_->value());
    s.oscLan = oscLan_->isChecked();
    return s;
}

QString SettingsDialog::applyAndSave() {
    const ControlSettings s = collect();
    QString err = control_.apply(s);                       // tu log bat/tat; LAN khong token se bi tu choi tai day
    {
        QSettings stp = appSettings();
        setReopenLast(stp, reopen_->isChecked());
    }
    if (runKey_) {
        const AutostartState as = autostartState(*runKey_, exe_);
        const bool want = autostart_->isChecked();
        if (want != (as != AutostartState::Off) || (want && as == AutostartState::OnOtherPath)) {
            if (setAutostart(*runKey_, want, exe_)) qInfo().noquote() << QString("Khởi động cùng Windows: %1 (%2)").arg(want ? "BẬT" : "TẮT", want ? autostartCommand(exe_) : QString());
            else {
                const i18n::Text e = LC_BOTH(LC_TR("Không ghi được khoá khởi động cùng Windows (kiểm tra quyền/đường dẫn)"));
                qWarning().noquote() << e.source;
                err += (err.isEmpty() ? "" : "\n") + e.shown;
            }
        }
    }
    err_->setText(err);
    QSettings st = appSettings();
    s.save(st);                                            // luu ca khi cong bi chiem: de nguoi dung sua tiep, khong mat nhap
    return err;
}

void SettingsDialog::accept() {
    if (applyAndSave().isEmpty()) QDialog::accept();       // co loi: giu hop thoai de sua
}

} // namespace lc
