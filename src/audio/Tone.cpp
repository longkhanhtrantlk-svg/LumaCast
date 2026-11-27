#include "Tone.h"
#include <algorithm>
#include <cmath>

namespace lc {

namespace {
constexpr double kTwoPi = 6.283185307179586;
constexpr double kTickSeconds = 0.09, kFastSeconds = 0.08, kBellSeconds = 2.2;

// Bao mem hai dau de khong "lach".
double edges(double t, double len, double attack, double release) {
    return std::clamp(std::min(t / attack, (len - t) / release), 0.0, 1.0);
}
double wave(double hz, double t) { return std::sin(kTwoPi * hz * t); }
} // namespace

int toneFrames(ToneKind kind) {
    const double s = kind == ToneKind::Tick ? kTickSeconds : kind == ToneKind::Fast ? kFastSeconds : kBellSeconds;
    return int(s * kAudioRate);
}

float toneSample(ToneKind kind, int frame) {
    if (frame < 0 || frame >= toneFrames(kind)) return 0.0f;
    const double t = double(frame) / kAudioRate;
    switch (kind) {
    case ToneKind::Tick: return float(wave(880.0, t) * edges(t, kTickSeconds, 0.004, 0.02));
    case ToneKind::Fast: return float(wave(1318.5, t) * edges(t, kFastSeconds, 0.003, 0.015));
    case ToneKind::Bell: {
        const double v = 0.60 * wave(988.0, t) + 0.25 * wave(1976.0, t) + 0.15 * wave(2974.0, t);
        return float(v * std::exp(-1.6 * t) * edges(t, kBellSeconds, 0.003, 0.3));
    }
    }
    return 0.0f;
}

} // namespace lc
