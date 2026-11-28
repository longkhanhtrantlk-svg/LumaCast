// Backend tu cap nhat. Co thu vien Velopack (CMake: LUMACAST_VELOPACK_DIR) thi dung that; khong thi "khong ho tro".
// Bo cuc ban cai cua Velopack: %LocalAppData%\LumaCast\{current\, packages\, Update.exe}. Ban truoc duoc sao ra
// %LocalAppData%\LumaCast-rollback\ (ngoai thu muc cai) kem rollback.cmd + loi tat o menu Start, de van quay ve duoc
// ke ca khi ban moi khong mo noi.
#include "Update.h"
#include "core/I18n.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVersionNumber>

#ifdef LUMACAST_HAS_VELOPACK
#include <Velopack.h>
#endif

namespace lc::update {

namespace {

class NoBackend : public Backend {
public:
    explicit NoBackend(const char* why) : why_(why) {}
    bool supported(QString* why) const override {
        if (why) *why = i18n::translate(why_);
        return false;
    }
    QString currentVersion() const override { return QCoreApplication::applicationVersion(); }
    CheckResult check(Release&, QString*) override { return CheckResult::Failed; }
    bool download(const std::function<void(int)>&, QString*) override { return false; }
    bool applyAndRestart(const QStringList&, QString*) override { return false; }
    QString previousVersion() const override { return {}; }
    bool rollback(QString*) override { return false; }

private:
    const char* why_;
};

#ifdef LUMACAST_HAS_VELOPACK

constexpr const char* kShortcutName = "LumaCast - Quay ve ban truoc.lnk";

QString localAppData() { return QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")); }
QString backupDir() { return localAppData() + "/LumaCast-rollback"; }
QString startMenuShortcut() {
    return QDir::fromNativeSeparators(qEnvironmentVariable("APPDATA")) + "/Microsoft/Windows/Start Menu/Programs/" + kShortcutName;
}
QString rootDir() { return QDir::cleanPath(QCoreApplication::applicationDirPath() + "/.."); }      // exe nam trong <goc>/current

// "LumaCast-1.2.3-full.nupkg" -> "1.2.3"
QString versionOfPackage(const QString& fileName) {
    static const QRegularExpression re(R"(^LumaCast-(\d+\.\d+\.\d+(?:-[0-9A-Za-z.]+)?)(?:-win)?-full\.nupkg$)");
    const auto m = re.match(fileName);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString lastError() {
    std::string buf(1024, '\0');
    vpkc_get_last_error(buf.data(), buf.size());
    return QString::fromUtf8(buf.c_str());
}

class VelopackBackend : public Backend {
public:
    explicit VelopackBackend(const QString& source) : source_(source) {
        vpkc_update_manager_t* m = open();
        if (!m) return;
        std::string buf(256, '\0');
        vpkc_get_current_version(m, buf.data(), buf.size());
        current_ = QString::fromUtf8(buf.c_str());
        vpkc_free_update_manager(m);
    }
    ~VelopackBackend() override { forget(); }

    bool supported(QString* why) const override {
        if (!current_.isEmpty()) return true;
        if (why) *why = LC_TR("Bản này không cài bằng bộ cài (chạy từ thư mục build hoặc bản nén) nên không tự cập nhật được.");
        return false;
    }
    QString currentVersion() const override { return current_.isEmpty() ? QCoreApplication::applicationVersion() : current_; }
    QString installDir() const override { return current_.isEmpty() ? QString() : rootDir(); }

    CheckResult check(Release& out, QString* err) override {
        forget();
        manager_ = open();
        if (!manager_) { if (err) *err = lastError(); return CheckResult::Failed; }
        const vpkc_update_check_t r = vpkc_check_for_updates(manager_, &info_);
        if (r == NO_UPDATE_AVAILABLE || r == REMOTE_IS_EMPTY) return CheckResult::UpToDate;
        if (r != UPDATE_AVAILABLE || !info_ || !info_->TargetFullRelease) { if (err) *err = lastError(); return CheckResult::Failed; }
        if (info_->IsDowngrade) return CheckResult::UpToDate;                  // quay ve ban cu la viec rieng (rollback)
        out.version = QString::fromUtf8(info_->TargetFullRelease->Version);
        out.notes = QString::fromUtf8(info_->TargetFullRelease->NotesMarkdown ? info_->TargetFullRelease->NotesMarkdown : "");
        out.delta = info_->DeltasToTargetCount > 0;
        out.downloadBytes = qint64(info_->TargetFullRelease->Size);
        if (out.delta) {
            out.downloadBytes = 0;
            for (size_t i = 0; i < info_->DeltasToTargetCount; ++i) out.downloadBytes += qint64(info_->DeltasToTarget[i]->Size);
        }
        return CheckResult::Available;
    }

    bool download(const std::function<void(int)>& progress, QString* err) override {
        if (!manager_ || !info_) { if (err) *err = LC_TR("Chưa có bản cập nhật đã tải"); return false; }
        // PHAI sao luu TRUOC khi tai: Velopack xoa goi day du cua ban dang chay ngay khi tai xong ban moi.
        if (!backUp(err)) return false;
        auto cb = [](void* user, size_t percent) { (*static_cast<const std::function<void(int)>*>(user))(int(percent)); };
        if (vpkc_download_updates(manager_, info_, cb, const_cast<std::function<void(int)>*>(&progress))) return true;
        if (err) *err = lastError();
        return false;
    }

    bool applyAndRestart(const QStringList& args, QString* err) override {
        if (!manager_ || !info_) { if (err) *err = LC_TR("Chưa có bản cập nhật đã tải"); return false; }
        if (!QFileInfo::exists(backupDir() + "/" + backupName_) || backupName_.isEmpty()) {      // chua co ban sao thi khong cap nhat
            if (!backUp(err)) return false;
        }
        std::vector<QByteArray> bytes;
        std::vector<char*> argv;
        for (const QString& a : args) bytes.push_back(a.toUtf8());
        for (QByteArray& b : bytes) argv.push_back(b.data());
        if (vpkc_wait_exit_then_apply_updates(manager_, info_->TargetFullRelease, true, true, argv.empty() ? nullptr : argv.data(), argv.size()))
            return true;
        if (err) *err = lastError();
        return false;
    }

    QString previousVersion() const override {
        const QFileInfo p = backupPackage();
        return p.fileName().isEmpty() ? QString() : versionOfPackage(p.fileName());
    }

    bool rollback(QString* err) override {
        const QFileInfo backup = backupPackage();
        if (backup.fileName().isEmpty()) { if (err) *err = LC_TR("Không có bản trước để quay về"); return false; }
        // Update.exe co the don file goi: dua cho no mot ban sao
        const QString temp = QDir::tempPath() + "/" + backup.fileName();
        QFile::remove(temp);
        if (!QFile::copy(backup.absoluteFilePath(), temp)) { if (err) *err = LC_TR("Không sao chép được gói của bản trước"); return false; }
        const QStringList args{"apply", "--package", QDir::toNativeSeparators(temp), "--waitPid", QString::number(QCoreApplication::applicationPid()), "--silent"};
        if (QProcess::startDetached(rootDir() + "/Update.exe", args)) return true;
        if (err) *err = LC_TR("Không chạy được Update.exe");
        return false;
    }

private:
    vpkc_update_manager_t* open() const {
        vpkc_update_manager_t* m = nullptr;
        const QByteArray src = source_.toUtf8();
        if (source_.startsWith("https://github.com/", Qt::CaseInsensitive)) {
            vpkc_update_source_t* gh = vpkc_new_source_github(src.constData(), nullptr, false);
            if (!gh || !vpkc_new_update_manager_with_source(gh, nullptr, nullptr, &m)) m = nullptr;
            if (gh) vpkc_free_source(gh);
        } else if (!vpkc_new_update_manager(src.constData(), nullptr, nullptr, &m)) {
            m = nullptr;
        }
        return m;
    }
    void forget() {
        if (info_) vpkc_free_update_info(info_);
        if (manager_) vpkc_free_update_manager(manager_);
        info_ = nullptr;
        manager_ = nullptr;
    }
    // Ban moi nhat trong thu muc sao luu ma KHAC ban dang chay.
    QFileInfo backupPackage() const {
        QFileInfo best;
        QVersionNumber bestVersion;
        for (const QFileInfo& f : QDir(backupDir()).entryInfoList({"*.nupkg"}, QDir::Files)) {
            const QString v = versionOfPackage(f.fileName());
            if (v.isEmpty() || v == current_) continue;
            const QVersionNumber n = QVersionNumber::fromString(v);
            if (best.fileName().isEmpty() || n > bestVersion) { best = f; bestVersion = n; }
        }
        return best;
    }
    // Giu goi day du cua ban dang chay truoc khi tai ban moi: Velopack don goi cu, khong giu ho.
    bool backUp(QString* err) {
        auto fail = [&] { if (err) *err = LC_TR("Không sao lưu được bản đang chạy (cần để quay về) nên chưa cập nhật."); return false; };
        QFileInfo package;
        for (const QFileInfo& f : QDir(rootDir() + "/packages").entryInfoList({"*.nupkg"}, QDir::Files))
            if (versionOfPackage(f.fileName()) == current_) package = f;
        if (package.fileName().isEmpty()) {
            // da sao luu o lan tai truoc (goi trong thu muc cai bi don mat roi): dung ban sao do
            for (const QFileInfo& f : QDir(backupDir()).entryInfoList({"*.nupkg"}, QDir::Files))
                if (versionOfPackage(f.fileName()) == current_) { backupName_ = f.fileName(); return true; }
            qWarning().noquote() << "Cap nhat: khong thay goi day du cua ban" << current_ << "trong" << rootDir() + "/packages";
            return fail();
        }
        if (!QDir().mkpath(backupDir())) return fail();
        const QString target = backupDir() + "/" + package.fileName();
        if (QFileInfo(target).size() != package.size()) {
            QFile::remove(target);
            if (!QFile::copy(package.absoluteFilePath(), target)) return fail();
        }
        // giu ban dang chay + mot ban cu moi nhat (con quay ve duoc neu nguoi dung tai roi khong cai)
        const QString keep = backupPackage().fileName();
        for (const QFileInfo& f : QDir(backupDir()).entryInfoList({"*.nupkg"}, QDir::Files))
            if (f.fileName() != package.fileName() && f.fileName() != keep) QFile::remove(f.absoluteFilePath());
        backupName_ = package.fileName();

        // Quay ve khong can mo duoc LumaCast
        QFile script(backupDir() + "/rollback.cmd");
        if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail();
        const QString update = QDir::toNativeSeparators(rootDir() + "/Update.exe");
        const QString text = QString("@echo off\r\nchcp 65001 >nul\r\n"
                                     "echo Dong LumaCast truoc khi tiep tuc / Close LumaCast before continuing.\r\npause\r\n"
                                     "copy /Y \"%~dp0%1\" \"%TEMP%\\%1\" >nul\r\n"
                                     "\"%2\" apply --package \"%TEMP%\\%1\"\r\n")
                                 .arg(package.fileName(), update);
        script.write(text.toUtf8());
        script.close();
        QFile::remove(startMenuShortcut());
        if (!QFile::link(backupDir() + "/rollback.cmd", startMenuShortcut())) qWarning() << "Cap nhat: khong tao duoc loi tat quay ve o menu Start";
        return true;
    }

    QString source_, current_, backupName_;
    vpkc_update_manager_t* manager_ = nullptr;
    vpkc_update_info_t* info_ = nullptr;
};

// Go cai: don nhung thu nam ngoai thu muc cai. Khong duoc hien giao dien, phai xong nhanh.
void beforeUninstall(void*, const char*) {
    QDir(backupDir()).removeRecursively();
    QFile::remove(startMenuShortcut());
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (run.value("LumaCast").toString().contains("\\LumaCast\\current\\", Qt::CaseInsensitive)) run.remove("LumaCast");      // chi khi tro vao ban cai nay
}

#endif // LUMACAST_HAS_VELOPACK

} // namespace

std::shared_ptr<Backend> createBackend(const QString& source) {
#ifdef LUMACAST_HAS_VELOPACK
    if (source.isEmpty()) return std::make_shared<NoBackend>(LC_NOOP("Chưa đặt nơi lấy bản cập nhật."));
    return std::make_shared<VelopackBackend>(source);
#else
    Q_UNUSED(source);
    return std::make_shared<NoBackend>(LC_NOOP("Bản dịch này không kèm chức năng tự cập nhật."));
#endif
}

void runStartupHooks() {
#ifdef LUMACAST_HAS_VELOPACK
    vpkc_app_set_auto_apply_on_startup(false);       // KHONG tu cai ban da tai khi mo app: chi cai khi nguoi dung bam
    vpkc_app_set_hook_before_uninstall(beforeUninstall);
    vpkc_app_run(nullptr);
#endif
}

} // namespace lc::update
