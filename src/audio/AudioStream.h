#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

namespace lc {

// Dinh dang chung cua moi bus am thanh: float32, xen ke (interleaved), stereo, 48 kHz.
constexpr int kAudioRate = 48000;
constexpr int kAudioChannels = 2;

inline int64_t audioSteadyNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Mot luong PCM cua mot nguon: ring buffer lock-free mot-nguoi-ghi (thread giai ma) / mot-nguoi-doc (mixer).
// Khong cap phat, khong khoa mutex tren duong pull() cua mixer. Chi so doc/ghi la so dem don dieu 64-bit (khong quan).
//
// Dong ho am thanh: vi tri (giay, tren "dong thoi gian lien tuc" cua nguon) cua mau tiep theo se duoc mixer lay.
// Luong giai ma dung no lam dong ho chu cho hinh (video theo audio). flush() dat lai goc dong thoi gian.
class AudioStream {
public:
    explicit AudioStream(size_t capacityFrames = kAudioRate * 2)
        : cap_(capacityFrames), buf_(capacityFrames * kAudioChannels, 0.0f) {}

    // ---- phia ghi (thread giai ma) ----
    size_t capacity() const { return cap_; }
    size_t queued() const { return size_t(w_.load(std::memory_order_acquire) - rEff()); }
    size_t writable() const { return cap_ - queued(); }
    // Ghi toi da `frames` khung (stereo xen ke); tra ve so khung thuc su nhan.
    size_t push(const float* src, size_t frames) {
        const uint64_t w = w_.load(std::memory_order_relaxed);
        const size_t n = std::min(frames, cap_ - size_t(w - rEff()));
        for (size_t i = 0; i < n; ++i) {
            const size_t p = size_t((w + i) % cap_) * kAudioChannels;
            buf_[p] = src[i * 2];
            buf_[p + 1] = src[i * 2 + 1];
        }
        w_.store(w + n, std::memory_order_release);
        return n;
    }
    // Bo het du lieu dang cho; mau ke tiep duoc ghi se o vi tri `startSec` tren dong thoi gian. Cung bo co "giu im lang" do restart().
    void flush(double startSec) {
        flushIdx_.store(w_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        base_.store(startSec, std::memory_order_relaxed);
        epoch_.fetch_add(1, std::memory_order_release);
        hold_.store(false, std::memory_order_release);
    }
    // Yeu cau tu thread bat ky (render thread khi Source::restart()): im lang ngay cho den khi thread giai ma flush().
    void holdUntilFlush() { hold_.store(true, std::memory_order_release); }

    // ---- phia doc (mixer) ----
    // Lay `frames` khung; thieu thi dien 0 (underrun). Tra ve so khung THAT. Thread mixer duy nhat goi.
    size_t pull(float* dst, size_t frames) {
        lastPullNs_.store(audioSteadyNs(), std::memory_order_relaxed);
        syncEpoch();
        if (hold_.load(std::memory_order_acquire)) {                  // dang cho restart: bo du lieu cu, im lang
            r_.store(w_.load(std::memory_order_acquire), std::memory_order_release);
            std::fill(dst, dst + frames * kAudioChannels, 0.0f);
            return 0;
        }
        const uint64_t r = r_.load(std::memory_order_relaxed);
        const size_t n = std::min(frames, size_t(w_.load(std::memory_order_acquire) - r));
        for (size_t i = 0; i < n; ++i) {
            const size_t p = size_t((r + i) % cap_) * kAudioChannels;
            dst[i * 2] = buf_[p];
            dst[i * 2 + 1] = buf_[p + 1];
        }
        std::fill(dst + n * kAudioChannels, dst + frames * kAudioChannels, 0.0f);
        r_.store(r + n, std::memory_order_release);
        if (n < frames) underruns_.fetch_add(1, std::memory_order_relaxed);
        return n;
    }
    void setOutputLatency(double sec) { latency_.store(sec, std::memory_order_relaxed); }
    uint64_t underruns() const { return underruns_.load(std::memory_order_relaxed); }

    // ---- dong ho ----
    // Vi tri (s) cua mau tiep theo se vao mixer.
    double position() const {
        const uint64_t fi = flushIdx_.load(std::memory_order_acquire);
        return base_.load(std::memory_order_relaxed) + double(std::max(r_.load(std::memory_order_acquire), fi) - fi) / kAudioRate;
    }
    // Vi tri dang DUOC NGHE: tru do tre ring dau ra + bo dem thiet bi.
    double heardPosition() const { return position() - latency_.load(std::memory_order_relaxed); }
    // Mixer co dang lay du lieu khong (trong `ms` mili giay gan day)? Khong thi nguon chay theo dong ho tuong.
    bool consumerActive(int ms = 250) const {
        const int64_t t = lastPullNs_.load(std::memory_order_relaxed);
        return t != 0 && audioSteadyNs() - t < int64_t(ms) * 1000000;
    }

private:
    uint64_t rEff() const { return std::max(r_.load(std::memory_order_acquire), flushIdx_.load(std::memory_order_acquire)); }
    void syncEpoch() {
        const uint64_t e = epoch_.load(std::memory_order_acquire);
        if (e == seenEpoch_) return;
        seenEpoch_ = e;
        const uint64_t fi = flushIdx_.load(std::memory_order_acquire);
        if (r_.load(std::memory_order_relaxed) < fi) r_.store(fi, std::memory_order_release);
    }

    const size_t cap_;
    std::vector<float> buf_;
    std::atomic<uint64_t> w_{0}, r_{0};
    std::atomic<uint64_t> flushIdx_{0}, epoch_{0};
    uint64_t seenEpoch_ = 0;                         // chi mixer dung
    std::atomic<double> base_{0}, latency_{0};
    std::atomic<bool> hold_{false};
    std::atomic<int64_t> lastPullNs_{0};
    std::atomic<uint64_t> underruns_{0};
};

} // namespace lc
