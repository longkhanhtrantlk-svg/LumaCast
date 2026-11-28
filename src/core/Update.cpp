#include "Update.h"
#include "core/AppSettings.h"
#include "core/I18n.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <thread>

#ifndef LUMACAST_UPDATE_URL
#define LUMACAST_UPDATE_URL ""
#endif

namespace lc::update {

QString configuredSource(QSettings& st) {
    const QString env = qEnvironmentVariable("LUMACAST_UPDATE_SOURCE");
    if (!env.isEmpty()) return env;
    const QString saved = st.value(kKeySource).toString().trimmed();
    return saved.isEmpty() ? QString::fromUtf8(LUMACAST_UPDATE_URL) : saved;
}

bool isInside(const QString& path, const QString& dir) {
    if (path.isEmpty() || dir.isEmpty()) return false;
    const QString p = QDir::cleanPath(QFileInfo(path).absoluteFilePath()), d = QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    return p.compare(d, cs) == 0 || p.startsWith(d + "/", cs);
}

namespace guard {

void markPending(QSettings& st, const QString& newVersion) {
    st.setValue(kKeyVerifying, newVersion);
    st.setValue(kKeyAttempts, 0);
    st.sync();
}

bool beginStartup(QSettings& st, const QString& currentVersion) {
    const QString pending = st.value(kKeyVerifying).toString();
    if (pending.isEmpty()) return false;
    if (pending != currentVersion) {                 // chua cai duoc hoac da quay ve: khong con gi de theo doi
        confirm(st);
        return false;
    }
    const int attempts = st.value(kKeyAttempts, 0).toInt();
    st.setValue(kKeyAttempts, attempts + 1);
    st.sync();                                       // ghi ngay: neu app chet lan nay thi lan sau van biet
    return attempts >= 1;
}

void confirm(QSettings& st) {
    st.remove(kKeyVerifying);
    st.remove(kKeyAttempts);
}

} // namespace guard

Service::Service(std::shared_ptr<Backend> backend, QObject* parent) : QObject(parent), backend_(std::move(backend)), link_(std::make_shared<Link>()) {
    link_->service = this;
    QString why;
    if (!backend_->supported(&why)) {
        state_ = State::Unsupported;
        message_ = why;
    }
}

Service::~Service() {
    std::lock_guard lk(link_->mutex);
    link_->service = nullptr;
}

bool Service::autoCheck() const {
    QSettings st = appSettings();
    return st.value(kKeyAutoCheck, true).toBool();
}

void Service::setAutoCheck(bool on) {
    QSettings st = appSettings();
    st.setValue(kKeyAutoCheck, on);
}

void Service::set(State s, const QString& message) {
    state_ = s;
    message_ = message;
    emit changed();
}

// work() chay tren luong rieng va tra ve viec can lam tren luong giao dien.
void Service::run(std::function<std::function<void(Service&)>(Backend&)> work) {
    std::thread([backend = backend_, link = link_, work = std::move(work)] {
        std::function<void(Service&)> done = work(*backend);
        std::lock_guard lk(link->mutex);
        if (Service* s = link->service) QMetaObject::invokeMethod(s, [s, done] { done(*s); }, Qt::QueuedConnection);
    }).detach();
}

void Service::checkAuto() {
    if (autoCheck() && !locked()) check(false);
}

void Service::checkNow() {
    if (!locked()) check(true);
}

void Service::check(bool manual) {
    if (state_ == State::Unsupported || busy() || state_ == State::Ready) return;
    set(State::Checking);
    run([manual](Backend& b) -> std::function<void(Service&)> {
        Release r;
        QString err;
        const CheckResult result = b.check(r, &err);
        return [manual, result, r, err](Service& s) {
            if (result == CheckResult::Failed) {
                qWarning().noquote() << "Cap nhat: khong kiem tra duoc:" << err;
                // tu dong (vd. mat mang): im lang, khong lam phien
                if (manual) s.set(State::Failed, QString(LC_TR("Không kiểm tra được bản mới: %1")).arg(err));
                else s.set(State::Idle);
                return;
            }
            if (result == CheckResult::UpToDate) {
                s.set(State::UpToDate);
                return;
            }
            QSettings st = appSettings();
            if (!manual && st.value(kKeySkipVersion).toString() == r.version) {      // ban vua bi quay ve: khong nhac lai
                s.set(State::Idle);
                return;
            }
            s.release_ = r;
            qInfo().noquote() << "Cap nhat: co ban moi" << r.version;
            s.set(State::Available);
        };
    });
}

void Service::download() {
    if (state_ != State::Available || locked()) return;
    progress_ = 0;
    set(State::Downloading);
    auto link = link_;
    run([link](Backend& b) -> std::function<void(Service&)> {
        QString err;
        const bool ok = b.download([link](int percent) {
            std::lock_guard lk(link->mutex);
            if (Service* s = link->service)
                QMetaObject::invokeMethod(s, [s, percent] { s->progress_ = percent; emit s->changed(); }, Qt::QueuedConnection);
        }, &err);
        return [ok, err](Service& s) {
            if (ok) {
                qInfo() << "Cap nhat: da tai xong, cho nguoi van hanh bam cap nhat";
                s.set(State::Ready);
            } else {
                qWarning().noquote() << "Cap nhat: tai that bai:" << err;
                s.set(State::Available, QString(LC_TR("Tải bản mới không xong: %1")).arg(err));
            }
        };
    });
}

bool Service::apply(const QStringList& args, QString* err) {
    auto fail = [&](const QString& e) { if (err) *err = e; return false; };
    if (locked()) return fail(LC_TR("Đang khoá vận hành"));
    if (state_ != State::Ready) return fail(LC_TR("Chưa có bản cập nhật đã tải"));
    QSettings st = appSettings();
    guard::markPending(st, release_.version);
    st.remove(kKeySkipVersion);
    QString why;
    if (!backend_->applyAndRestart(args, &why)) {
        guard::confirm(st);
        qWarning().noquote() << "Cap nhat: khong chay duoc trinh cap nhat:" << why;
        return fail(why);
    }
    qInfo().noquote() << "Cap nhat: thoat de cai ban" << release_.version;
    return true;
}

bool Service::rollback(QString* err) {
    auto fail = [&](const QString& e) { if (err) *err = e; return false; };
    if (locked()) return fail(LC_TR("Đang khoá vận hành"));
    if (previousVersion().isEmpty()) return fail(LC_TR("Không có bản trước để quay về"));
    const QString from = backend_->currentVersion(), to = backend_->previousVersion();
    QString why;
    if (!backend_->rollback(&why)) {
        qWarning().noquote() << "Cap nhat: khong quay ve duoc:" << why;
        return fail(why);
    }
    QSettings st = appSettings();
    st.setValue(kKeySkipVersion, from);
    guard::confirm(st);
    qInfo().noquote() << "Cap nhat: quay ve ban" << to << "tu" << from;
    return true;
}

} // namespace lc::update
