#include "Overlay.h"
#include <QJsonObject>
#include <algorithm>
#include <cmath>

namespace lc {

OverlayDef defaultOverlay(OverlayType type) {
    OverlayDef d;
    d.type = type;
    switch (type) {
    case OverlayType::Title:
        d.name = "Tiêu đề";
        d.text = "Tiêu đề";
        d.bold = true;
        d.background = QColor(0, 0, 0, 160);
        break;
    case OverlayType::Ticker:
        d.name = "Chữ chạy";
        d.text = "Nội dung chữ chạy";
        d.rect = QRectF(0, 0.9, 1, 0.1);
        d.fontSize = 56;
        d.background = QColor(0, 0, 0, 180);
        break;
    case OverlayType::Countdown:
        d.name = "Đếm ngược";
        d.rect = QRectF(0.35, 0.05, 0.3, 0.15);
        d.fontSize = 96;
        d.bold = true;
        d.background = QColor(0, 0, 0, 160);
        break;
    case OverlayType::Logo:
        d.name = "Logo";
        d.rect = QRectF(0.83, 0.04, 0.14, 0.14);
        d.align = OverlayAlign::Right;
        break;
    }
    return d;
}

QString formatCountdown(int seconds, bool withHours) {
    seconds = std::max(0, seconds);
    const QChar z('0');
    if (withHours)
        return QString("%1:%2:%3").arg(seconds / 3600).arg(seconds / 60 % 60, 2, 10, z).arg(seconds % 60, 2, 10, z);
    return QString("%1:%2").arg(seconds / 60, 2, 10, z).arg(seconds % 60, 2, 10, z);
}

QString overlayDisplayText(const OverlayDef& d, int secondsLeft) {
    if (d.type == OverlayType::Countdown) {
        const QString clock = formatCountdown(secondsLeft, d.seconds >= 3600);
        return d.text.isEmpty() ? clock : d.text + " " + clock;
    }
    if (d.type == OverlayType::Logo) return {};
    return d.text;
}

OverlayDef overlayShownDef(const OverlayFrame& f) {
    OverlayDef d = f.def;
    if (f.alerting) d.color = kAlertColor;
    return d;
}

// ---------------- OverlayStack ----------------

const OverlayDef* OverlayStack::find(int id) const {
    for (const auto& d : defs_) if (d.id == id) return &d;
    return nullptr;
}

std::vector<OverlayDef> OverlayStack::defs() const {
    std::lock_guard lk(m_);
    return defs_;
}

bool OverlayStack::empty() const {
    std::lock_guard lk(m_);
    return defs_.empty();
}

int OverlayStack::nextId() const {
    std::lock_guard lk(m_);
    int n = 0;
    for (const auto& d : defs_) n = std::max(n, d.id);
    return n + 1;
}

void OverlayStack::setDefs(std::vector<OverlayDef> defs) {
    std::lock_guard lk(m_);
    std::map<int, Live> keep;
    for (const auto& d : defs) {
        const OverlayDef* old = find(d.id);
        if (!old || old->type != d.type) continue;                  // moi (hoac xoa roi Hoan tac): trang thai TAT
        auto it = live_.find(d.id);
        if (it == live_.end()) continue;
        Live l = it->second;
        if (old->seconds != d.seconds) { l.running = false; l.accumulated = 0; }
        keep[d.id] = l;
    }
    live_ = std::move(keep);
    defs_ = std::move(defs);
}

float OverlayStack::level(const Live& l, const OverlayDef& d, double now) {
    if (!l.fading || d.fade <= 0.0f) return l.on ? 1.0f : 0.0f;
    const float step = float(std::max(0.0, now - l.switchedAt) / d.fade);
    return std::clamp(l.on ? l.levelAtSwitch + step : l.levelAtSwitch - step, 0.0f, 1.0f);
}

void OverlayStack::setOn(int id, bool on, double now) {
    std::lock_guard lk(m_);
    const OverlayDef* d = find(id);
    if (!d) return;
    Live& l = live_[id];
    if (l.on == on) return;
    const float from = level(l, *d, now);
    l.on = on;
    l.fading = true;
    l.switchedAt = now;
    l.levelAtSwitch = from;
    if (on && from <= 0.0f) l.onSince = now;                        // hien lai tu dau: chu chay vao tu mep phai
}

void OverlayStack::restoreOn(int id) {
    std::lock_guard lk(m_);
    if (!find(id)) return;
    Live& l = live_[id];
    l.on = true;
    l.fading = false;
    l.onSince = -1;
}

bool OverlayStack::isOn(int id) const {
    std::lock_guard lk(m_);
    auto it = live_.find(id);
    return it != live_.end() && it->second.on;
}

void OverlayStack::countdownStart(int id, double now) {
    std::lock_guard lk(m_);
    const OverlayDef* d = find(id);
    if (!d || d->type != OverlayType::Countdown) return;
    Live& l = live_[id];
    if (l.running || l.accumulated >= d->seconds) return;           // dang chay, hoac da het gio (phai Dat lai)
    l.running = true;
    l.startedAt = now;
}

void OverlayStack::countdownPause(int id, double now) {
    std::lock_guard lk(m_);
    const OverlayDef* d = find(id);
    auto it = live_.find(id);
    if (!d || it == live_.end() || !it->second.running) return;
    Live& l = it->second;
    l.accumulated = std::min(double(d->seconds), l.accumulated + std::max(0.0, now - l.startedAt));
    l.running = false;
}

void OverlayStack::countdownReset(int id) {
    std::lock_guard lk(m_);
    auto it = live_.find(id);
    if (it == live_.end()) return;
    it->second.running = false;
    it->second.accumulated = 0;
}

OverlayFrame OverlayStack::frameOf(const OverlayDef& d, double now) const {
    OverlayFrame f;
    f.def = d;
    f.secondsLeft = d.type == OverlayType::Countdown ? d.seconds : 0;
    f.remaining = f.secondsLeft;
    auto it = live_.find(d.id);
    if (it == live_.end()) return f;
    Live& l = it->second;
    f.on = l.on;
    f.level = level(l, d, now);
    if (l.onSince < 0 && l.on) l.onSince = now;
    f.onSeconds = l.onSince < 0 ? 0.0 : std::max(0.0, now - l.onSince);
    if (d.type == OverlayType::Countdown) {
        double elapsed = l.accumulated + (l.running ? std::max(0.0, now - l.startedAt) : 0.0);
        if (elapsed >= d.seconds) {                                 // het gio: dung o 00:00
            elapsed = d.seconds;
            l.running = false;
            l.accumulated = d.seconds;
        }
        f.secondsLeft = int(std::ceil(d.seconds - elapsed - 1e-6));
        f.running = l.running;
        f.remaining = std::max(0.0, d.seconds - elapsed);
        f.finished = !l.running && elapsed >= d.seconds;
        f.alerting = d.alert && l.on && (f.finished || (f.running && f.remaining <= d.alertSeconds + 1e-6));
    }
    return f;
}

std::vector<OverlayFrame> OverlayStack::frames(double now) const {
    std::lock_guard lk(m_);
    std::vector<OverlayFrame> v;
    v.reserve(defs_.size());
    for (const auto& d : defs_) v.push_back(frameOf(d, now));
    return v;
}

bool OverlayStack::frame(int id, double now, OverlayFrame& out) const {
    std::lock_guard lk(m_);
    const OverlayDef* d = find(id);
    if (!d) return false;
    out = frameOf(*d, now);
    return true;
}

// ---------------- JSON ----------------

static const char* alignId(OverlayAlign a) {
    return a == OverlayAlign::Left ? "left" : a == OverlayAlign::Right ? "right" : "center";
}

static double num(const QJsonObject& o, const char* key, double def, double lo, double hi) {
    const double v = o[QLatin1String(key)].toDouble(def);
    return std::isfinite(v) ? std::clamp(v, lo, hi) : def;
}

static QColor colorOf(const QJsonObject& o, const char* key, const QColor& def) {
    const QColor c(o[QLatin1String(key)].toString());
    return c.isValid() ? c : def;
}

QJsonArray overlaysToJson(const OverlayStack& s, const QDir& base) {
    QJsonArray a;
    for (const OverlayDef& d : s.defs()) {
        QJsonObject o;
        o["id"] = d.id;
        o["type"] = overlayTypeId(d.type);
        o["name"] = d.name;
        o["x"] = d.rect.x(); o["y"] = d.rect.y(); o["w"] = d.rect.width(); o["h"] = d.rect.height();
        o["opacity"] = double(d.opacity);
        o["fade"] = double(d.fade);
        o["align"] = alignId(d.align);
        o["background"] = d.background.name(QColor::HexArgb);
        if (d.type == OverlayType::Logo) {
            if (!d.image.isEmpty()) o["image"] = base.relativeFilePath(d.image);
        } else {
            o["text"] = d.text;
            if (!d.font.isEmpty()) o["font"] = d.font;
            o["fontSize"] = d.fontSize;
            if (d.bold) o["bold"] = true;
            if (d.italic) o["italic"] = true;
            o["color"] = d.color.name(QColor::HexArgb);
            o["outlineColor"] = d.outlineColor.name(QColor::HexArgb);
            o["outlineWidth"] = double(d.outlineWidth);
        }
        if (d.type == OverlayType::Ticker) o["speed"] = d.speed;
        if (d.type == OverlayType::Countdown) {
            o["seconds"] = d.seconds;
            const OverlayDef def;                                    // tieng nhac: chi ghi phan khac mac dinh
            if (d.alert) o["alert"] = true;
            if (d.alertSeconds != def.alertSeconds) o["alertSeconds"] = d.alertSeconds;
            if (d.fastSeconds != def.fastSeconds) o["fastSeconds"] = d.fastSeconds;
            if (d.alertVolume != def.alertVolume) o["alertVolume"] = d.alertVolume;
        }
        if (s.isOn(d.id)) o["on"] = true;
        a.append(o);
    }
    return a;
}

void overlaysFromJson(OverlayStack& s, const QJsonArray& a, const QDir& base) {
    std::vector<OverlayDef> defs;
    std::vector<int> on;
    int maxId = 0;
    for (const QJsonValue& v : a) {
        if (int(defs.size()) >= kMaxOverlays) break;
        const QJsonObject o = v.toObject();
        const QString type = o["type"].toString();
        OverlayType t;
        if (type == "title") t = OverlayType::Title;
        else if (type == "ticker") t = OverlayType::Ticker;
        else if (type == "countdown") t = OverlayType::Countdown;
        else if (type == "logo") t = OverlayType::Logo;
        else continue;                                              // loai la: bo qua (khong lam hong project)
        OverlayDef d = defaultOverlay(t);
        d.id = o["id"].toInt(0);
        bool taken = d.id <= 0;
        for (const auto& e : defs) taken = taken || e.id == d.id;
        if (taken) d.id = maxId + 1;
        maxId = std::max(maxId, d.id);
        d.name = o["name"].toString(d.name).left(120);
        d.rect = QRectF(num(o, "x", d.rect.x(), -4, 8), num(o, "y", d.rect.y(), -4, 8),
                        num(o, "w", d.rect.width(), 0.005, 8), num(o, "h", d.rect.height(), 0.005, 8));
        d.opacity = float(num(o, "opacity", 1.0, 0, 1));
        d.fade = float(num(o, "fade", 0.3, 0, 10));
        const QString al = o["align"].toString(alignId(d.align));
        d.align = al == "left" ? OverlayAlign::Left : al == "right" ? OverlayAlign::Right : OverlayAlign::Center;
        d.background = colorOf(o, "background", QColor(0, 0, 0, 0));
        d.text = o["text"].toString().left(kMaxOverlayText);
        d.font = o["font"].toString().left(120);
        d.fontSize = int(num(o, "fontSize", d.fontSize, 4, 2000));
        d.bold = o["bold"].toBool(false);
        d.italic = o["italic"].toBool(false);
        d.color = colorOf(o, "color", Qt::white);
        d.outlineColor = colorOf(o, "outlineColor", Qt::black);
        d.outlineWidth = float(num(o, "outlineWidth", 0, 0, 50));
        d.speed = num(o, "speed", 150, 1, 5000);
        d.seconds = int(num(o, "seconds", 300, 1, kMaxCountdownSeconds));
        d.alert = o["alert"].toBool(false);
        d.alertSeconds = int(num(o, "alertSeconds", d.alertSeconds, 1, kMaxAlertSeconds));
        d.fastSeconds = int(num(o, "fastSeconds", std::min(d.fastSeconds, d.alertSeconds), 0, d.alertSeconds));
        d.alertVolume = int(num(o, "alertVolume", d.alertVolume, 0, 100));
        const QString img = o["image"].toString();
        if (!img.isEmpty()) d.image = QDir::cleanPath(base.absoluteFilePath(img));     // thieu file: van mo duoc project, lop phu trong
        if (o["on"].toBool(false)) on.push_back(d.id);
        defs.push_back(d);
    }
    s.setDefs(std::move(defs));
    for (int id : on) s.restoreOn(id);
}

} // namespace lc
