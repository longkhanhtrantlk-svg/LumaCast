#include "ControlApi.h"
#include "control/ControlPage.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>

namespace lc {

namespace {

bool parseIndex(const QString& s, int& out) {
    bool ok = false;
    const int v = s.toInt(&ok);
    if (!ok || s.size() > 6) return false;
    out = v;
    return true;
}

bool isLoopbackHost(const QByteArray& hostHeader) {
    static const QRegularExpression re(R"(^(127\.0\.0\.1|localhost|\[::1\])(:\d{1,5})?$)", QRegularExpression::CaseInsensitiveOption);
    return re.match(QString::fromLatin1(hostHeader)).hasMatch();
}

// Origin (neu co) phai trung voi Host: chan trang web ben ngoai bam POST toi 127.0.0.1 (CSRF).
bool sameOrigin(const QByteArray& origin, const QByteArray& host) {
    const QString o = QString::fromLatin1(origin).trimmed();
    const int k = o.indexOf("://");
    if (k <= 0) return false;
    const QString hostPort = o.mid(k + 3);
    return !host.isEmpty() && hostPort.compare(QString::fromLatin1(host), Qt::CaseInsensitive) == 0;
}

const char* healthId(OutputHealth h) {
    switch (h) {
    case OutputHealth::Ok: return "ok";
    case OutputHealth::NoScreen: return "no_screen";
    case OutputHealth::Hidden: return "hidden";
    case OutputHealth::WrongScreen: return "wrong_screen";
    }
    return "ok";
}

// Doc tham so "on" tuy chon: vang = toggle; sai kieu = loi.
bool readOptBool(const QJsonObject& b, const QString& key, std::optional<bool>& out, CmdResult& err) {
    out.reset();
    if (!b.contains(key) || b[key].isNull()) return true;
    if (!b[key].isBool()) { err = CmdResult::error(400, "bad_request", QString("'%1' phải là true/false").arg(key)); return false; }
    out = b[key].toBool();
    return true;
}

bool oscBool(const QVariant& v, std::optional<bool>& out, CmdResult& err) {
    out.reset();
    if (!v.isValid()) return true;
    switch (v.typeId()) {
    case QMetaType::Bool: out = v.toBool(); return true;
    case QMetaType::Int: case QMetaType::LongLong: out = v.toInt() != 0; return true;
    case QMetaType::Double: out = std::abs(v.toDouble()) > 1e-9; return true;
    default: err = CmdResult::error(400, "bad_request", "tham số OSC phải là số hoặc T/F"); return false;
    }
}

} // namespace

bool ControlApi::constantTimeEquals(const QByteArray& a, const QByteArray& b) {
    unsigned diff = unsigned(a.size()) ^ unsigned(b.size());
    const int n = std::max(a.size(), b.size());
    for (int i = 0; i < n; ++i) {
        const unsigned x = i < a.size() ? uchar(a[i]) : 0u, y = i < b.size() ? uchar(b[i]) : 0u;
        diff |= x ^ y;
    }
    return diff == 0;
}

ControlApi::ControlApi(Project& p, RenderEngine& e, OutputSupervisor* s, QObject* parent)
    : QObject(parent), project_(p), engine_(e), sup_(s) {
    refreshProject();
    buildDefaultRoutes();
    fpsClock_.start();
    connect(&fpsTimer_, &QTimer::timeout, this, [this] { sampleFps(); });
    fpsTimer_.start(500);
}

void ControlApi::refreshProject() {
    channels_ = project_.channels;
    scenes_ = project_.scenes;
    chScenes_.clear();
    for (const auto& c : channels_) chScenes_.push_back(c->scenes);
    const size_t keepFps = outs_.size();
    std::vector<OutInfo> old;
    old.swap(outs_);
    for (int i = 0; i < engine_.outputCount(); ++i) {
        const OutputConfig& c = engine_.output(i)->config();
        OutInfo o;
        if (size_t(i) < keepFps) o = old[i];
        o.name = c.name; o.channel = c.channel; o.size = c.size;
        outs_.push_back(o);
    }
}

void ControlApi::sampleFps() {
    const double dt = fpsClock_.restart() / 1000.0;
    if (dt <= 0) return;
    for (size_t i = 0; i < outs_.size() && int(i) < engine_.outputCount(); ++i) {
        const quint64 pr = engine_.output(int(i))->presented;
        outs_[i].fps = double(pr - outs_[i].lastPresented) / dt;
        outs_[i].lastPresented = pr;
    }
}

void ControlApi::noteRejected(const QString& who, const QString& what) {
    ++rejected_;
    emit commandRejected(what);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - logWindowStart_ > 10000) {                      // cua so 10 s: toi da 20 dong log, tranh bi lam ngap file log
        if (logSuppressed_ > 0) qWarning().noquote() << QString("Điều khiển ngoài: đã bỏ qua %1 dòng log từ chối do quá nhiều").arg(logSuppressed_);
        logWindowStart_ = now; logInWindow_ = 0; logSuppressed_ = 0;
    }
    if (logInWindow_ < 20) { ++logInWindow_; qWarning().noquote() << QString("Điều khiển ngoài: TỪ CHỐI %1 (từ %2)").arg(what, who); }
    else ++logSuppressed_;
}

