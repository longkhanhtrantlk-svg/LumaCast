#include "AudioMixer.h"
#include <QDebug>
#include <cmath>
#include <chrono>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>
#endif

namespace lc {

bool AudioPlan::sameAs(const AudioPlan& o) const {
    if (channels != o.channels || entries.size() != o.entries.size() || channelGain.size() != o.channelGain.size()) return false;
    for (size_t i = 0; i < channelGain.size(); ++i)
        if (std::abs(channelGain[i] - o.channelGain[i]) > 1e-4f) return false;
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto &a = entries[i], &b = o.entries[i];
        if (a.src != b.src || a.channel != b.channel || std::abs(a.gain - b.gain) > 1e-4f) return false;
    }
    return true;
}

AudioMixer::AudioMixer() = default;
AudioMixer::~AudioMixer() { stop(); }

void AudioMixer::setPlan(std::shared_ptr<const AudioPlan> plan) {
    std::lock_guard lk(planMu_);
    pending_ = std::move(plan);
    dirty_ = true;
}

void AudioMixer::setTap(int ch, std::shared_ptr<AudioStream> tap) {
    if (ch < 0 || ch >= kMaxAudioChannels) return;
    std::lock_guard lk(tapMu_);
    taps_[size_t(ch)] = std::move(tap);
    int n = 0;
    for (const auto& t : taps_) n += t != nullptr;
    tapCount_ = n;
}

QString AudioMixer::deviceName() const { return dev_ ? dev_->name() : QString(); }

void AudioMixer::adoptPlan() {
    if (!dirty_.load(std::memory_order_acquire)) return;
    std::shared_ptr<const AudioPlan> p;
    {
        std::lock_guard lk(planMu_);
        p = pending_;
        dirty_ = false;
    }
    if (p == plan_) return;
    // nho he so da ap dung cuoi cung de doan tang/giam muot (khong click) khi ke hoach doi
    if (plan_) {
        prev_.clear();
        for (const auto& e : plan_->entries) prev_.push_back({e.src.get(), e.channel, e.gain});
    }
    plan_ = std::move(p);
}

namespace {
constexpr int kToneReleaseFrames = kAudioRate / 200;        // 5 ms
constexpr size_t kMaxVoices = 64;
constexpr float kToneLevel = 0.6f;                          // chua cho cho tieng chuong trinh
}

void AudioMixer::playTone(int channel, int id, ToneKind kind, float volume, double delaySeconds) {
    std::lock_guard lk(toneMu_);
    if (toneCmds_.size() >= 256) toneCmds_.erase(toneCmds_.begin());
    toneCmds_.push_back({false, channel, id, kind, std::clamp(volume, 0.0f, 1.0f), int(std::max(0.0, delaySeconds) * kAudioRate)});
    toneDirty_ = true;
}

void AudioMixer::stopTones(int channel, int id) {
    std::lock_guard lk(toneMu_);
    if (toneCmds_.size() >= 256) toneCmds_.erase(toneCmds_.begin());
    toneCmds_.push_back({true, channel, id, ToneKind::Tick, 0.0f, 0});
    toneDirty_ = true;
}

void AudioMixer::adoptTones() {
    if (!toneDirty_.load(std::memory_order_acquire)) return;
    std::lock_guard lk(toneMu_);
    for (const ToneCmd& c : toneCmds_) {
        if (!c.stop) {
            if (voices_.size() >= kMaxVoices) voices_.erase(voices_.begin());
            voices_.push_back({c.channel, c.id, c.kind, c.volume, -c.delay, -1, -1.0f});
            continue;
        }
        for (Voice& v : voices_)
            if (v.channel == c.channel && v.id == c.id && v.release < 0) v.release = v.pos < 0 ? 0 : kToneReleaseFrames;
    }
    toneCmds_.clear();
    toneDirty_ = false;
}

