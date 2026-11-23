#pragma once
#include "core/Types.h"
#include "media/Source.h"
#include <QString>
#include <QVariantMap>
#include <algorithm>
#include <memory>

namespace lc {

enum class Easing { Linear, EaseInOut, EaseIn, EaseOut };

inline const char* easingName(Easing e) {
    switch (e) {
    case Easing::Linear: return "Tuyến tính";
    case Easing::EaseInOut: return "Êm hai đầu";
    case Easing::EaseIn: return "Êm lúc đầu";
    case Easing::EaseOut: return "Êm lúc cuối";
    }
    return "?";
}

inline double applyEasing(Easing e, double t) {
    t = std::clamp(t, 0.0, 1.0);
    switch (e) {
    case Easing::Linear: return t;
    case Easing::EaseInOut: return t * t * (3 - 2 * t);
    case Easing::EaseIn: return t * t;
    case Easing::EaseOut: return 1 - (1 - t) * (1 - t);
    }
    return t;
}

// Mo ta day du mot lan chuyen: loai co san (Fade, Dip, Wipe...), hoac Plugin (dung san: push/slide/zoom/luma, hoac plug-in nguoi dung
// theo pluginId + params), hoac Stinger (video alpha chay de len, cat canh o diem cutPoint).
struct TransitionSpec {
    TransitionType type = TransitionType::Fade;
    QString pluginId;                         // khi type == Plugin
    QVariantMap params;
    Easing easing = Easing::EaseInOut;
    double duration = 1.0;                    // giay (Stinger: do dai video)
    std::shared_ptr<Source> stinger;          // khi type == Stinger
    double cutPoint = 0.5;                    // 0..1: luc doi canh duoi stinger
};

} // namespace lc
