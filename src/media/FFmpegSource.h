#pragma once
#include "Source.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace lc {

// Giai ma video bang FFmpeg tren thread rieng, phat theo PTS, tu lap lai.
// M1: giai ma + doi YUV->RGBA tren CPU. (M6: hw decode + doi mau bang shader.)
class FFmpegSource : public Source {
public:
    explicit FFmpegSource(QString path, bool loop = true);
    ~FFmpegSource() override;
    QString name() const override { return name_; }
    TexRef acquire(GL& gl) override;
    QString error() const;     // rong neu khong loi

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
    GlTexture tex_;
};

} // namespace lc