// Cong tieng tu tong hop vao bus cua Channel; he so bus (am luong/mute/FTB) doi dan trong khoi.
void AudioMixer::mixTones(int channels, int frames) {
    for (Voice& v : voices_) {
        const float target = plan_ && v.channel >= 0 && size_t(v.channel) < plan_->channelGain.size() ? plan_->channelGain[size_t(v.channel)] : 1.0f;
        const float from = v.gain < 0 ? target : v.gain;
        v.gain = target;
        float* bus = v.channel >= 0 && v.channel < channels ? bus_[size_t(v.channel)].data() : nullptr;
        const int len = toneFrames(v.kind);
        for (int i = 0; i < frames; ++i) {
            if (v.release == 0 || v.pos >= len) break;
            float env = 1.0f;
            if (v.release > 0) env = float(v.release--) / float(kToneReleaseFrames);
            if (bus && v.pos >= 0) {
                const float g = from + (target - from) * float(i + 1) / float(frames);
                const float s = toneSample(v.kind, v.pos) * v.volume * kToneLevel * env * g;
                bus[i * 2] += s;
                bus[i * 2 + 1] += s;
            }
            ++v.pos;
        }
    }
    std::erase_if(voices_, [](const Voice& v) { return v.release == 0 || v.pos >= toneFrames(v.kind); });
}

void AudioMixer::renderBlock(float* out, int frames) {
    adoptPlan();
    adoptTones();
    int done = 0;
    while (done < frames) {
        const int n = std::min(frames - done, kMixBlock * 4);
        float* o = out + size_t(done) * kAudioChannels;
        std::fill(o, o + size_t(n) * kAudioChannels, 0.0f);
        const AudioPlan* plan = plan_.get();
        const int nch = plan ? std::min(plan->channels, kMaxAudioChannels) : 0;
        if (int(bus_.size()) < nch) bus_.resize(size_t(nch));
        for (int c = 0; c < nch; ++c) {
            bus_[size_t(c)].assign(size_t(n) * kAudioChannels, 0.0f);
        }
        const double lat = latency_;
        size_t used = 0;
        if (plan) {
            // 1) rut du lieu moi nguon duy nhat mot lan
            for (const auto& e : plan->entries) {
                AudioStream* st = e.src ? e.src->audio() : nullptr;
                if (!st) continue;
                bool seen = false;
                for (size_t k = 0; k < used; ++k) if (scr_[k].st == st) { seen = true; break; }
                if (seen) continue;
                if (used == scr_.size()) scr_.emplace_back();
                Scr& s = scr_[used++];
                s.st = st;
                if (s.buf.size() < size_t(n) * kAudioChannels) s.buf.resize(size_t(kMixBlock) * 4 * kAudioChannels);
                st->setOutputLatency(lat);
                st->pull(s.buf.data(), size_t(n));
            }
            // 2) cong vao bus cua tung Channel, he so doi dan tuyen tinh trong khoi (khong tieng "lach")
            std::vector<Key>& now = now_;
            now.clear();
            for (const auto& e : plan->entries) {
                now.push_back({e.src.get(), e.channel, e.gain});
                if (e.channel < 0 || e.channel >= nch) continue;
                AudioStream* st = e.src ? e.src->audio() : nullptr;
                if (!st) continue;
                const float* buf = nullptr;
                for (size_t k = 0; k < used; ++k) if (scr_[k].st == st) { buf = scr_[k].buf.data(); break; }
                if (!buf) continue;
                float g0 = 0;
                for (const Key& p : prev_) if (p.src == e.src.get() && p.ch == e.channel) { g0 = p.gain; break; }
                const float g1 = e.gain;
                if (g0 == 0.0f && g1 == 0.0f) continue;
                float* bus = bus_[size_t(e.channel)].data();
                if (g0 == g1) {
                    for (int i = 0; i < n * 2; ++i) bus[i] += buf[i] * g1;
                } else {
                    const float step = (g1 - g0) / float(n);
                    for (int i = 0; i < n; ++i) {
                        const float g = g0 + step * float(i + 1);
                        bus[i * 2] += buf[i * 2] * g;
                        bus[i * 2 + 1] += buf[i * 2 + 1] * g;
                    }
                }
            }
            prev_.assign(now.begin(), now.end());
        }
        if (!voices_.empty()) mixTones(nch, n);       // tieng nhac: di cung bus (ca NDI ra, meter, master)
        // 2b) tap bus (NDI ra): chi khi co tap nao
        if (tapCount_.load(std::memory_order_relaxed) > 0) {
            std::lock_guard lk(tapMu_);
            for (int c = 0; c < nch; ++c)
                if (taps_[size_t(c)]) taps_[size_t(c)]->push(bus_[size_t(c)].data(), size_t(n));
        }
        // 3) meter tung bus + cong vao master
        const float decay = std::exp(-float(n) / float(kAudioRate) / 0.4f);
        bool clip = false;
        for (int c = 0; c < nch; ++c) {
            const float* b = bus_[size_t(c)].data();
            float pk[2] = {0, 0};
            double sq[2] = {0, 0};
            for (int i = 0; i < n; ++i)
                for (int k = 0; k < 2; ++k) {
                    const float v = b[i * 2 + k];
                    pk[k] = std::max(pk[k], std::abs(v));
                    sq[k] += double(v) * v;
                    o[i * 2 + k] += v;
                }
            AudioMeter& m = meters_[size_t(c)];
            for (int k = 0; k < 2; ++k) {
                m.peak[k] = std::max(pk[k], m.peak[k].load() * decay);
                m.rms[k] = float(std::sqrt(sq[k] / n));
            }
        }
        for (int c = nch; c < kMaxAudioChannels; ++c)
            for (int k = 0; k < 2; ++k) { meters_[size_t(c)].peak[k] = 0; meters_[size_t(c)].rms[k] = 0; }
        float pk[2] = {0, 0};
        double sq[2] = {0, 0};
        for (int i = 0; i < n; ++i)
            for (int k = 0; k < 2; ++k) {
                float& v = o[i * 2 + k];
                if (!std::isfinite(v)) v = 0;
                pk[k] = std::max(pk[k], std::abs(v));
                if (v > 1.0f) { v = 1.0f; clip = true; } else if (v < -1.0f) { v = -1.0f; clip = true; }
                sq[k] += double(v) * v;
            }
        for (int k = 0; k < 2; ++k) {
            master_.peak[k] = std::max(pk[k], master_.peak[k].load() * decay);
            master_.rms[k] = float(std::sqrt(sq[k] / n));
        }
        if (clip) ++clipped_;
        ++blocks_;
        done += n;
    }
}