// ---------------------------------------------------------------- lenh

std::shared_ptr<Channel> ControlApi::chan(int n, CmdResult& err) const {
    if (n < 1 || n > int(channels_.size())) {
        err = CmdResult::error(404, "channel_not_found", QString("Không có kênh %1 (có %2 kênh)").arg(n).arg(channels_.size()));
        return nullptr;
    }
    return channels_[n - 1];
}

CmdResult ControlApi::setPreview(int channel, int scene) {
    CmdResult err;
    auto c = chan(channel, err);
    if (!c) return err;
    const auto& list = chScenes_[channel - 1];
    if (scene < 1 || scene > int(list.size()))
        return CmdResult::error(404, "scene_not_found", QString("Không có Scene %1 (kênh %2 có %3 Scene)").arg(scene).arg(channel).arg(list.size()));
    if (c->inTransition()) return CmdResult::error(409, "transition_in_progress", "Kênh đang chuyển cảnh");
    const int idx = scene - 1;
    engine_.post([c, idx](double) { c->setPreview(idx); });       // chi Preview; Program khong bao gio doi
    return CmdResult::success({{"channel", channel}, {"preview", scene}});
}

CmdResult ControlApi::cut(int channel) {
    CmdResult err;
    auto c = chan(channel, err);
    if (!c) return err;
    if (c->preview < 0) return CmdResult::error(409, "no_preview", "Kênh chưa có Scene ở Preview");
    engine_.post([c](double now) { c->cut(now); });
    return CmdResult::success({{"channel", channel}});
}

