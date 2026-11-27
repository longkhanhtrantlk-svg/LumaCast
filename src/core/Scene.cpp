#include "Scene.h"
#include <algorithm>
#include <cmath>

namespace lc {

namespace {
bool hasKeyframes(const Scene& s) {
    for (const Layer& l : s.layers)
        for (const Track& t : l.tracks)
            if (!t.keys.empty()) return true;
    return false;
}
// Moi video cua Scene mot lan (mot nguon co the nam o nhieu Layer).
std::vector<Source*> videos(const Scene& s) {
    std::vector<Source*> v;
    for (const Layer& l : s.layers)
        if (l.source && l.source->hasTransport() && std::find(v.begin(), v.end(), l.source.get()) == v.end()) v.push_back(l.source.get());
    return v;
}
} // namespace

SceneMediaState sceneMediaState(const Scene& s) {
    SceneMediaState st;
    st.loop = s.loop;
    st.keyframes = hasKeyframes(s);
    for (const Layer& l : s.layers)
        if (l.source && l.source->audio()) st.audio = true;
    const auto v = videos(s);
    st.video = !v.empty();
    for (const Source* src : v) {
        st.playing = st.playing || (!src->paused() && !src->ended());
        if (src->durationSeconds() > st.duration) {
            st.duration = src->durationSeconds();
            st.position = std::clamp(src->positionSeconds(), 0.0, st.duration);
        }
    }
    if (!st.video && st.keyframes) {
        st.playing = s.clock.playing;
        st.duration = s.duration;
        st.position = std::clamp(double(s.clock.time), 0.0, s.duration);
    }
    return st;
}

void scenePlay(Scene& s, double now) {
    for (Source* src : videos(s)) src->setPaused(false);
    if (hasKeyframes(s)) s.clock.play(now, s.duration);
}

void scenePause(Scene& s) {
    for (Source* src : videos(s)) src->setPaused(true);
    s.clock.pause();
}

void sceneSeek(Scene& s, double seconds) {
    if (!std::isfinite(seconds)) return;
    seconds = std::max(0.0, seconds);
    for (Source* src : videos(s)) src->seekTo(seconds);
    if (s.duration > 0) s.clock.seek(seconds <= s.duration ? seconds : s.loop ? std::fmod(seconds, s.duration) : s.duration);
}

LayerState evalLayer(const Layer& l, double t) {
    LayerState s;
    double x = l.rect.x(), y = l.rect.y(), w = l.rect.width(), h = l.rect.height();
    double op = l.opacity, rot = l.rotation;
    for (const Track& tr : l.tracks) {
        if (tr.keys.empty()) continue;
        const double v = evalTrack(tr, t);
        switch (tr.prop) {
        case AnimProp::X: x = v; break;
        case AnimProp::Y: y = v; break;
        case AnimProp::W: w = v; break;
        case AnimProp::H: h = v; break;
        case AnimProp::Opacity: op = v; break;
        case AnimProp::Rotation: rot = v; break;
        }
    }
    s.rect = QRectF(x, y, w, h);
    s.opacity = float(std::clamp(op, 0.0, 1.0));
    s.rotation = float(rot);
    return s;
}

} // namespace lc
