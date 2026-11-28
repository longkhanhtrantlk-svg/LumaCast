#pragma once
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <mutex>

// Tu cap nhat trong phan mem. Luat an toan khi dang dien (test `update` kiem tra):
//   - KHONG bao gio tu tai, tu cai hay tu khoi dong lai: moi buoc do mot lan bam cua nguoi van hanh;
//   - dang Khoa van hanh: khong tu kiem tra, khong hien thong bao, khong tai / cai / quay ve;
//   - mat mang: chi ghi log (kiem tra tu dong) hoac bao mot dong (nguoi dung bam kiem tra); phan mem chay binh thuong.
// Viec voi thu vien (Velopack) nam sau Backend de test bang backend gia; ban khong cai bang bo cai thi Unsupported.
namespace lc::update {

constexpr const char* kKeyAutoCheck = "update/autoCheck";      // mac dinh BAT
constexpr const char* kKeySource = "update/source";            // URL GitHub / URL web / thu muc; rong = mac dinh cua ban dich
constexpr const char* kKeySkipVersion = "update/skipVersion";  // ban vua bi quay ve: kiem tra TU DONG bo qua
constexpr const char* kKeyVerifying = "update/verifying";      // ban vua cai, chua chac chay duoc
constexpr const char* kKeyAttempts = "update/attempts";

QString configuredSource(QSettings& st);                       // bien moi truong LUMACAST_UPDATE_SOURCE > cai dat > mac dinh

struct Release {
    QString version;
    qint64 downloadBytes = 0;
    bool delta = false;                                        // chi tai phan thay doi
    QString notes;
};

enum class CheckResult { UpToDate, Available, Failed };

// Cac ham co the cham (mang, dia): Service goi tren luong rieng. Moi luc chi mot viec.
class Backend {
public:
    virtual ~Backend() = default;
    virtual bool supported(QString* why) const = 0;
    virtual QString currentVersion() const = 0;
    // Thu muc bi THAY khi cap nhat va XOA khi go cai (rong = khong co): khong duoc de file cua nguoi dung o day.
    virtual QString installDir() const { return {}; }
    virtual CheckResult check(Release& out, QString* err) = 0;
    virtual bool download(const std::function<void(int percent)>& progress, QString* err) = 0;
    // Sao luu ban dang chay roi goi trinh cap nhat CHO tien trinh nay thoat; nguoi goi phai dong app ngay.
    virtual bool applyAndRestart(const QStringList& args, QString* err) = 0;
    virtual QString previousVersion() const = 0;               // ban co the quay ve (rong = khong co)
    virtual bool rollback(QString* err) = 0;                   // nhu applyAndRestart, nhung ve ban truoc
};

// Backend cua ban dich nay: Velopack neu duoc dich kem va app dang chay tu ban cai; nguoc lai khong ho tro.
std::shared_ptr<Backend> createBackend(const QString& source);

// path co nam trong thu muc dir khong (dir rong: khong).
bool isInside(const QString& path, const QString& dir);

// Goi DAU TIEN trong main() (truoc QApplication): trinh cai / go cai goi lai exe voi tham so rieng va co the thoat tai day.
void runStartupHooks();

// Ban moi vua cai ma khong khoi dong noi: nho de lan sau de nghi quay ve.
namespace guard {
void markPending(QSettings& st, const QString& newVersion);
// Goi som khi khoi dong. true = lan mo truoc cua ban moi nay khong chay on (nen de nghi quay ve).
bool beginStartup(QSettings& st, const QString& currentVersion);
void confirm(QSettings& st);                                   // ban nay da chay on
}

class Service : public QObject {
    Q_OBJECT
public:
    enum class State { Unsupported, Idle, Checking, UpToDate, Available, Downloading, Ready, Failed };

    explicit Service(std::shared_ptr<Backend> backend, QObject* parent = nullptr);
    ~Service() override;

    void setLockProvider(std::function<bool()> f) { locked_ = std::move(f); }
    bool locked() const { return locked_ && locked_(); }

    State state() const { return state_; }
    bool busy() const { return state_ == State::Checking || state_ == State::Downloading; }
    const Release& release() const { return release_; }
    QString message() const { return message_; }               // loi / ly do khong ho tro
    int progress() const { return progress_; }
    QString currentVersion() const { return backend_->currentVersion(); }
    QString installDir() const { return backend_->installDir(); }
    QString previousVersion() const { return state_ == State::Unsupported ? QString() : backend_->previousVersion(); }
    bool shouldNotify() const { return (state_ == State::Available || state_ == State::Ready) && !locked(); }

    bool autoCheck() const;
    void setAutoCheck(bool on);

    void checkAuto();                       // lan mo app: bo qua neu tat tu kiem tra, dang khoa, hoac dang ban
    void checkNow();                        // nguoi dung bam
    void download();                        // chi khi Available va khong khoa
    bool apply(const QStringList& args, QString* err);   // chi khi Ready va khong khoa; true = dong app ngay
    bool rollback(QString* err);            // chi khi co ban truoc va khong khoa; true = dong app ngay

signals:
    void changed();

private:
    struct Link {                           // luong nen con chay khi Service da huy (vd. thoat luc dang tai)
        std::mutex mutex;
        Service* service = nullptr;
    };
    void check(bool manual);
    void run(std::function<std::function<void(Service&)>(Backend&)> work);
    void set(State s, const QString& message = {});

    std::shared_ptr<Backend> backend_;
    std::shared_ptr<Link> link_;
    std::function<bool()> locked_;
    State state_ = State::Idle;
    Release release_;
    QString message_;
    int progress_ = 0;
};

} // namespace lc::update