CmdResult ControlApi::take(int channel, const QJsonObject& o) {
    CmdResult err;
    auto c = chan(channel, err);
    if (!c) return err;
    TransitionSpec spec;
    spec.duration = 1.0;
    QString type = "fade";
    if (o.contains("type")) {
        if (!o["type"].isString()) return CmdResult::error(400, "bad_request", "'type' phải là chuỗi");
        type = o["type"].toString().toLower();
    }
    if (o.contains("duration")) {
        if (!o["duration"].isDouble()) return CmdResult::error(400, "bad_request", "'duration' phải là số (giây)");
        spec.duration = o["duration"].toDouble();
    }
    if (!(spec.duration >= 0.1 && spec.duration <= 10.0)) return CmdResult::error(400, "bad_request", "'duration' phải trong khoảng 0,1–10 giây");
    if (o.contains("easing")) {
        const QString e = o["easing"].toString().toLower();
        if (!o["easing"].isString() || (e != "linear" && e != "easeinout" && e != "easein" && e != "easeout"))
            return CmdResult::error(400, "bad_request", "'easing' phải là linear | easeinout | easein | easeout");
        spec.easing = e == "linear" ? Easing::Linear : e == "easein" ? Easing::EaseIn : e == "easeout" ? Easing::EaseOut : Easing::EaseInOut;
    }
    static const QMap<QString, TransitionType> kBuiltin = {
        {"cut", TransitionType::Cut}, {"fade", TransitionType::Fade}, {"dip", TransitionType::DipToBlack},
        {"wipeleft", TransitionType::WipeLeft}, {"wiperight", TransitionType::WipeRight},
        {"wipeup", TransitionType::WipeUp}, {"wipedown", TransitionType::WipeDown}};
    if (kBuiltin.contains(type)) {
        spec.type = kBuiltin[type];
    } else {
        QString id;
        if (type == "plugin") {
            if (!o["plugin"].isString()) return CmdResult::error(400, "bad_request", "type=plugin cần trường 'plugin' (id)");
            id = o["plugin"].toString();
        } else if (type == "push" || type == "slide" || type == "zoom" || type == "luma") {
            id = "builtin." + type;
        } else if (type == "stinger") {
            return CmdResult::error(400, "unsupported", "Stinger chưa hỗ trợ qua điều khiển ngoài");
        } else {
            return CmdResult::error(400, "bad_request", "Kiểu chuyển không hợp lệ: " + type.left(40));
        }
        auto def = engine_.registry().find(id);
        if (!def || def->kind != EffectDef::Transition) return CmdResult::error(400, "unknown_plugin", "Không có Transition: " + id.left(60));
        spec.type = TransitionType::Plugin;
        spec.pluginId = id;
        if (o.contains("params") && o["params"].isObject()) spec.params = o["params"].toObject().toVariantMap();
    }
    if (c->preview < 0) return CmdResult::error(409, "no_preview", "Kênh chưa có Scene ở Preview");
    if (c->inTransition()) return CmdResult::error(409, "transition_in_progress", "Kênh đang chuyển cảnh");
    engine_.post([c, spec](double now) { c->startTransition(spec, now); });
    return CmdResult::success({{"channel", channel}});
}

CmdResult ControlApi::ftb(int channel, std::optional<bool> on, double seconds) {
    CmdResult err;
    auto c = chan(channel, err);
    if (!c) return err;
    if (!(seconds >= 0.1 && seconds <= 10.0)) return CmdResult::error(400, "bad_request", "'duration' phải trong khoảng 0,1–10 giây");
    engine_.post([c, on, seconds](double) { if (!on || *on != c->ftbTarget.load()) c->toggleFtb(seconds); });
    return CmdResult::success({{"channel", channel}});
}

CmdResult ControlApi::audioMute(int channel, std::optional<bool> on) {
    CmdResult err;
    auto c = chan(channel, err);
    if (!c) return err;
    engine_.post([c, on](double) { c->mute = on ? *on : !c->mute.load(); });
    return CmdResult::success({{"channel", channel}});
}
CmdResult ControlApi::audioVolume(int channel, double linear) {
    CmdResult err;
    auto c = chan(channel, err);
    if (!c) return err;
    if (!std::isfinite(linear) || linear < 0.0 || linear > 2.0) return CmdResult::error(400, "bad_request", "Âm lượng phải trong khoảng 0–2 (1 = nguyên gốc) hoặc -60..+6 dB");
    const float v = float(linear);
    engine_.post([c, v](double) { c->volume = v; });
    return CmdResult::success({{"channel", channel}, {"volume", std::round(linear * 1000) / 1000}});
}
CmdResult ControlApi::audioInfo() {
    AudioMixer& m = engine_.audio();
    QJsonObject o;
    o["running"] = m.running();
    o["device"] = m.deviceName();
    o["deviceId"] = m.deviceId();
    o["underruns"] = double(m.outputUnderruns());          // so lan thiet bi doi du lieu ma mixer chua kip dua
    o["clippedBlocks"] = double(m.clippedBlocks());        // so khoi 10 ms bi cat o master (qua +-1)
    auto lv = [](const AudioMeter& mt) {
        auto r2 = [](double x) { return std::round(x * 1000) / 1000; };
        return QJsonObject{{"peak", QJsonArray{r2(mt.peak[0]), r2(mt.peak[1])}}, {"rms", QJsonArray{r2(mt.rms[0]), r2(mt.rms[1])}}};
    };
    QJsonArray chs;
    for (size_t i = 0; i < channels_.size(); ++i) {
        QJsonObject c = lv(m.channelMeter(int(i)));
        c["index"] = int(i + 1);
        c["volume"] = std::round(double(channels_[i]->volume.load()) * 1000) / 1000;
        c["mute"] = channels_[i]->mute.load();
        chs.append(c);
    }
    o["channels"] = chs;
    o["master"] = lv(m.masterMeter());
    return CmdResult::success(o);
}
CmdResult ControlApi::outputFlag(int output, OutFlag f, std::optional<bool> on) {
    if (output < 1 || output > engine_.outputCount())
        return CmdResult::error(404, "output_not_found", QString("Không có Output %1 (có %2 Output)").arg(output).arg(engine_.outputCount()));
    OutputSink* sink = engine_.output(output - 1);
    engine_.post([sink, f, on](double) {
        std::atomic<bool>& a = f == OutFlag::Black ? sink->black : f == OutFlag::Freeze ? sink->freeze : sink->testPattern;
        a = on ? *on : !a.load();
    });
    return CmdResult::success({{"output", output}});
}

