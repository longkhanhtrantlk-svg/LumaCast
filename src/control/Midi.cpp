#include "Midi.h"
#include "core/I18n.h"
#include "core/Features.h"
#include <QDebug>

namespace lc {

// ---------------------------------------------------------------- MidiBinding

QString MidiBinding::toString() const {
    if (!valid()) return {};
    const char* t = type == Note ? "note" : type == Cc ? "cc" : "program";
    return QString("%1:%2:%3").arg(t).arg(channel).arg(number);
}

MidiBinding MidiBinding::fromString(const QString& s) {
    const QStringList p = s.split(':');
    if (p.size() != 3) return {};
    bool ok1 = false, ok2 = false;
    MidiBinding b;
    b.type = p[0] == "note" ? Note : p[0] == "cc" ? Cc : p[0] == "program" ? Program : None;
    b.channel = p[1].toInt(&ok1);
    b.number = p[2].toInt(&ok2);
    if (b.type == None || !ok1 || !ok2 || b.channel < 0 || b.channel > 15 || b.number < 0 || b.number > 127) return {};
    return b;
}

QString MidiBinding::label() const {
    if (!valid()) return "—";
    const char* t = type == Note ? "Note" : type == Cc ? "CC" : "Program";
    return QString(LC_TR("%1 %2 · kênh %3")).arg(t).arg(number).arg(channel + 1);
}

MidiBinding MidiDecoder::press(quint8 status, quint8 d1, quint8 d2) {
    if (status < 0x80 || status >= 0xF0 || d1 > 127 || d2 > 127) return {};      // thong diep he thong/thoi gian thuc: bo
    const int kind = status & 0xF0, ch = status & 0x0F;
    if (kind == 0x90) return d2 > 0 ? MidiBinding{MidiBinding::Note, ch, d1} : MidiBinding{};
    if (kind == 0xC0) return MidiBinding{MidiBinding::Program, ch, d1};
    if (kind == 0xB0) {
        const int key = ch * 128 + d1;
        const int before = cc_.value(key, 0);
        cc_[key] = d2;
        if (before < 64 && d2 >= 64) return MidiBinding{MidiBinding::Cc, ch, d1};
    }
    return {};
}

// ---------------------------------------------------------------- MidiConfig

MidiConfig MidiConfig::load(QSettings& st) {
    MidiConfig c;
    c.enabled = st.value("midi/enabled", false).toBool();
    c.device = st.value("midi/device").toString();
    st.beginGroup("midi/map");
    const QStringList keys = st.childKeys();
    QMap<int, std::pair<QString, MidiBinding>> ordered;
    for (const QString& id : keys) {
        const LiveAction a = LiveAction::fromId(id);
        const MidiBinding b = MidiBinding::fromString(st.value(id).toString());
        if (a.externalAllowed() && b.valid()) ordered.insert(a.order(), {id, b});
    }
    st.endGroup();
    for (const auto& [id, b] : ordered) {                  // cau hinh sua tay bi trung: giu lenh dung truoc
        bool dup = false;
        for (const MidiBinding& have : std::as_const(c.map)) dup = dup || have == b;
        if (!dup) c.map.insert(id, b);
        else qWarning().noquote() << "MIDI: bỏ gán trùng nút" << b.label() << "cho" << id;
    }
    return c;
}

void MidiConfig::save(QSettings& st) const {
    st.setValue("midi/enabled", enabled);
    st.setValue("midi/device", device);
    st.remove("midi/map");
    for (auto it = map.begin(); it != map.end(); ++it)
        if (it.value().valid()) st.setValue("midi/map/" + it.key(), it.value().toString());
}

QStringList MidiConfig::duplicates(const MidiMap& map) {
    QStringList dup;
    for (auto a = map.begin(); a != map.end(); ++a)
        for (auto b = map.begin(); b != map.end(); ++b)
            if (a.key() != b.key() && a.value().valid() && a.value() == b.value()) { dup << a.key(); break; }
    return dup;
}

// ---------------------------------------------------------------- MidiService

MidiService::MidiService(ControlApi& api, std::unique_ptr<MidiBackend> backend, QObject* parent)
    : QObject(parent), api_(api), backend_(std::move(backend)) {
    connect(&pollTimer_, &QTimer::timeout, this, [this] { poll(); });
}

MidiService::~MidiService() { closePort(); }               // phai dong truoc khi cac thanh vien bi huy: het callback

void MidiService::setBackend(std::unique_ptr<MidiBackend> backend) {
    closePort();
    backend_ = std::move(backend);
    apply(MidiConfig(cfg_));
}

void MidiService::apply(const MidiConfig& c) {
    closePort();
    cfg_ = c;
    pollTimer_.stop();
    if (!cfg_.enabled) { setStatus(LC_TR("MIDI đang tắt")); return; }
    if (!Entitlements::current().externalControl) {
        qWarning().noquote() << "MIDI không có trong gói giấy phép hiện tại: không mở cổng";
        setStatus(LC_TR("MIDI không có trong gói giấy phép hiện tại"));
        return;
    }
    pollTimer_.start(1000);
    tryOpen();
}

void MidiService::setStatus(const QString& s) {
    if (s == status_) { return; }
    status_ = s;
    emit statusChanged();
}

void MidiService::closePort() {
    ++generation_;                                       // thong diep dang xep hang tro thanh vo hieu
    decoder_.reset();
    if (!open_) return;
    open_ = false;
    if (backend_) backend_->close();
}

void MidiService::tryOpen() {
    if (open_ || !backend_ || !cfg_.enabled) return;
    if (cfg_.device.isEmpty()) { setStatus(LC_TR("Chưa chọn thiết bị MIDI")); return; }
    if (!backend_->inputs().contains(cfg_.device)) { setStatus(LC_TR("Chưa thấy thiết bị MIDI \"") + cfg_.device + LC_TR("\" — cắm vào là tự nối")); return; }
    const quint64 gen = ++generation_;
    QString err;
    const bool ok = backend_->open(cfg_.device, [this, gen](quint8 st, quint8 d1, quint8 d2) {
        // luong cua driver: chi xep hang sang UI thread, khong goi gi them. close() bao dam khong con callback truoc khi huy.
        QMetaObject::invokeMethod(this, [this, gen, st, d1, d2] { onMessage(gen, st, d1, d2); }, Qt::QueuedConnection);
    }, &err);
    if (ok) {
        open_ = true;
        qInfo().noquote() << "MIDI: đã nối" << cfg_.device;
        setStatus(LC_TR("Đã nối: ") + cfg_.device);
    } else {
        const i18n::Text s = LC_BOTH(LC_TR("Không mở được \"") + cfg_.device + "\"" + (err.isEmpty() ? QString() : ": " + err));
        if (s.shown != status_) qWarning().noquote() << "MIDI:" << s.source;     // thu lai moi giay: chi log mot lan
        setStatus(s.shown);
    }
}

void MidiService::poll() {
    if (!cfg_.enabled || !backend_ || !pollTimer_.isActive()) return;
    if (open_ && !backend_->inputs().contains(cfg_.device)) {
        closePort();
        qWarning().noquote() << "MIDI: mất thiết bị" << cfg_.device;
        setStatus(LC_TR("Mất kết nối \"") + cfg_.device + LC_TR("\" — cắm lại là tự nối"));
        return;
    }
    if (!open_) tryOpen();
}

void MidiService::reconnect() {
    closePort();
    tryOpen();
}

void MidiService::onMessage(quint64 generation, quint8 status, quint8 d1, quint8 d2) {
    if (generation != generation_) return;
    const MidiBinding b = decoder_.press(status, d1, d2);
    if (!b.valid()) return;
    emit pressed(b);
    if (suspended_) return;
    for (auto it = cfg_.map.constBegin(); it != cfg_.map.constEnd(); ++it) {
        if (!(it.value() == b)) continue;
        run(LiveAction::fromId(it.key()));
        return;                                          // mot nut chi chay mot lenh
    }
}

CmdResult MidiService::run(const LiveAction& a) {
    const Target t = target_ ? target_() : Target{};
    CmdResult r;
    switch (a.kind) {
    case LiveAction::Take: r = api_.takeWith(t.channel, t.spec); break;
    case LiveAction::Cut: r = api_.cut(t.channel); break;
    case LiveAction::Ftb: r = api_.ftb(t.channel, std::nullopt, t.ftbSeconds); break;
    case LiveAction::BlackAll: r = api_.blackAll(std::nullopt); break;
    case LiveAction::Preview: r = api_.setPreview(t.channel, a.index); break;
    case LiveAction::OutputBlack: r = api_.outputFlag(a.index, ControlApi::OutFlag::Black, std::nullopt); break;
    case LiveAction::OutputFreeze: r = api_.outputFlag(a.index, ControlApi::OutFlag::Freeze, std::nullopt); break;
    case LiveAction::OutputPattern: r = api_.outputFlag(a.index, ControlApi::OutFlag::Pattern, std::nullopt); break;
    case LiveAction::Lock:
    case LiveAction::SlideNext:
    case LiveAction::SlidePrev:
    case LiveAction::None:
        r = CmdResult::error(403, "not_allowed", LC_BOTH(LC_TR("Lệnh này không điều khiển từ MIDI được")));
        break;
    }
    if (!r.ok()) {
        i18n::Text what = LC_BOTH(QString("MIDI %1 → %2: ").arg(a.label(), r.code));
        what.shown += r.message;
        what.source += r.logMessage;
        api_.noteRejected("MIDI", what);
    }
    return r;
}

} // namespace lc
