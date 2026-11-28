#pragma once
#include "live/Ndi.h"
#include "core/I18n.h"
#include <atomic>
#include <map>
#include <mutex>
#include <thread>

namespace lc {

// Backend NDI GIA, trong cung tien trinh: mang NDI mo phong de test (khong can NDI that) va de demo (LUMACAST_NDI_BACKEND=fake).
//  - addSource(): "may phat" tong hop (hinh chuyen dong + tieng sin) chay thread rieng theo dong ho thuc, co the chinh lech dong ho am thanh (ppm).
//  - createSender(): NDI ra cung phat vao cung "mang" nen mot receiver trong tien trinh nhan lai duoc (kiem chung NDI ra den NDI vao).
class FakeNdiBackend : public NdiBackend {
public:
    struct SenderCfg {
        int w = 320, h = 180;
        double fps = 30;
        bool audio = true;
        double audioHz = 440;
        double audioPpm = 0;            // dong ho am thanh cua nguoi gui nhanh/cham so voi 48 kHz danh dinh (vd +2000 = nhanh 0,2%)
        int audioBlock = 480;           // so mau moi goi
    };
    struct SentStats {
        std::atomic<uint64_t> videoFrames{0}, audioFrames{0};
        std::atomic<int> lastW{0}, lastH{0};
        std::atomic<int> fpsNum{0}, fpsDen{0};
        std::atomic<int> audioRate{0}, audioChannels{0};
    };

    FakeNdiBackend();
    ~FakeNdiBackend() override;
    QString id() const override { return "fake"; }
    bool available() const override { return !unavailable_; }
    QString unavailableReason() const override { return unavailable_ ? LC_TR("Backend NDI giả: đang giả lập chưa cài NDI Runtime") : QString(); }
    std::vector<NdiSourceInfo> listSources(int waitMs) override;
    LiveOpenResult openReceiver(const QString& name, const QString& url) override;
    std::unique_ptr<NdiSender> createSender(const QString& name, QString* err) override;

    // ---- dieu khien "mang" (test) ----
    void addSource(const QString& name) { addSource(name, SenderCfg()); }
    void addSource(const QString& name, SenderCfg cfg);
    void removeSource(const QString& name);                 // nguoi gui bien mat khoi mang (receiver dang noi se het du lieu)
    void setSourceAlive(const QString& name, bool alive);   // tam dung/tiep tuc phat (mat/co lai tin hieu) ma khong go khoi danh sach
    void setSourceAudioPpm(const QString& name, double ppm);
    void setUnavailable(bool u) { unavailable_ = u; }
    bool hasSource(const QString& name) const;
    // Thong ke cua nguoi gui tao bang createSender() (NDI ra cua LumaCast); nullptr neu khong co.
    std::shared_ptr<SentStats> senderStats(const QString& name) const;
    // Khung cuoi cung ma nguoi gui `name` (NDI ra) da gui: RGBA (rong neu chua co).
    bool lastSentFrame(const QString& name, LiveVideoFrame& out) const;
    size_t receiverCount(const QString& name) const;

    struct Net;
private:
    std::shared_ptr<Net> net_;
    std::atomic<bool> unavailable_{false};
};

} // namespace lc