CmdResult ControlApi::blackAll(std::optional<bool> on) {
    const int n = engine_.outputCount();
    std::vector<OutputSink*> sinks;
    for (int i = 0; i < n; ++i) sinks.push_back(engine_.output(i));
    engine_.post([sinks, on](double) {
        bool all = !sinks.empty();
        for (auto* s : sinks) all = all && s->black;
        const bool v = on ? *on : !all;
        for (auto* s : sinks) s->black = v;
    });
    return CmdResult::success();
}

CmdResult ControlApi::sceneTransport(int scene, Transport t, double seconds) {
    if (scene < 1 || scene > int(scenes_.size()))
        return CmdResult::error(404, "scene_not_found", QString("Không có Scene %1 (có %2 Scene)").arg(scene).arg(scenes_.size()));
    auto sc = scenes_[scene - 1];
    switch (t) {
    case Transport::Play: engine_.post([sc](double now) { sc->clock.play(now, sc->duration); }); break;
    case Transport::Pause: engine_.post([sc](double) { sc->clock.pause(); }); break;
    case Transport::Stop: engine_.post([sc](double) { sc->clock.stop(); }); break;
    case Transport::Seek:
        if (!std::isfinite(seconds) || seconds < 0 || seconds > 86400) return CmdResult::error(400, "bad_request", "'time' phải là số giây ≥ 0");
        engine_.post([sc, seconds](double) { sc->clock.seek(std::min(seconds, sc->duration)); });
        break;
    }
    return CmdResult::success({{"scene", scene}});
}

// ---------------------------------------------------------------- trang thai

