#include "Playlist.h"
#include "core/I18n.h"
#include "core/Channel.h"
#include <QJsonArray>
#include <QtGlobal>
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace lc {

namespace {
// Muc video "tu dong": chuyen som hon cuoi video it nhat chung nay (ke ca CUT) de khong lo khung dau cua vong lap.
constexpr double kClipMargin = 0.15;
constexpr double kMinTransition = 0.1, kMaxTransition = 10.0;

struct TransId { TransitionType type; const char* id; };
constexpr TransId kTransIds[] = {
    {TransitionType::Cut, "cut"},           {TransitionType::Fade, "fade"},           {TransitionType::DipToBlack, "dip"},
    {TransitionType::WipeLeft, "wipeleft"}, {TransitionType::WipeRight, "wiperight"}, {TransitionType::WipeUp, "wipeup"},
    {TransitionType::WipeDown, "wipedown"}, {TransitionType::Plugin, "plugin"}};

double finiteOr(double v, double lo, double hi, double fallback) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; }

bool channelHas(const Channel& ch, const Scene* s) {
    return std::any_of(ch.scenes.begin(), ch.scenes.end(), [s](const auto& p) { return p.get() == s; });
}

bool sceneUses(const Scene* s, const Source* src) {
    return s && std::any_of(s->layers.begin(), s->layers.end(), [src](const Layer& l) { return l.visible && l.source.get() == src; });
}
} // namespace

bool PlaylistData::isDefault() const {
    const PlaylistData d;
    return items.empty() && imageSeconds == d.imageSeconds && loop == d.loop && transition == d.transition
           && transitionSeconds == d.transitionSeconds;
}

TransitionSpec PlaylistData::spec() const {
    TransitionSpec s;
    s.type = transition == TransitionType::Stinger ? TransitionType::Fade : transition;
    s.pluginId = pluginId;
    s.duration = s.type == TransitionType::Cut ? 0.0 : transitionSeconds;
    return s;
}

int newPlaylistItemId() {
    static std::atomic<int> last{0};
    return ++last;
}

double sceneClipSeconds(const Scene& s) {
    double len = 0;
    for (const Layer& l : s.layers)
        if (l.visible && l.source) len = std::max(len, l.source->durationSeconds());
    return len;
}

// ---------------------------------------------------------------- JSON

QJsonObject playlistToJson(const PlaylistData& d, const std::vector<std::shared_ptr<Scene>>& scenes) {
    QJsonObject o;
    o["loop"] = d.loop;
    o["imageSeconds"] = d.imageSeconds;
    for (const TransId& t : kTransIds)
        if (t.type == d.transition) o["transition"] = t.id;
    if (d.transition == TransitionType::Plugin) o["transitionPlugin"] = d.pluginId;
    o["transitionSeconds"] = d.transitionSeconds;
    QJsonArray items;
    for (const PlaylistItem& it : d.items) {
        const auto k = std::find(scenes.begin(), scenes.end(), it.scene);
        if (k == scenes.end()) continue;                       // Scene da bi go khoi project: khong luu muc
        QJsonObject io;
        io["scene"] = int(k - scenes.begin());
        if (it.seconds > 0) io["seconds"] = it.seconds;
        items.append(io);
    }
    o["items"] = items;
    return o;
}

