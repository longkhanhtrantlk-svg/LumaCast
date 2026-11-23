#pragma once
#include <QString>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <vector>

namespace lc {

// Thuoc tinh Layer co the hoat hoa theo thoi gian (keyframe).
enum class AnimProp { X, Y, W, H, Opacity, Rotation };
constexpr int kAnimPropCount = 6;

// Cach noi tu mot keyframe den keyframe KE TIEP.
enum class Interp { Linear, EaseIn, EaseOut, EaseInOut, Hold };
constexpr int kInterpCount = 5;

struct Keyframe {
    double time = 0;                 // giay tren dong ho Scene
    double value = 0;
    Interp interp = Interp::Linear;  // doan [key nay, key ke tiep]
};

struct Track {
    AnimProp prop = AnimProp::X;
    std::vector<Keyframe> keys;      // luon sap xep tang dan theo time
};

const char* animPropId(AnimProp p);                  // "x","y","w","h","opacity","rotation"
QString animPropLabel(AnimProp p);                   // nhan tieng Viet
bool animPropFromId(const QString& id, AnimProp& out);
const char* interpId(Interp i);                      // "linear","easein","easeout","easeinout","hold"
QString interpLabel(Interp i);
bool interpFromId(const QString& id, Interp& out);

double interpolate(Interp i, double a, double b, double u);       // u in [0,1]
// Gia tri track tai thoi diem t: truoc key dau = key dau; sau key cuoi = key cuoi; giua = noi theo interp cua key truoc.
double evalTrack(const Track& t, double time);
void sortKeys(Track& t);
int findKey(const Track& t, double time, double eps = 1e-3);       // chi so key co time ~ `time`, hoac -1
int setKey(Track& t, double time, double value, Interp interp);   // them hoac ghi de key o `time`; tra ve chi so sau khi sap xep
void removeKey(Track& t, int index);

// Dong ho cua Scene (phat/tam dung/lap). tick() goi tren render thread moi khung; cac truong atomic de UI doc.
struct SceneClock {
    std::atomic<double> time{0};
    std::atomic<bool> playing{false};
    double lastNow = -1;                             // chi render thread dung

    SceneClock() = default;
    SceneClock(const SceneClock& o) : time(o.time.load()), playing(o.playing.load()), lastNow(o.lastNow) {}
    SceneClock& operator=(const SceneClock& o) { time = o.time.load(); playing = o.playing.load(); lastNow = o.lastNow; return *this; }

    void tick(double now, double duration, bool loop) {
        if (!playing || lastNow < 0) { lastNow = now; return; }       // vua bat dau / dang dung: khong nhay thoi gian
        const double dt = std::max(0.0, now - lastNow);
        lastNow = now;
        double t = time + dt;
        if (duration > 0 && t >= duration) {
            if (loop) t = std::fmod(t, duration);
            else { t = duration; playing = false; }
        }
        time = t;
    }
    void play(double now, double duration) { if (duration > 0 && time >= duration) time = 0; playing = true; lastNow = now; }
    void pause() { playing = false; }
    void stop() { playing = false; time = 0; }
    void seek(double t) { time = t < 0 ? 0 : t; }
    void restart(double now) { time = 0; playing = true; lastNow = now; }   // now < 0: bat dau o lan tick ke tiep
};

} // namespace lc