QJsonObject ControlApi::state() {
    QJsonObject root;
    root["version"] = 1;
    root["locked"] = locked();
    QJsonObject eng;
    eng["fps"] = std::round(engine_.fps() * 10) / 10;
    eng["droppedFrames"] = double(engine_.droppedFrames());
    eng["frameP50"] = std::round(engine_.frameP50() * 10) / 10;
    eng["frameP99"] = std::round(engine_.frameP99() * 10) / 10;
    eng["stalled"] = engine_.stalled();
    root["engine"] = eng;

    QJsonArray chs;
    for (size_t i = 0; i < channels_.size(); ++i) {
        const Channel& c = *channels_[i];
        const auto& list = chScenes_[i];
        const int pg = c.program, pv = c.preview;
        QJsonObject o;
        o["index"] = int(i + 1);
        o["name"] = c.name;
        o["program"] = pg >= 0 && pg < int(list.size()) ? QJsonValue(pg + 1) : QJsonValue();
        o["preview"] = pv >= 0 && pv < int(list.size()) ? QJsonValue(pv + 1) : QJsonValue();
        o["programName"] = pg >= 0 && pg < int(list.size()) ? list[pg]->name : QString();
        o["previewName"] = pv >= 0 && pv < int(list.size()) ? list[pv]->name : QString();
        o["inTransition"] = c.inTransition();
        o["ftb"] = c.ftbTarget.load();
        o["ftbAmount"] = std::round(double(c.ftbAmount.load()) * 100) / 100;
        o["audio"] = QJsonObject{{"volume", std::round(double(c.volume.load()) * 100) / 100}, {"mute", c.mute.load()}};
        QJsonArray names;
        for (const auto& s : list) names.append(s->name);
        o["scenes"] = names;
        chs.append(o);
    }
    root["channels"] = chs;

    QJsonArray scs;
    for (size_t i = 0; i < scenes_.size(); ++i) {
        QJsonObject o;
        o["index"] = int(i + 1);
        o["name"] = scenes_[i]->name;
        o["time"] = std::round(scenes_[i]->clock.time.load() * 10) / 10;
        o["playing"] = scenes_[i]->clock.playing.load();
        scs.append(o);
    }
    root["scenes"] = scs;

    QJsonArray outs;
    for (size_t i = 0; i < outs_.size() && int(i) < engine_.outputCount(); ++i) {
        OutputSink* s = engine_.output(int(i));
        QJsonObject o;
        o["index"] = int(i + 1);
        o["name"] = outs_[i].name;
        o["channel"] = outs_[i].channel + 1;
        o["width"] = outs_[i].size.width();
        o["height"] = outs_[i].size.height();
        o["black"] = s->black.load();
        o["freeze"] = s->freeze.load();
        o["pattern"] = s->testPattern.load();
        o["health"] = healthId(sup_ ? sup_->health(int(i)) : OutputHealth::Ok);
        o["fps"] = std::round(outs_[i].fps);
        outs.append(o);
    }
    root["outputs"] = outs;
    return root;
}

QByteArray ControlApi::stateJson() { return QJsonDocument(state()).toJson(QJsonDocument::Compact); }

// ---------------------------------------------------------------- HTTP

void ControlApi::addRoute(const QByteArray& method, const QString& pattern, bool edit, RouteFn fn) {
    routes_.push_back({method, pattern.split('/', Qt::SkipEmptyParts), edit, std::move(fn)});
}