PlaylistData playlistFromJson(const QJsonObject& o, const std::vector<std::shared_ptr<Scene>>& scenes) {
    PlaylistData d;
    d.loop = o["loop"].toBool(true);
    d.imageSeconds = finiteOr(o["imageSeconds"].toDouble(d.imageSeconds), kPlaylistMinSeconds, kPlaylistMaxSeconds, d.imageSeconds);
    const QString tn = o["transition"].toString("fade");
    for (const TransId& t : kTransIds)
        if (tn == t.id) d.transition = t.type;
    d.pluginId = o["transitionPlugin"].toString();
    if (d.transition == TransitionType::Plugin && d.pluginId.isEmpty()) d.transition = TransitionType::Fade;
    d.transitionSeconds = finiteOr(o["transitionSeconds"].toDouble(1.0), kMinTransition, kMaxTransition, 1.0);
    for (const QJsonValue& v : o["items"].toArray()) {
        const QJsonObject io = v.toObject();
        const int k = io["scene"].toInt(-1);
        if (k < 0 || k >= int(scenes.size())) {
            qWarning().noquote() << QString("Playlist: bỏ qua mục trỏ tới Scene không tồn tại (%1)").arg(k);
            continue;
        }
        PlaylistItem it;
        it.id = newPlaylistItemId();
        it.scene = scenes[size_t(k)];
        const double s = io["seconds"].toDouble(0);
        it.seconds = std::isfinite(s) && s > 0 ? std::clamp(s, kPlaylistMinSeconds, kPlaylistMaxSeconds) : 0;
        d.items.push_back(std::move(it));
    }
    return d;
}

// ---------------------------------------------------------------- bo chay

int Playlist::indexOf(int itemId) const {
    for (size_t i = 0; i < data.items.size(); ++i)
        if (data.items[i].id == itemId) return int(i);
    return -1;
}

QString Playlist::label(int pos) const {
    const auto& s = data.items[size_t(pos)].scene;
    return QString(LC_TR("mục %1 '%2'")).arg(pos + 1).arg(s ? s->name : QString("?"));
}

void Playlist::notice(const Channel& ch, const i18n::Text& msg) {
    const QString head = QString("Playlist %1: ").arg(ch.name);
    qWarning().noquote() << head + msg.source;
    std::lock_guard lk(noticeMutex_);
    if (notices_.size() < 50) notices_.push_back(head + msg.shown);
}

std::vector<QString> Playlist::takeNotices() {
    std::lock_guard lk(noticeMutex_);
    std::vector<QString> out;
    out.swap(notices_);
    return out;
}

QString Playlist::problemOf(const Channel& ch, const PlaylistItem& it) const {
    if (!it.scene || !channelHas(ch, it.scene.get())) return LC_TR("Scene không còn trong kênh");
    for (const Layer& l : it.scene->layers) {
        if (!l.visible || !l.source) continue;
        const QString e = l.source->error();
        if (!e.isEmpty()) return e;
    }
    return {};
}

void Playlist::play(Channel& ch, double now, int itemId) {
    const State was = state_;
    if (data.items.empty()) return;
    int pos = itemId ? indexOf(itemId) : -1;
    if (itemId && pos < 0) return;
    seenOps_ = ch.operatorOps();
    fails_ = 0;
    state_ = State::Playing;
    if (was == State::Stopped) {
        current_ = 0;
        onAir_.reset();
        cueFrom(ch, now, pos < 0 ? 0 : pos);
    } else if (pos >= 0 && !(was == State::Paused && itemId == current_)) {
        cueFrom(ch, now, pos);
    } else if (was == State::Paused) {
        if (cue_) {                                                       // tam dung luc dang chuan bi: chuan bi lai
            const int k = indexOf(cue_);
            cueFrom(ch, now, k >= 0 ? k : cuePos_);
        } else if (onAir_ && ch.showing(onAir_.get())) {                  // hinh van la muc dang do: chay tiep
            const double len = seconds_ > 0 ? 0 : sceneClipSeconds(*onAir_);
            double position = -1;                                          // vi tri that cua video (co the da bi tam dung / tua tay)
            for (const Layer& l : onAir_->layers)
                if (l.visible && l.source && l.source->hasTransport()) {
                    l.source->setPaused(false);                            // chay lai playlist = chay lai video
                    if (l.source->durationSeconds() >= len) position = l.source->positionSeconds();
                }
            if (len > 0 && position >= 0) { started_ = now - position; inLen_ = 0; }
            else if (len > 0) started_ = now - std::fmod(std::max(0.0, now - started_), len);   // video van lap trong luc tam dung
            else started_ += now - pausedAt_;
        } else {                                                          // nguoi van hanh da doi hinh: dua muc dang do len lai
            const int k = indexOf(current_);
            cueFrom(ch, now, k >= 0 ? k : currentPos_);
        }
    }
    tryAir(ch, now);
    publish(now);
}

