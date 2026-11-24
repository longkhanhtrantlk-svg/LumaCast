#pragma once
#include "audio/AudioDrift.h"
#include "live/LiveInput.h"
#include "media/Source.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace lc {

enum class LiveState { Connecting, Live, Lost, Unavailable };
const char* liveStateId(LiveState s);        // "connecting" | "live" | "lost" | "unavailable"

struct LiveOptions {
    bool audio = false;                 // nguon co the co am thanh (NDI): lap AudioStream + bu troi
    int stallMs = 2000;                 // khong co hinh lan tieng qua ngan nay -> coi la mat nguon
    int retryMs = 500;                  // cho truoc lan noi lai dau; tang gap doi toi retryMaxMs
    int retryMaxMs = 2000;
    int unavailableRetryMs = 3000;      // 'khong the lam duoc tren may nay' (thieu runtime...): thu lai thua hon
    double audioTargetMs = 60;          // khoang dem am thanh muc tieu (DriftResampler)
};

// Nguon truc tiep: KHONG co dong ho file, hinh/tieng troi doc lap theo dong ho nguoi gui.
//  - Mot thread rieng giu ket noi: mo (LiveInputFactory) -> doc hinh/tieng -> mat thi giu khung cuoi, bao trang thai, noi lai (backoff). Render thread
//    chi doc khung moi nhat (acquire) nen KHONG BAO GIO bi chan boi mang/thiet bi.
//  - Chi giu MOT khung moi nhat (khong hang cho): tre hinh <= 1 khung render + do tre cua chinh nguon.
//  - Am thanh di qua DriftResampler vao AudioStream (mixer rut nhu moi nguon khac).
class LiveSource : public Source {
public:
    LiveSource(QString name, QString kind, LiveInputFactory factory, LiveOptions opts = {});
    ~LiveSource() override;
    QString name() const override { return name_; }
    TexRef acquire(GL& gl) override;
    QImage snapshot(int maxWidth = 160) const override;
    AudioStream* audio() override { return hasAudio_ ? &audio_ : nullptr; }

    QString kind() const { return kind_; }                // "ndi" | "camera"
    LiveState state() const { return state_; }
    QString statusText() const;                           // tieng Viet, cho UI
    void reconnect();                                     // yeu cau dong va mo lai ngay
    bool isLive() const { return state_ == LiveState::Live; }

    struct Stats {
        uint64_t videoFrames = 0, audioBlocks = 0, droppedVideo = 0, reconnects = 0, audioResyncs = 0;
        double audioFillMs = 0, audioRatioPpm = 0;
        double latencyMs = 0;            // tu luc khung toi LiveSource den luc duoc nap len GPU (lan acquire gan nhat co khung moi)
        double frameAgeMs = 0;           // tuoi cua khung moi nhat
        int width = 0, height = 0;
        double fps = 0;                  // fps thuc nhan duoc (loc)
    };
    Stats stats() const;

private:
    struct Frame {
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        uint64_t seq = 0;
        int64_t arriveNs = 0;
    };
    void run();
    void setStatus(LiveState s, const QString& msg);
    void closeInput();

    QString name_, kind_;
    LiveInputFactory factory_;
    LiveOptions opts_;
    std::atomic<bool> stop_{false}, reconnectReq_{false};
    std::thread thread_;

    mutable std::mutex m_;
    std::shared_ptr<Frame> latest_;
    QString status_;
    uint64_t uploaded_ = 0;
    GlTexture tex_;

    std::atomic<LiveState> state_{LiveState::Connecting};
    AudioStream audio_{kAudioRate};
    std::atomic<bool> hasAudio_{false};
    std::atomic<uint64_t> videoFrames_{0}, audioBlocks_{0}, droppedVideo_{0}, reconnects_{0}, audioResyncs_{0};
    std::atomic<double> latencyMs_{0}, fillMs_{0}, ratioPpm_{0}, fps_{0};
    std::atomic<int64_t> lastFrameNs_{0};
    std::atomic<int> w_{0}, h_{0};
};

} // namespace lc
