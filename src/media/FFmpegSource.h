#pragma once
#include "Hap.h"
#include "Source.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace lc {

// Tuy chon giai ma video (M6). Mac dinh lay tu bien moi truong luc dau tien dung, doi duoc bang setDefault() truoc khi tao nguon.
//  - hwDecode: "auto" (Windows: D3D11VA; nen tang khac: tat), "off", hoac ten thiet bi FFmpeg ("d3d11va", "dxva2", "vaapi", "cuda"...).
//    Khong mo duoc thiet bi/codec hoac loi giua chung -> tu ve phan mem (khong dung hinh).
//  - gpuColor: true = day plane Y/UV len GPU, doi YUV->RGB bang shader (SRC-04); false = sws_scale RGBA tren CPU (duong du phong + doi chieu).
// Bien moi truong: LUMACAST_HWDEC=off|auto|<ten>, LUMACAST_GPU_YUV=0, LUMACAST_HAP=0.
struct VideoDecodeOptions {
    QString hwDecode = "auto";
    bool gpuColor = true;
    bool hapDirect = true;          // HAP: tach DXT va day thang len GPU (SRC-05); false = bo giai ma hap cua FFmpeg (RGBA tren CPU)
    static VideoDecodeOptions fromEnvironment();
    static VideoDecodeOptions defaults();
    static void setDefaults(const VideoDecodeOptions& o);
};

// Giai ma video + am thanh bang FFmpeg tren MOT thread rieng, tu lap lai.
// M1: giai ma + doi YUV->RGBA tren CPU. M6: giai ma phan cung (D3D11VA) + doi mau bang shader tren GPU (xem VideoDecodeOptions).
// M5a: am thanh (swresample -> float32 stereo 48 kHz) vao AudioStream. Khi mixer dang rut am thanh cua nguon nay, HINH CHAY THEO
// DONG HO AM THANH (heardPosition); neu khong (khong co tieng, chua mo thiet bi, mixer khong lay, am thanh dung > 250 ms) thi chay theo
// dong ho tuong. Lap lai khong cat tieng: am thanh duoc noi lien, moc thoi gian cua hinh duoc tinh lai tu mau am thanh dau tien cua vong moi
// (khong tich luy lech giua do dai hinh va tieng).
class FFmpegSource : public Source {
public:
    explicit FFmpegSource(QString path, bool loop = true, VideoDecodeOptions opts = VideoDecodeOptions::defaults());
    ~FFmpegSource() override;
    QString name() const override { return name_; }
    TexRef acquire(GL& gl) override;
    QImage snapshot(int maxWidth = 160) const override;
    double durationSeconds() const override { return duration_; }
    void restart() override;                    // seek ve 0; cho den khi co khung moi thi acquire() tra ve rong (khong hien khung cu)
    QString error() const;     // rong neu khong loi
    AudioStream* audio() override { return hasAudio_ ? audio_.get() : nullptr; }
    bool hasAudio() const { return hasAudio_; }
    // Chan doan dong bo (test): moc "dong thoi gian lien tuc" (giay) cua khung hinh vua dua len, va do tre (ms) so voi dong ho dang theo.
    double presentedTime() const { return presentedCont_; }
    double lastLatenessMs() const { return lateMs_; }
    bool followingAudioClock() const { return followAudio_; }
    // Chan doan giai ma (M6): mo ta nhu "D3D11VA (phan cung) -> NV12 -> shader" / "phan mem -> yuv420p -> shader" / "phan mem -> RGBA (CPU)".
    QString decodeInfo() const;
    bool usesHardwareDecode() const { return hwActive_; }
    bool usesGpuColor() const { return gpuColorActive_; }

private:
    struct Frame {
        std::vector<uint8_t> rgba;              // duong CPU (sws_scale); rong khi dung yuv
        std::shared_ptr<YuvImage> yuv;          // duong GPU: plane Y/UV tho, shader doi mau
        std::shared_ptr<HapFrame> hap;          // HAP: khoi DXT, day thang len GPU
        int w = 0, h = 0;
        int rotation = 0;       // do xoay hien thi (theo chieu kim dong ho), tu display matrix
        uint64_t seq = 0;
    };
    void run();
    void setError(const QString& e);

    QString path_, name_;
    bool loop_;
    std::atomic<bool> stop_{false};
    std::thread thread_;

    mutable std::mutex m_;
    std::shared_ptr<Frame> latest_;
    QString error_;
    uint64_t uploaded_ = 0;
    int rotation_ = 0;
    std::atomic<double> duration_{0};
    std::atomic<bool> restartReq_{false};
    std::atomic<bool> waitFirst_{false};
    std::unique_ptr<AudioStream> audio_;
    std::atomic<bool> hasAudio_{false};
    std::atomic<double> presentedCont_{-1}, lateMs_{0};
    std::atomic<bool> followAudio_{false};
    GlTexture tex_;
    YuvTexture yuvTex_;
    HapTexture hapTex_;
    enum class Cur { Rgba, Yuv, Hap } cur_ = Cur::Rgba;
    VideoDecodeOptions opts_;
    QString info_;
    std::atomic<bool> hwActive_{false}, gpuColorActive_{false};
    struct BufPool;
    std::shared_ptr<BufPool> pool_;
};

} // namespace lc
