#pragma once
#include "audio/AudioDevice.h"
#include "audio/AudioStream.h"
#include "media/Source.h"
#include <QString>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace lc {

// Mot muc cua ke hoach tron: nguon -> bus cua Channel, voi he so GIA TRI DICH (da gom am luong Channel/Layer, mute, trong so transition).
struct AudioPlanEntry {
    std::shared_ptr<Source> src;
    int channel = 0;
    float gain = 0;
};

// Ke hoach tron cho mot khoang thoi gian: render thread tao (moi khung hinh khi co thay doi), mixer doc.
struct AudioPlan {
    int channels = 0;
    std::vector<AudioPlanEntry> entries;
    bool sameAs(const AudioPlan& o) const;
};

// Do muc cua mot bus (cap nhat boi mixer, doc boi UI tu thread khac). Gia tri tuyen tinh 0..1+ (dBFS = 20*log10).
struct AudioMeter {
    std::atomic<float> peak[2];     // gia tri dinh co giu & roi cham
    std::atomic<float> rms[2];
    AudioMeter() { for (int i = 0; i < 2; ++i) { peak[i] = 0; rms[i] = 0; } }
};

constexpr int kMaxAudioChannels = 32;
constexpr int kMixBlock = 480;              // 10 ms @ 48 kHz

// Mixer: moi Channel mot bus stereo, cong vao bus "master" ra thiet bi.
//  - render thread goi setPlan();
//  - thread mixer rieng dinh ky tron truoc khoang `targetLatencyMs` roi day vao ring dau ra (lock-free);
//  - callback cua thiet bi chi rut tu ring (khong khoa, khong cap phat).
// Moi nguon trong ke hoach duoc rut du lieu DUNG MOT LAN moi khoi (ke ca khi he so = 0) de dong ho am thanh cua no chay deu.
class AudioMixer {
public:
    AudioMixer();
    ~AudioMixer();

    // Tap bus cua Channel (NDI ra...): moi khoi tron xong, bus cua `ch` duoc day vao `tap` (ring lock-free; day thi bo phan thua). nullptr = go tap.
    // Goi tu thread bat ky; mixer khoa mutex ngan moi khoi (khong phai duong callback thiet bi).
    void setTap(int ch, std::shared_ptr<AudioStream> tap);

    // Render thread.
    void setPlan(std::shared_ptr<const AudioPlan> plan);

    // Mo thiet bi (id rong = mac dinh) va chay thread mixer. Neu da chay thi dong truoc. Luon thanh cong (roi ve Null neu thiet bi hong).
    bool start(const QString& deviceId, QString* err = nullptr);
    void stop();
    void releasePlan();                    // gia lai cac nguon dang giu (chi goi khi da stop()): engine dung truoc khi huy project
    bool running() const { return running_; }
    QString deviceName() const;
    QString deviceId() const { return deviceId_; }
    void setTargetLatencyMs(int ms) { targetMs_ = std::max(20, std::min(500, ms)); }

    // Tron `frames` khung (<= 8192) vao `out` (stereo xen ke) va cap nhat meter. Thread mixer goi; test goi truc tiep (khong can thread/thiet bi).
    void renderBlock(float* out, int frames);

    const AudioMeter& channelMeter(int ch) const { return meters_[size_t(std::max(0, std::min(ch, kMaxAudioChannels - 1)))]; }
    const AudioMeter& masterMeter() const { return master_; }
    uint64_t outputUnderruns() const { return outUnderruns_; }
    uint64_t clippedBlocks() const { return clipped_; }
    uint64_t blocksRendered() const { return blocks_; }
    // Do tre uoc tinh tu luc mixer lay mau den luc nghe (ring dau ra + bo dem thiet bi), giay. Dat thu cong cho test.
    double outputLatency() const { return latency_; }
    void setOutputLatency(double s) { latency_ = s; }

private:
    struct Scr { AudioStream* st = nullptr; std::vector<float> buf; };
    struct Key { const Source* src; int ch; float gain; };
    void threadMain();
    void adoptPlan();

    // plan (render thread -> mixer)
    std::mutex planMu_;
    std::shared_ptr<const AudioPlan> pending_;
    std::atomic<bool> dirty_{false};
    // chi mixer
    std::shared_ptr<const AudioPlan> plan_;
    std::vector<Key> prev_;
    std::vector<Scr> scr_;
    std::vector<std::vector<float>> bus_;
    std::vector<Key> now_;

    std::mutex tapMu_;
    std::array<std::shared_ptr<AudioStream>, kMaxAudioChannels> taps_;
    std::atomic<int> tapCount_{0};

    std::array<AudioMeter, kMaxAudioChannels> meters_;
    AudioMeter master_;
    std::atomic<uint64_t> outUnderruns_{0}, clipped_{0}, blocks_{0};
    std::atomic<double> latency_{0};

    std::unique_ptr<AudioDevice> dev_;
    AudioStream out_{kAudioRate / 2};
    std::thread th_;
    std::atomic<bool> stop_{false}, running_{false};
    std::atomic<int> targetMs_{40};
    QString deviceId_;
};

} // namespace lc