void ControlApi::buildDefaultRoutes() {
    auto idx = [](const QString& s, int& v, CmdResult& err) {
        if (parseIndex(s, v)) return true;
        err = CmdResult::error(400, "bad_request", "Chỉ số phải là số nguyên");
        return false;
    };
    addRoute("GET", "/api/ping", false, [](const QStringList&, const QJsonObject&) { return CmdResult::success({{"pong", true}}); });
    addRoute("GET", "/api/state", false, [this](const QStringList&, const QJsonObject&) { return CmdResult::success(state()); });
    addRoute("POST", "/api/channels/{n}/preview", false, [this, idx](const QStringList& p, const QJsonObject& b) {
        int n; CmdResult e;
        if (!idx(p[0], n, e)) return e;
        if (!b["scene"].isDouble() || b["scene"].toDouble() != std::floor(b["scene"].toDouble()) || std::abs(b["scene"].toDouble()) > 1e6)
            return CmdResult::error(400, "bad_request", "Cần trường số nguyên 'scene' (từ 1)");
        return setPreview(n, int(b["scene"].toDouble()));
    });
    addRoute("POST", "/api/channels/{n}/cut", false, [this, idx](const QStringList& p, const QJsonObject&) {
        int n; CmdResult e;
        return idx(p[0], n, e) ? cut(n) : e;
    });
    addRoute("POST", "/api/channels/{n}/take", false, [this, idx](const QStringList& p, const QJsonObject& b) {
        int n; CmdResult e;
        return idx(p[0], n, e) ? take(n, b) : e;
    });
    addRoute("POST", "/api/channels/{n}/ftb", false, [this, idx](const QStringList& p, const QJsonObject& b) {
        int n; CmdResult e; std::optional<bool> on;
        if (!idx(p[0], n, e)) return e;
        if (!readOptBool(b, "on", on, e)) return e;
        double d = 1.0;
        if (b.contains("duration")) { if (!b["duration"].isDouble()) return CmdResult::error(400, "bad_request", "'duration' phải là số"); d = b["duration"].toDouble(); }
        return ftb(n, on, d);
    });
    addRoute("POST", "/api/channels/{n}/mute", false, [this, idx](const QStringList& p, const QJsonObject& b) {
        int n; CmdResult e; std::optional<bool> on;
        if (!idx(p[0], n, e) || !readOptBool(b, "on", on, e)) return e;
        return audioMute(n, on);
    });
    addRoute("POST", "/api/channels/{n}/volume", false, [this, idx](const QStringList& p, const QJsonObject& b) {
        int n; CmdResult e;
        if (!idx(p[0], n, e)) return e;
        if (b.contains("db")) {
            if (!b["db"].isDouble()) return CmdResult::error(400, "bad_request", "'db' phải là số");
            const double db = b["db"].toDouble();
            if (!std::isfinite(db) || db < -60.0 || db > 6.0) return CmdResult::error(400, "bad_request", "'db' phải trong khoảng -60..+6");
            return audioVolume(n, db <= -60.0 ? 0.0 : std::pow(10.0, db / 20.0));
        }
        if (!b["volume"].isDouble()) return CmdResult::error(400, "bad_request", "Cần trường số 'volume' (0–2) hoặc 'db'");
        return audioVolume(n, b["volume"].toDouble());
    });
    addRoute("GET", "/api/audio", false, [this](const QStringList&, const QJsonObject&) { return audioInfo(); });
    for (const auto& [name, flag] : {std::pair<const char*, OutFlag>{"black", OutFlag::Black}, {"freeze", OutFlag::Freeze}, {"pattern", OutFlag::Pattern}})
        addRoute("POST", QString("/api/outputs/{i}/") + name, false, [this, idx, flag = flag](const QStringList& p, const QJsonObject& b) {
            int n; CmdResult e; std::optional<bool> on;
            if (!idx(p[0], n, e) || !readOptBool(b, "on", on, e)) return e;
            return outputFlag(n, flag, on);
        });
    addRoute("POST", "/api/black-all", false, [this](const QStringList&, const QJsonObject& b) {
        CmdResult e; std::optional<bool> on;
        return readOptBool(b, "on", on, e) ? blackAll(on) : e;
    });
    for (const auto& [name, t] : {std::pair<const char*, Transport>{"play", Transport::Play}, {"pause", Transport::Pause}, {"stop", Transport::Stop}})
        addRoute("POST", QString("/api/scenes/{n}/") + name, false, [this, idx, t = t](const QStringList& p, const QJsonObject&) {
            int n; CmdResult e;
            return idx(p[0], n, e) ? sceneTransport(n, t) : e;
        });
    addRoute("POST", "/api/scenes/{n}/seek", false, [this, idx](const QStringList& p, const QJsonObject& b) {
        int n; CmdResult e;
        if (!idx(p[0], n, e)) return e;
        if (!b["time"].isDouble()) return CmdResult::error(400, "bad_request", "Cần trường số 'time' (giây)");
        return sceneTransport(n, Transport::Seek, b["time"].toDouble());
    });
}

HttpResponse ControlApi::reply(const CmdResult& r) {
    QJsonObject o;
    if (r.ok()) {
        o = r.data;
        o["ok"] = true;
    } else {
        o["ok"] = false;
        o["error"] = r.code;
        o["message"] = r.message;
    }
    return HttpResponse::json(r.status, QJsonDocument(o).toJson(QJsonDocument::Compact));
}

HttpResponse ControlApi::errorReply(int status, const QString& code, const QString& msg, const HttpRequest& req) {
    noteRejected(req.peer.toString(), QString("%1 %2 → %3 %4").arg(QString::fromLatin1(req.method), QString::fromUtf8(req.path).left(80)).arg(status).arg(code));
    return reply(CmdResult::error(status, code, msg));
}

