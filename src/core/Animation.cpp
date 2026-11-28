#include "Animation.h"
#include "core/I18n.h"
#include "core/Transition.h"
#include <algorithm>

namespace lc {

static const char* kPropIds[kAnimPropCount] = {"x", "y", "w", "h", "opacity", "rotation"};
static const char* kInterpIds[kInterpCount] = {"linear", "easein", "easeout", "easeinout", "hold"};

const char* animPropId(AnimProp p) { return kPropIds[int(p)]; }

QString animPropLabel(AnimProp p) {
    switch (p) {
    case AnimProp::X: return LC_TR("Vị trí X");
    case AnimProp::Y: return LC_TR("Vị trí Y");
    case AnimProp::W: return LC_TR("Rộng");
    case AnimProp::H: return LC_TR("Cao");
    case AnimProp::Opacity: return LC_TR("Độ mờ (alpha)");
    case AnimProp::Rotation: return LC_TR("Xoay (độ)");
    }
    return {};
}

bool animPropFromId(const QString& id, AnimProp& out) {
    for (int i = 0; i < kAnimPropCount; ++i) if (id == kPropIds[i]) { out = AnimProp(i); return true; }
    return false;
}

const char* interpId(Interp i) { return kInterpIds[int(i)]; }

QString interpLabel(Interp i) {
    switch (i) {
    case Interp::Linear: return LC_TR("Tuyến tính");
    case Interp::EaseIn: return LC_TR("Êm lúc đầu");
    case Interp::EaseOut: return LC_TR("Êm lúc cuối");
    case Interp::EaseInOut: return LC_TR("Êm hai đầu");
    case Interp::Hold: return LC_TR("Giữ nguyên (nhảy bậc)");
    }
    return {};
}

bool interpFromId(const QString& id, Interp& out) {
    for (int i = 0; i < kInterpCount; ++i) if (id == kInterpIds[i]) { out = Interp(i); return true; }
    return false;
}

double interpolate(Interp i, double a, double b, double u) {
    switch (i) {
    case Interp::Hold: return u >= 1.0 ? b : a;
    case Interp::Linear: return a + (b - a) * applyEasing(Easing::Linear, u);
    case Interp::EaseIn: return a + (b - a) * applyEasing(Easing::EaseIn, u);
    case Interp::EaseOut: return a + (b - a) * applyEasing(Easing::EaseOut, u);
    case Interp::EaseInOut: return a + (b - a) * applyEasing(Easing::EaseInOut, u);
    }
    return a;
}

double evalTrack(const Track& t, double time) {
    const auto& k = t.keys;
    if (k.empty()) return 0;
    if (time <= k.front().time) return k.front().value;
    if (time >= k.back().time) return k.back().value;
    size_t i = 0;
    while (i + 1 < k.size() && k[i + 1].time <= time) ++i;                 // k[i].time <= time < k[i+1].time
    const double span = k[i + 1].time - k[i].time;
    const double u = span > 0 ? (time - k[i].time) / span : 1.0;
    return interpolate(k[i].interp, k[i].value, k[i + 1].value, u);
}

void sortKeys(Track& t) {
    std::stable_sort(t.keys.begin(), t.keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
}

int findKey(const Track& t, double time, double eps) {
    for (size_t i = 0; i < t.keys.size(); ++i) if (std::abs(t.keys[i].time - time) <= eps) return int(i);
    return -1;
}

int setKey(Track& t, double time, double value, Interp interp) {
    const int at = findKey(t, time);
    if (at >= 0) { t.keys[at].value = value; t.keys[at].interp = interp; return at; }
    t.keys.push_back({time, value, interp});
    sortKeys(t);
    return findKey(t, time, 1e-9);
}

void removeKey(Track& t, int index) {
    if (index >= 0 && index < int(t.keys.size())) t.keys.erase(t.keys.begin() + index);
}

} // namespace lc
