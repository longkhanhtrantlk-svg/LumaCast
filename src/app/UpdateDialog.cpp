#include "UpdateDialog.h"
#include "Text.h"
#include "core/I18n.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace lc {

using State = update::Service::State;

UpdateDialog::UpdateDialog(update::Service& service, std::function<void()> onApply, std::function<void()> onRollback, QWidget* parent)
    : QDialog(parent), service_(service) {
    setObjectName("updateDialog");
    setWindowTitle(LC_TR("Cập nhật LumaCast"));
    setWindowFlag(Qt::Tool);               // phim tat cua cua so chinh van chay khi hop thoai dang duoc chon
    setMinimumWidth(460);

    auto* root = new QVBoxLayout(this);
    auto label = [&](const char* name) {
        auto* l = new QLabel;
        l->setObjectName(name);
        l->setWordWrap(true);
        root->addWidget(l);
        return l;
    };
    current_ = label("updCurrent");
    status_ = label("updStatus");
    progress_ = new QProgressBar;
    progress_->setObjectName("updProgress");
    progress_->setRange(0, 100);
    root->addWidget(progress_);
    notes_ = label("updNotes");
    notes_->setTextFormat(Qt::PlainText);

    auto* row = new QHBoxLayout;
    auto button = [&](const char* name, const QString& text, QHBoxLayout* into) {
        auto* b = new QPushButton(text);
        b->setObjectName(name);
        b->setAutoDefault(false);
        b->setDefault(false);
        b->setFocusPolicy(Qt::NoFocus);
        into->addWidget(b);
        return b;
    };
    check_ = button("updCheck", LC_TR("Kiểm tra cập nhật"), row);
    download_ = button("updDownload", LC_TR("Tải về"), row);
    apply_ = button("updApply", LC_TR("Cập nhật và mở lại"), row);
    row->addStretch(1);
    root->addLayout(row);

    auto* bottom = new QHBoxLayout;
    auto_ = new QCheckBox(LC_TR("Tự kiểm tra khi mở LumaCast"));
    auto_->setObjectName("updAuto");
    auto_->setFocusPolicy(Qt::NoFocus);
    bottom->addWidget(auto_);
    bottom->addStretch(1);
    rollback_ = button("updRollback", QString(), bottom);
    auto* close = button("updClose", LC_TR("Đóng"), bottom);
    root->addLayout(bottom);

    connect(check_, &QPushButton::clicked, this, [this] { service_.checkNow(); });
    connect(download_, &QPushButton::clicked, this, [this] { service_.download(); });
    connect(apply_, &QPushButton::clicked, this, [onApply] { onApply(); });
    connect(rollback_, &QPushButton::clicked, this, [onRollback] { onRollback(); });
    connect(auto_, &QCheckBox::toggled, this, [this](bool on) { service_.setAutoCheck(on); });
    connect(close, &QPushButton::clicked, this, &QDialog::hide);
    connect(&service_, &update::Service::changed, this, [this] { sync(); });
    sync();
}

void UpdateDialog::sync() {
    const State s = service_.state();
    const bool locked = service_.locked();
    const update::Release& r = service_.release();
    current_->setText(QString(LC_TR("Đang dùng bản %1")).arg(service_.currentVersion()));

    const QString size = r.downloadBytes > 0 ? QString("%1 MB").arg(QString::number(r.downloadBytes / 1048576.0, 'f', 1)) : QString("?");
    QString text;
    switch (s) {
    case State::Unsupported: text = service_.message(); break;
    case State::Idle: text = LC_TR("Chưa kiểm tra."); break;
    case State::Checking: text = LC_TR("Đang kiểm tra…"); break;
    case State::UpToDate: text = LC_TR("Bạn đang dùng bản mới nhất."); break;
    case State::Available:
        text = QString(r.delta ? LC_TR("Có bản mới %1 (tải phần thay đổi: %2).") : LC_TR("Có bản mới %1 (tải: %2).")).arg(r.version, size);
        if (!service_.message().isEmpty()) text += "\n" + service_.message();
        break;
    case State::Downloading: text = QString(LC_TR("Đang tải bản %1…")).arg(r.version); break;
    case State::Ready: text = QString(LC_TR("Đã tải bản %1. Bấm \"Cập nhật và mở lại\" khi không còn diễn.")).arg(r.version); break;
    case State::Failed: text = service_.message(); break;
    }
    if (locked) text += "\n" + LC_TR("Đang khoá vận hành: không kiểm tra, tải hay cài.");
    status_->setText(text);
    progress_->setVisible(s == State::Downloading);
    progress_->setValue(service_.progress());
    notes_->setText(r.notes.left(1500));
    notes_->setVisible(!r.notes.isEmpty() && (s == State::Available || s == State::Downloading || s == State::Ready));

    check_->setEnabled(!locked && s != State::Unsupported && !service_.busy() && s != State::Ready);
    download_->setVisible(s == State::Available);
    download_->setEnabled(!locked);
    apply_->setVisible(s == State::Ready);
    apply_->setEnabled(!locked);
    const QString previous = service_.previousVersion();
    rollback_->setVisible(!previous.isEmpty());
    rollback_->setText(QString(LC_TR("Quay về bản %1")).arg(previous));
    rollback_->setEnabled(!locked && !service_.busy());
    auto_->setEnabled(s != State::Unsupported);
    const QSignalBlocker block(auto_);
    auto_->setChecked(service_.autoCheck());
    for (QPushButton* b : {check_, download_, apply_, rollback_}) b->setToolTip(locked ? text::lockedWhy() : QString());
}

} // namespace lc