HttpResponse ControlApi::page() {
    HttpResponse r;
    r.contentType = "text/html; charset=utf-8";
    r.body = controlPageHtml();
    r.headers.append(std::make_pair(QByteArray("Content-Security-Policy"), QByteArray("default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'")));
    r.headers.append(std::make_pair(QByteArray("X-Frame-Options"), QByteArray("DENY")));
    return r;
}

HttpResponse ControlApi::handleHttp(const HttpRequest& req) {
    const QByteArray host = req.header("host");
    if (strictHost_ && !host.isEmpty() && !isLoopbackHost(host))
        return errorReply(403, "bad_host", "Host không hợp lệ", req);
    const QByteArray origin = req.header("origin");
    if (!origin.isEmpty() && !sameOrigin(origin, host))
        return errorReply(403, "bad_origin", "Origin không được phép (chống CSRF)", req);

    if (req.path == "/") {
        if (req.method != "GET" && req.method != "HEAD") return errorReply(405, "method_not_allowed", "Chỉ hỗ trợ GET", req);
        return page();
    }
    if (!req.path.startsWith("/api/")) return errorReply(404, "not_found", "Không tìm thấy", req);

    // xac thuc (hang thoi gian)
    if (requireToken_ && token_.isEmpty()) return errorReply(401, "token_required", "Máy chủ yêu cầu token nhưng chưa đặt", req);
    if (!token_.isEmpty()) {
        const QByteArray auth = req.header("authorization").trimmed();
        const int sp = auth.indexOf(' ');
        const bool schemeOk = sp > 0 && auth.left(sp).toLower() == "bearer";
        const QByteArray given = schemeOk ? auth.mid(sp + 1).trimmed() : QByteArray();
        const bool eq = constantTimeEquals(given, token_);
        if (!schemeOk || !eq) {
            HttpResponse r = errorReply(401, "unauthorized", "Thiếu hoặc sai token (Authorization: Bearer ...)", req);
            r.headers.append(std::make_pair(QByteArray("WWW-Authenticate"), QByteArray("Bearer")));
            return r;
        }
    }

    if (req.path == "/api/events") {
        if (req.method != "GET") return errorReply(405, "method_not_allowed", "Chỉ hỗ trợ GET", req);
        HttpResponse r;
        r.sse = true;
        r.body = "retry: 2000\nevent: state\ndata: " + stateJson() + "\n\n";
        return r;
    }

    const QStringList segs = QString::fromUtf8(req.path).split('/', Qt::SkipEmptyParts);
    const Route* hit = nullptr;
    QStringList params;
    QStringList allow;
    for (const Route& r : routes_) {
        if (r.segs.size() != segs.size()) continue;
        QStringList ps;
        bool m = true;
        for (int i = 0; i < segs.size() && m; ++i) {
            if (r.segs[i].startsWith('{')) ps << segs[i];
            else m = r.segs[i] == segs[i];
        }
        if (!m) continue;
        if (r.method == req.method) { hit = &r; params = ps; break; }
        allow << QString::fromLatin1(r.method);
    }
    if (!hit) {
        if (!allow.isEmpty()) {
            HttpResponse r = errorReply(405, "method_not_allowed", "Phương thức không được hỗ trợ cho đường dẫn này", req);
            r.headers.append(std::make_pair(QByteArray("Allow"), allow.join(", ").toLatin1()));
            return r;
        }
        return errorReply(404, "not_found", "Không có đường dẫn này", req);
    }
    if (hit->edit && locked()) return errorReply(423, "locked", "Đang khoá vận hành: lệnh sửa cấu hình bị từ chối", req);

    QJsonObject body;
    if (req.method == "POST" && !req.body.trimmed().isEmpty()) {
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(req.body, &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject())
            return errorReply(400, "invalid_json", "Nội dung phải là một đối tượng JSON hợp lệ", req);
        body = doc.object();
    }
    CmdResult r;
    try { r = hit->fn(params, body); }
    catch (...) { r = CmdResult::error(500, "internal_error", "Lỗi nội bộ"); }
    if (!r.ok()) {
        noteRejected(req.peer.toString(), QString("%1 %2 → %3 %4").arg(QString::fromLatin1(req.method), QString::fromUtf8(req.path).left(80)).arg(r.status).arg(r.code));
    }
    return reply(r);
}