void Playlist::pause(double now) {
    if (state_ != State::Playing) return;
    state_ = State::Paused;
    pausedAt_ = now;
    publish(now);
}

void Playlist::stop() {
    state_ = State::Stopped;
    current_ = 0;
    cue_ = 0;
    onAir_.reset();
    remaining_ = -1;
}

void Playlist::step(Channel& ch, double now, int dir) {
    if (state_ == State::Stopped || data.items.empty()) return;
    const int n = int(data.items.size());
    const int ref = cue_ ? cue_ : current_.load();
    int k = indexOf(ref);
    int pos;
    if (k >= 0) pos = k + dir;
    else pos = (cue_ ? cuePos_ : currentPos_) + (dir > 0 ? 0 : -1);        // muc hien tai da bi xoa
    if (pos < 0 || pos >= n) {
        if (!data.loop) return;
        pos = (pos % n + n) % n;
    }
    seenOps_ = ch.operatorOps();
    fails_ = 0;
    // Muc loi khi bam "truoc": cueFrom bo qua theo chieu tien, nen lui them qua cac muc loi truoc
    for (int i = 0; dir < 0 && i < n && !problemOf(ch, data.items[size_t(pos)]).isEmpty() && (data.loop || pos > 0); ++i)
        pos = (pos - 1 + n) % n;
    cueFrom(ch, now, pos);
    tryAir(ch, now);
    publish(now);
}

void Playlist::cueFrom(Channel& ch, double now, int pos) {
    cue_ = 0;
    const int n = int(data.items.size());
    while (n > 0) {
        if (pos >= n) {
            if (!data.loop) {
                notice(ch, LC_BOTH(LC_TR("đã phát hết danh sách")));
                stop();
                return;
            }
            pos = 0;
        }
        const PlaylistItem& it = data.items[size_t(pos)];
        const QString why = problemOf(ch, it);
        if (why.isEmpty()) break;
        notice(ch, LC_BOTH(QString(LC_TR("bỏ qua %1: %2")).arg(label(pos), problemOf(ch, it))));
        if (++fails_ >= n) {
            notice(ch, LC_BOTH(LC_TR("không còn mục nào phát được, đã dừng")));
            stop();
            return;
        }
        ++pos;
    }
    if (n == 0) { stop(); return; }

    const PlaylistItem& it = data.items[size_t(pos)];
    cue_ = it.id;
    cuePos_ = pos;
    cueSince_ = now;
    // Tua video ve dau NGAY BAY GIO va doi co khung hinh roi moi chuyen (khong len song man den / khung cu).
    // Nguon dang hien o Program thi khong tua truoc (se lam trong hinh dang phat) - tua luc chuyen.
    cueRestarted_ = false;
    if (it.seconds > 0 && !it.scene->restartOnAir) return;
    const Scene* program = ch.programScene();
    for (const Layer& l : it.scene->layers)
        if (l.visible && l.source && l.source->durationSeconds() > 0 && sceneUses(program, l.source.get())) return;
    for (const Layer& l : it.scene->layers)
        if (l.visible && l.source) l.source->restart();
    cueRestarted_ = true;
}

void Playlist::fail(Channel& ch, double now, int pos, const i18n::Text& why) {
    i18n::Text msg = LC_BOTH(QString(LC_TR("bỏ qua %1: ")).arg(label(pos)));
    msg.shown += why.shown;
    msg.source += why.source;
    notice(ch, msg);
    cue_ = 0;
    if (++fails_ >= int(data.items.size())) {
        notice(ch, LC_BOTH(LC_TR("không còn mục nào phát được, đã dừng")));
        stop();
        return;
    }
    cueFrom(ch, now, pos + 1);
}

