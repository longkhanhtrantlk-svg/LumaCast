#pragma once
#include "control/ControlApi.h"
#include "control/LiveActions.h"
#include "core/Transition.h"
#include <QMap>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <functional>
#include <memory>

namespace lc {

// ---------------------------------------------------------------- lop backend (that: winmm; test: backend gia)

// Cong MIDI VAO. Handler co the duoc goi tu LUONG BAT KY cua he dieu hanh; sau khi close() tra ve thi khong con duoc goi nua.
class MidiBackend {
public:
    using Handler = std::function<void(quint8 status, quint8 data1, quint8 data2)>;
    virtual ~MidiBackend() = default;
    virtual QStringList inputs() = 0;                   // ten cac cong vao dang co (hoi lai moi lan: thiet bi cam/rut luc chay)
    virtual bool open(const QString& name, Handler handler, QString* error) = 0;
    virtual void close() = 0;                           // goi nhieu lan / khi thiet bi da bi rut deu an toan
};

std::unique_ptr<MidiBackend> createSystemMidiBackend();     // Windows: winmm; he khac: khong co cong nao

// ---------------------------------------------------------------- mot "nut" tren thiet bi

struct MidiBinding {
    enum Type { None, Note, Cc, Program };
    Type type = None;
    int channel = 0;            // 0..15
    int number = 0;             // 0..127: so note / so CC / so program

    bool valid() const { return type != None; }
    bool operator==(const MidiBinding& o) const { return type == o.type && channel == o.channel && number == o.number; }
    QString toString() const;                           // "note:0:36" (luu trong QSettings)
    static MidiBinding fromString(const QString& s);    // sai dinh dang -> None
    QString label() const;                              // "Note 36 · kênh 1"
};

// Phan biet "bam" voi nha nut / nhiem tin hieu. Note On (luc > 0) va Program Change: moi thong diep la mot lan bam.
// CC: chi tinh khi gia tri VUOT LEN tu duoi 64 (nut gui 127/0, num xoay keo qua giua chi kich mot lan).
class MidiDecoder {
public:
    MidiBinding press(quint8 status, quint8 data1, quint8 data2);     // None neu thong diep khong phai mot lan bam
    void reset() { cc_.clear(); }
private:
    QMap<int, int> cc_;                                  // channel*128+so -> gia tri truoc do
};

using MidiMap = QMap<QString, MidiBinding>;             // ma lenh (LiveAction::id) -> nut

struct MidiConfig {
    bool enabled = false;       // mac dinh TAT nhu HTTP/OSC
    QString device;             // ten cong vao
    MidiMap map;
    static MidiConfig load(QSettings& st);
    void save(QSettings& st) const;
    // Cac ma lenh dung chung mot nut (rong = khong trung).
    static QStringList duplicates(const MidiMap& map);
};

// ---------------------------------------------------------------- dich vu

// Quan ly ket noi MIDI va chay lenh. Chay tren UI thread; lenh di qua ControlApi (CUNG quy tac an toan voi HTTP/OSC:
// qua RenderEngine::post(), chon Preview khong doi Program, chi so sai bi tu choi + ghi log, khong co lenh sua cau hinh/khoa).
// Thiet bi rut ra: dong cong; cam lai: tu mo lai (do moi giay).
class MidiService : public QObject {
    Q_OBJECT
public:
    // Kenh dang chon + hieu ung dang chon tren thanh chuyen (giong bam phim tat).
    struct Target { int channel = 1; TransitionSpec spec; double ftbSeconds = 1.0; };

    MidiService(ControlApi& api, std::unique_ptr<MidiBackend> backend, QObject* parent = nullptr);
    ~MidiService() override;

    void setTargetProvider(std::function<Target()> f) { target_ = std::move(f); }
    void setBackend(std::unique_ptr<MidiBackend> backend);        // cho test: thay backend (dong cong cu truoc)

    // Ap dung cau hinh: dong cong cu, mo cong moi neu enabled.
    void apply(const MidiConfig& c);
    const MidiConfig& config() const { return cfg_; }

    QStringList devices() const { return backend_ ? backend_->inputs() : QStringList(); }
    bool connected() const { return open_; }
    QString statusText() const { return status_; }
    void reconnect();           // dong roi mo lai (dung khi rut/cam qua nhanh)
    void poll();                // goi dinh ky boi bo dem gio; test goi truc tiep

    // Dang gan nut (hop thoai mo): khong chay lenh nao, chi bao pressed().
    void setSuspended(bool s) { suspended_ = s; }
    bool suspended() const { return suspended_; }

    // Chay mot lenh nhu khi bam nut MIDI (diem vao chung, ca cho test).
    CmdResult run(const LiveAction& a);

signals:
    void statusChanged();
    void pressed(const lc::MidiBinding& binding);

private:
    void tryOpen();
    void closePort();
    void setStatus(const QString& s);
    void onMessage(quint64 generation, quint8 status, quint8 d1, quint8 d2);

    ControlApi& api_;
    std::unique_ptr<MidiBackend> backend_;
    MidiConfig cfg_;
    MidiDecoder decoder_;
    std::function<Target()> target_;
    QTimer pollTimer_;
    QString status_ = "MIDI đang tắt";
    quint64 generation_ = 0;            // bo thong diep con xep hang tu cong da dong
    bool open_ = false;
    bool suspended_ = false;
};

} // namespace lc