// ---------------------------------------------------------------- OSC

CmdResult ControlApi::handleOsc(const OscMessage& m) {
    const QStringList a = m.address.split('/', Qt::SkipEmptyParts);
    auto bad = [&](const QString& why) { return CmdResult::error(400, "bad_request", why); };
    auto argInt = [&](int k, int& out) {
        if (k >= m.args.size()) return false;
        const QVariant& v = m.args[k];
        if (v.typeId() == QMetaType::Int || v.typeId() == QMetaType::LongLong) { out = v.toInt(); return true; }
        if (v.typeId() == QMetaType::Double) {
            const double d = v.toDouble();
            if (std::isfinite(d) && d == std::floor(d) && std::abs(d) < 1e6) { out = int(d); return true; }
        }
        return false;
    };
    if (a.size() < 2 || a[0] != "lumacast") return CmdResult::error(404, "unknown_address", "Địa chỉ OSC phải bắt đầu bằng /lumacast/");
    CmdResult err;
    std::optional<bool> on;

    if ((a[1] == "blackall" || a[1] == "black-all") && a.size() == 2) {
        if (!oscBool(m.args.value(0), on, err)) return err;
        return blackAll(on);
    }
    if (a.size() != 4) return CmdResult::error(404, "unknown_address", "Địa chỉ OSC không hợp lệ: " + m.address.left(60));
    int n;
    if (!parseIndex(a[2], n)) return bad("Chỉ số trong địa chỉ phải là số nguyên");
    const QString& act = a[3];
    if (a[1] == "channel") {
        if (act == "preview") {
            int s;
            if (!argInt(0, s)) return bad("preview cần một số nguyên (số Scene, từ 1)");
            return setPreview(n, s);
        }
        if (act == "cut") return cut(n);
        if (act == "mute") {
            if (!oscBool(m.args.value(0), on, err)) return err;
            return audioMute(n, on);
        }
        if (act == "volume") {
            if (m.args.isEmpty() || !(m.args[0].typeId() == QMetaType::Double || m.args[0].typeId() == QMetaType::Int)) return bad("volume cần một số (0–2, 1 = nguyên gốc)");
            return audioVolume(n, m.args[0].toDouble());
        }
        if (act == "ftb") {
            if (!oscBool(m.args.value(0), on, err)) return err;
            return ftb(n, on);
        }
        if (act == "take") {
            QJsonObject o;
            int strings = 0;
            for (const QVariant& v : m.args) {
                if (v.typeId() == QMetaType::QString) o[strings++ == 0 ? "type" : "easing"] = v.toString();
                else if (v.typeId() == QMetaType::Int || v.typeId() == QMetaType::Double) o["duration"] = v.toDouble();
            }
            return take(n, o);
        }
    } else if (a[1] == "output") {
        const OutFlag f = act == "black" ? OutFlag::Black : act == "freeze" ? OutFlag::Freeze : OutFlag::Pattern;
        if (act == "black" || act == "freeze" || act == "pattern") {
            if (!oscBool(m.args.value(0), on, err)) return err;
            return outputFlag(n, f, on);
        }
    } else if (a[1] == "scene") {
        if (act == "play") return sceneTransport(n, Transport::Play);
        if (act == "pause") return sceneTransport(n, Transport::Pause);
        if (act == "stop") return sceneTransport(n, Transport::Stop);
        if (act == "seek") {
            if (m.args.isEmpty() || !(m.args[0].typeId() == QMetaType::Double || m.args[0].typeId() == QMetaType::Int)) return bad("seek cần một số giây");
            return sceneTransport(n, Transport::Seek, m.args[0].toDouble());
        }
    }
    return CmdResult::error(404, "unknown_address", "Địa chỉ OSC không hợp lệ: " + m.address.left(60));
}

} // namespace lc
