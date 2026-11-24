#pragma once
#include "Source.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace lc {

// Giai ma video + am thanh bang FFmpeg tren MOT thread rieng, tu lap lai.
// M1: giai ma + doi YUV->RGBA tren CPU. (M6: hw decode + doi mau bang shader.)
// M5a: am thanh (swresample -> float32 stereo 48 kHz) vao AudioStream. Khi mixer dang rut am thanh cua nguon nay, HINH CHAY THEO
// DONG HO AM THANH (heardPosition); neu khong (khong co tieng, chua mo thiet bi, mixer khong lay, am thanh dung > 250 ms) thi chay theo
// dong ho tuong. Lap lai khong cat tieng: am thanh duoc noi lien, moc thoi gian cua hinh duoc tinh lai tu mau am thanh dau tien cua vong moi
// (khong tich luy lech giua do dai hinh va tieng).
class FFmpegSource : public Source {
public:
    explicit FFmpegSource(QString path, bool loop = true);
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

private:
    struct Frame {
        std::vector<uint8_t> rgba;
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
};

} // namespace lc