void Playlist::tryAir(Channel& ch, double now) {
    if (!cue_ || state_ == State::Stopped) return;
    const int pos = indexOf(cue_);
    if (pos < 0) { cueFrom(ch, now, cuePos_); return; }                    // muc vua bi xoa
    const PlaylistItem it = data.items[size_t(pos)];
    const QString why = problemOf(ch, it);
    if (!why.isEmpty()) { fail(ch, now, pos, LC_BOTH(problemOf(ch, it))); return; }
    for (const Layer& l : it.scene->layers) {
        if (!l.visible || !l.source || l.source->ready()) continue;
        if (now - cueSince_ < kReadyTimeout) return;                       // cho khung hinh dau; Program giu nguyen
        fail(ch, now, pos, LC_BOTH(LC_TR("video không ra hình")));
        return;
    }
    if (ch.inTransition()) return;

    const TransitionSpec spec = data.spec();
    double inLen = 0;
    if (!ch.showing(it.scene.get())) {
        const bool hadProgram = ch.programScene() != nullptr;
        if (!ch.startAuto(it.scene.get(), spec, now)) return;
        inLen = hadProgram ? spec.duration : 0;
    }
    if (!cueRestarted_ && (it.seconds <= 0 || it.scene->restartOnAir) && sceneClipSeconds(*it.scene) > 0)
        for (const Layer& l : it.scene->layers)
            if (l.visible && l.source) l.source->restart();

    cue_ = 0;
    fails_ = 0;
    current_ = it.id;
    currentPos_ = pos;
    onAir_ = it.scene;
    seconds_ = it.seconds;
    started_ = now;
    inLen_ = inLen;
    pausedAt_ = now;
}

double Playlist::switchAt() const {
    const int pos = indexOf(current_);
    const double seconds = pos >= 0 ? data.items[size_t(pos)].seconds : seconds_;
    const double clip = seconds > 0 || !onAir_ ? 0 : sceneClipSeconds(*onAir_);
    if (clip <= 0) return started_ + inLen_ + (seconds > 0 ? seconds : data.imageSeconds);
    // Video: bat dau chuyen som de hieu ung ket thuc dung luc video het (muc cuoi khong lap: phat tron video)
    const bool last = !data.loop && pos == int(data.items.size()) - 1;
    const double tail = last ? 0.0 : std::max(data.spec().duration, kClipMargin);
    return started_ + std::max(clip - tail, inLen_);
}

void Playlist::publish(double now) {
    if (state_ == State::Stopped || !current_) { remaining_ = -1; return; }
    const double ref = state_ == State::Paused ? pausedAt_ : now;
    remaining_ = std::max(0.0, switchAt() - ref);
}

void Playlist::tick(Channel& ch, double now) {
    if (state_ == State::Stopped) return;
    if (ch.operatorOps() != seenOps_) {                      // nguoi van hanh can thiep: giu hinh cua ho, khong gianh lai
        seenOps_ = ch.operatorOps();
        cue_ = 0;
        if (state_ == State::Playing) {
            pause(now);
            notice(ch, LC_BOTH(LC_TR("tạm dừng vì người vận hành đổi hình")));
        }
        return;
    }
    if (data.items.empty()) { stop(); return; }

    if (!cue_ && state_ == State::Playing) {
        if (!current_ || !onAir_) { cueFrom(ch, now, currentPos_); }
        else {
            const int k = indexOf(current_);
            if (k >= 0) currentPos_ = k;
            QString broken;
            for (const Layer& l : onAir_->layers)
                if (l.visible && l.source && broken.isEmpty()) broken = l.source->error();
            if (!broken.isEmpty()) {                          // hong giua chung
                notice(ch, LC_BOTH(QString(LC_TR("bỏ qua mục đang phát '%1': %2")).arg(onAir_->name, broken)));
                cueFrom(ch, now, k >= 0 ? k + 1 : currentPos_);
            } else if (!ch.inTransition() && now >= switchAt()) {
                cueFrom(ch, now, k >= 0 ? k + 1 : currentPos_);
            }
        }
    }
    tryAir(ch, now);
    publish(now);
}

} // namespace lc
