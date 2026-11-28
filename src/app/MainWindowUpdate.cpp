// Tu cap nhat o cua so chinh: nut bao ban moi o thanh trang thai, hop thoai, va duong DUY NHAT dan toi cai dat:
// nguoi van hanh bam -> xac nhan -> luu project -> trinh cap nhat cho app thoat. Dang khoa van hanh thi khong co gi chay.
#include "MainWindow.h"
#include "UpdateDialog.h"
#include "core/AppSettings.h"
#include "core/I18n.h"
#include "core/Project.h"
#include "core/Startup.h"
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QStatusBar>
#include <chrono>
#include <future>

namespace lc {

void MainWindow::setupUpdates() {
    statusUpdate_ = new QPushButton;
    statusUpdate_->setObjectName("statusUpdate");
    statusUpdate_->setStyleSheet("background:#1f5fbf;color:white;font-weight:bold;padding:0 8px");
    statusUpdate_->setFocusPolicy(Qt::NoFocus);            // Space/Enter van la TAKE/CUT
    statusUpdate_->setVisible(false);
    statusBar()->insertPermanentWidget(0, statusUpdate_);
    connect(statusUpdate_, &QPushButton::clicked, this, [this] { showUpdateDialog(); });
    updateConfirm_ = [this](const QString& question) {
        return QMessageBox::question(this, LC_TR("Cập nhật LumaCast"), question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    };
    askSavePath_ = [this](const QString& title, const QString& suggestion) {
        return QFileDialog::getSaveFileName(this, title, suggestion, "LumaCast project (*.json)");
    };
    warnUser_ = [this](const QString& text) { QMessageBox::warning(this, "LumaCast", text); };
    setUpdateBackend(update::createBackend(QString()));
}

// Hop thoai luu mo o Tai lieu: thu muc lam viec cua ban da cai la thu muc cai (bi thay khi cap nhat).
QString MainWindow::defaultSavePath(const QString& fileName) const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (dir.isEmpty()) dir = QDir::homePath();
    return QDir(dir).filePath(fileName);
}

bool MainWindow::refuseWipedPath(const QString& path) {
    if (!updates_ || !update::isInside(path, updates_->installDir())) return false;
    warnUser_(QString(LC_TR("Không lưu vào thư mục cài của LumaCast:\n%1\n\nThư mục này bị thay khi cập nhật và bị xoá khi gỡ cài, file sẽ mất. Hãy chọn chỗ khác (vd. Documents)."))
                  .arg(QDir::toNativeSeparators(path)));
    return true;
}

void MainWindow::setUpdateBackend(std::shared_ptr<update::Backend> backend) {
    delete updateDlg_;
    updateDlg_ = nullptr;
    delete updates_;
    updates_ = new update::Service(std::move(backend), this);
    updates_->setLockProvider([this] { return locked_; });
    connect(updates_, &update::Service::changed, this, [this] { refreshUpdateUi(); });
    refreshUpdateUi();
    if (update::isInside(project_.filePath, updates_->installDir()))
        showStartupNotice(LC_TR("Project đang mở nằm trong thư mục cài của LumaCast, sẽ mất khi cập nhật. Dùng Lưu thành… để chuyển ra chỗ khác."));
}

void MainWindow::refreshUpdateUi() {
    if (!updates_ || !statusUpdate_) return;
    const bool notify = updates_->shouldNotify();          // khoa van hanh: khong nhac
    if (notify) statusUpdate_->setText(QString(LC_TR("⬆ Có bản mới %1")).arg(updates_->release().version));
    statusUpdate_->setVisible(notify);
    if (updateDlg_) updateDlg_->sync();
}

void MainWindow::showUpdateDialog() {
    if (!updateDlg_)
        updateDlg_ = new UpdateDialog(*updates_, [this] { applyUpdate(false); }, [this] { applyUpdate(true); }, this);
    updateDlg_->sync();
    updateDlg_->show();
    updateDlg_->raise();
}

bool MainWindow::saveProjectNow() {
    const QString path = project_.filePath;
    struct Result { bool ok = false; QString err; };
    auto done = std::make_shared<std::promise<Result>>();
    std::future<Result> result = done->get_future();
    engine_.post([this, path, done](double) {
        engine_.syncOutputsToProject();                     // lay cau hinh Output hien tai, tren render thread
        Result r;
        r.ok = lc::saveProject(project_, path, &r.err);
        done->set_value(r);
    });
    if (result.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
        showMessage(LC_TR("Lỗi lưu: ") + LC_TR("render không phản hồi"));
        return false;
    }
    const Result r = result.get();
    if (!r.ok) { showMessage(LC_TR("Lỗi lưu: ") + r.err); return false; }
    QFile::remove(path + ".autosave");
    QSettings st = appSettings();
    rememberProject(st, path);
    return true;
}

bool MainWindow::applyUpdate(bool rollback) {
    if (locked_) { showMessage(LC_TR("Đang khoá vận hành")); return false; }
    const bool possible = rollback ? !updates_->previousVersion().isEmpty() : updates_->state() == update::Service::State::Ready;
    if (!possible) return false;
    const QString version = rollback ? updates_->previousVersion() : updates_->release().version;
    const QString question = QString(rollback ? LC_TR("Quay về bản %1?\n\nLumaCast sẽ lưu project, TẮT TẤT CẢ OUTPUT, đổi bản rồi mở lại. Đừng làm việc này khi đang diễn.")
                                              : LC_TR("Cập nhật lên bản %1?\n\nLumaCast sẽ lưu project, TẮT TẤT CẢ OUTPUT, cài bản mới rồi mở lại. Đừng làm việc này khi đang diễn."))
                                 .arg(version);
    if (!updateConfirm_(question)) return false;
    if (locked_) return false;
    // Project chua co file, hoac nam trong thu muc cai (se bi xoa): phai luu ra cho khac truoc; huy = khong cap nhat
    if (project_.filePath.isEmpty() || update::isInside(project_.filePath, updates_->installDir())) {
        saveProject(true);
        if (project_.filePath.isEmpty() || update::isInside(project_.filePath, updates_->installDir())) return false;
    }
    if (!saveProjectNow()) return false;
    QString err;
    const bool ok = rollback ? updates_->rollback(&err) : updates_->apply({"--reopen", project_.filePath}, &err);
    if (!ok) { showMessage(err); return false; }
    askOnClose_ = false;
    close();                                                // trinh cap nhat dang cho tien trinh nay thoat
    return true;
}

} // namespace lc
