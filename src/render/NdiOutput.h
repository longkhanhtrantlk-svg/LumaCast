#pragma once
#include "audio/AudioMixer.h"
#include "live/Ndi.h"
#include "render/GlHelpers.h"
#include "render/OutputSink.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace lc {

// Output NDI (OUT-13): ve Program cua Channel vao mot FBO kich thuoc cau hinh (cung scale/crop/xoay/hieu chinh nhu man hinh), doc nguoc ve CPU bang
// 2 PBO (tre them 1 khung, KHONG chan render thread), roi chuyen cho THREAD GUI rieng (NdiSender co the cham/chan khi mang dong) qua o "moi nhat":
// render nhanh hon gui thi rot khung cu, khong bao gio hang doi. Am thanh: bus cua Channel qua AudioMixer::setTap -> thread gui.
// Den/Dung/Mau thu cua OutputSink hoat dong nhu man hinh (Den = gui khung den; Dung = khong cap nhat hinh, giu khung cuoi).
class NdiOutputSink : public OutputSink {
public:
    NdiOutputSink(const OutputConfig& cfg, double fps, AudioMixer* mixer);
    ~NdiOutputSink() override;
    const OutputConfig& config() const override { return cfg_; }
    OutputConfig& mutableConfig() override { return cfg_; }
    void present(QOpenGLContext& ctx, Compositor& comp, RenderTarget* src) override;
    void shutdown(QOpenGLContext& ctx, Compositor& comp) override;

    // UI/thread bat ky
    QString statusText() const;               // "Đang phát (N người nhận)" / "Chưa cài NDI Runtime"...
    bool sending() const { return sending_; }
    int connections() const { return conns_; }
    uint64_t sentFrames() const { return sentFrames_; }
    uint64_t droppedFrames() const { return dropped_; }
    QString sourceName() const { return nameUsed_; }

private:
    void senderMain();
    void setStatus(const QString& s);

    OutputConfig cfg_;
    const double fps_;
    AudioMixer* mixer_;
    QString nameUsed_;
    // GL (render thread)
    GLuint fbo_ = 0, tex_ = 0, pbo_[2] = {0, 0};
    int glW_ = 0, glH_ = 0;
    int pboIdx_ = 0;
    bool pboFilled_[2] = {false, false};
    GlTexture testTex_;
    QSize testSize_;
    std::vector<uint8_t> back_;
    // chuyen khung sang thread gui
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<uint8_t> slot_;
    int slotW_ = 0, slotH_ = 0;
    bool slotNew_ = false;
    std::atomic<bool> stop_{false};
    std::thread th_;
    std::shared_ptr<AudioStream> tap_;
    mutable std::mutex statusM_;
    QString status_;
    std::atomic<bool> sending_{false};
    std::atomic<int> conns_{0};
    std::atomic<uint64_t> sentFrames_{0}, dropped_{0};
};

} // namespace lc