bool AudioMixer::start(const QString& deviceId, QString* err) {
    stop();
    deviceId_ = deviceId;
    dev_ = createAudioDevice(deviceId, err);
    stop_ = false;
    out_.flush(0);
    // do tre ban dau: ring dau ra + bo dem thiet bi
    latency_ = double(targetMs_) / 1000.0 + dev_->latencySeconds();
    {   // nap san de thiet bi khong bi doi ngay luc dau
        std::vector<float> tmp(size_t(kMixBlock) * kAudioChannels);
        for (int i = 0; i < std::max(1, targetMs_ / 10); ++i) { renderBlock(tmp.data(), kMixBlock); out_.push(tmp.data(), kMixBlock); }
    }
    running_ = true;
    th_ = std::thread([this] { threadMain(); });
    QString e;
    auto cb = [this](float* o, int frames) {
        const size_t got = out_.pull(o, size_t(frames));          // lock-free; thieu -> im lang (da dien 0)
        if (got < size_t(frames)) outUnderruns_.fetch_add(1, std::memory_order_relaxed);
    };
    const bool ok = dev_->start(cb, &e);
    if (!ok) {                                                      // thiet bi hong: van chay dong ho am thanh bang backend Null
        qWarning().noquote() << "Âm thanh: thiết bị không khởi động được:" << e << "- dùng Null";
        if (err) *err = e;
        dev_ = std::make_unique<NullAudioDevice>();
        dev_->start(cb, nullptr);
    }
    return ok;
}

void AudioMixer::stop() {
    if (dev_) dev_->stop();
    stop_ = true;
    if (th_.joinable()) th_.join();
    running_ = false;
    dev_.reset();
}

void AudioMixer::releasePlan() {
    {
        std::lock_guard lk(planMu_);
        pending_.reset();
    }
    dirty_ = false;
    plan_.reset();
    prev_.clear();
    {
        std::lock_guard lk(toneMu_);
        toneCmds_.clear();
    }
    toneDirty_ = false;
    voices_.clear();
}

void AudioMixer::threadMain() {
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
    std::vector<float> tmp(size_t(kMixBlock) * kAudioChannels);
    while (!stop_) {
        const size_t target = size_t(targetMs_.load()) * kAudioRate / 1000;
        size_t q = out_.queued();
        int guard = 0;
        while (q < target && !stop_ && guard++ < 50) {
            latency_ = double(q) / kAudioRate + (dev_ ? dev_->latencySeconds() : 0.0);
            renderBlock(tmp.data(), kMixBlock);
            q += out_.push(tmp.data(), kMixBlock);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

} // namespace lc
