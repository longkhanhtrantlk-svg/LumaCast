#pragma once
#include "audio/AudioStream.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace lc {

// Bu troi dong ho am thanh cho nguon TRUC TIEP (NDI/camera). Nguon di theo dong ho cua nguoi gui, mixer lay theo dong ho thiet bi ra;
// hai dong ho lech nhau vai chuc-tram ppm nen ring se can dan (mat tieng) hoac day dan (tre tang mai). Lop nay:
//  - doi tan so vao -> 48 kHz bang noi suy tuyen tinh voi ti le BIEN THIEN CHAM (+/- toi da 0,5%),
//  - dieu khien ti le bang bo PI theo muc day cua ring (muc tieu targetMs), nen ring on dinh quanh muc tieu,
//  - mat dong bo (can/tran) thi dat lai cung: bom im lang de dem lai, hoac bo bot du lieu, va dem so lan (resyncs).
// Goi tu MOT thread (thread nhan). Khong cap phat tren duong nong ngoai vector tam tai su dung.
class DriftResampler {
public:
    explicit DriftResampler(AudioStream& out, double targetMs = 60.0) : out_(out), targetMs_(targetMs) {}

    // in: `frames` khung, `channels` kenh xen ke (1 = mono nhan doi, >2 = lay 2 kenh dau), tan so `rate`.
    void process(const float* in, size_t frames, int rate, int channels) {
        if (frames == 0 || rate <= 0 || channels <= 0) return;
        const size_t target = size_t(targetMs_ * kAudioRate / 1000.0);
        // Khong ai rut (mixer chua chay / nguon khong nam trong Scene): chi giu ~target moi nhat, khong tich tru.
        if (!out_.consumerActive()) {
            if (out_.queued() > target) out_.flush(0);
            reset(false);
        }
        stereo_.resize(frames * 2);
        for (size_t i = 0; i < frames; ++i) {
            const float l = in[i * size_t(channels)];
            stereo_[i * 2] = l;
            stereo_[i * 2 + 1] = channels > 1 ? in[i * size_t(channels) + 1] : l;
        }
        const size_t fill = out_.queued();
        if (!primed_ || fill == 0) {                      // dau luong / vua can: dem lai khoang dem muc tieu (bang im lang)
            if (primed_) ++resyncs_;                      // can giua chung = mot lan mat dong bo (nghe thay mot cai "vap")
            const size_t pad = target > fill ? target - fill : 0;
            if (pad > 0) { zeros_.assign(pad * 2, 0.0f); out_.push(zeros_.data(), pad); }
            primed_ = true;
            integ_ = std::clamp(integ_, -0.002, 0.002);
        } else {
            // loc muc day de bo qua nhap nho cua goi mang/khoi mixer
            fillAvg_ += kAvg * (double(fill) - fillAvg_);
            const double err = (fillAvg_ - double(target)) / kAudioRate;         // giay; duong = ring dang day hon muc tieu
            const double dt = double(frames) / rate;
            integ_ = std::clamp(integ_ + kKi * err * dt, -kMaxEps, kMaxEps);
            const double target_eps = std::clamp(kKp * err + integ_, -kMaxEps, kMaxEps);
            eps_ += kSlew * (target_eps - eps_);                                    // doi ti le tu tu: khong 'run' cao do
        }
        if (!primed0_) { fillAvg_ = double(out_.queued()); primed0_ = true; }

        // noi suy: buoc `step` khung vao cho moi khung ra; step > 1 -> it khung ra hon (ring dang day)
        epsAvg_ += 0.02 * (eps_ - epsAvg_);
        const double step = (double(rate) / kAudioRate) * (1.0 + eps_);
        const size_t n = frames;
        outBuf_.clear();
        outBuf_.reserve(size_t(double(n) / step) + 8);
        auto at = [&](long i, int c) { return i < 0 ? prev_[c] : stereo_[size_t(i) * 2 + size_t(c)]; };
        while (pos_ < double(n) - 1.0) {
            const long i0 = long(std::floor(pos_));
            const float f = float(pos_ - double(i0));
            for (int c = 0; c < 2; ++c) outBuf_.push_back(at(i0, c) * (1.0f - f) + at(i0 + 1, c) * f);
            pos_ += step;
        }
        pos_ -= double(n);
        prev_[0] = stereo_[(n - 1) * 2];
        prev_[1] = stereo_[(n - 1) * 2 + 1];

        const size_t outFrames = outBuf_.size() / 2;
        const size_t room = out_.writable();
        size_t offset = 0, take = outFrames;
        if (outFrames > room) {                           // tran (mixer dung han / khong ai rut): bo phan thua, cu giu du lieu MOI NHAT
            offset = outFrames - room;
            take = room;
            ++resyncs_;
        }
        out_.push(outBuf_.data() + offset * 2, take);
        // ring qua day so voi muc tieu (vd. tre tich luy sau khi mixer ngung roi chay lai): bo bot ve gan muc tieu, khong de tre tang mai
        if (out_.queued() > target * 3) {
            out_.flush(0);
            ++resyncs_;
            primed_ = false;
        }
    }

    void reset(bool full = true) {
        primed_ = false;
        pos_ = 0;
        prev_[0] = prev_[1] = 0;
        fillAvg_ = 0;
        primed0_ = false;
        if (full) { integ_ = 0; eps_ = 0; epsAvg_ = 0; }
    }

    double ratioPpm() const { return epsAvg_ * 1e6; }               // lech ti le (ppm, loc ~1 s); duong = nguon nhanh hon thiet bi, dang bo bot khung
    double fillMs() const { return fillAvg_ * 1000.0 / kAudioRate; }
    uint64_t resyncs() const { return resyncs_; }
    double targetMs() const { return targetMs_; }

private:
    static constexpr double kMaxEps = 0.005;       // toi da 0,5% (khoang 8,6 cent): khong nghe thay doi cao do
    static constexpr double kKp = 0.3, kKi = 0.0225;   // tat dan toi han (omega = 0.15 rad/s): hoi tu trong ~30 s
    static constexpr double kAvg = 0.01, kSlew = 0.1;
    AudioStream& out_;
    double targetMs_;
    std::vector<float> stereo_, outBuf_, zeros_;
    float prev_[2] = {0, 0};
    double pos_ = 0, eps_ = 0, epsAvg_ = 0, integ_ = 0, fillAvg_ = 0;
    bool primed_ = false, primed0_ = false;
    uint64_t resyncs_ = 0;
};

} // namespace lc
