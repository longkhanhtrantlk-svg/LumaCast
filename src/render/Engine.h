#pragma once
#include "audio/AudioMixer.h"
#include "core/Project.h"
#include "render/EffectRegistry.h"
#include "render/OutputSink.h"
#include <QImage>
#include <QOffscreenSurface>
#include <QThread>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>

namespace lc {

// Mot render thread duy nhat cho moi Output -> cac man LED ghep khong bi lech khung.
class RenderEngine : public QThread {
    Q_OBJECT
public:
    explicit RenderEngine(Project& project, QObject* parent = nullptr);
    ~RenderEngine() override;

    void startEngine();        // thread chinh: tao + hien cua so Output, roi chay render thread
    void stopEngine();
    // Gui lenh cho render thread (chay o dau khung hinh ke tiep). fn nhan thoi gian hien tai (giay).
    void post(std::function<void(double)> fn);
    // Nhu post() nhung cho (toi da timeoutMs) cho den khi render thread chay xong lenh. Dung cho thao tac hiem (Hoan tac/Lam lai nhap media)
    // can du lieu da doi truoc khi UI cap nhat tiep. Khong chay thread -> chay ngay. Tra false neu het thoi gian (lenh van se chay sau).
    bool postAndWait(std::function<void(double)> fn, int timeoutMs = 2000);
    double now() const;
    double fps() const { return fps_; }
    int outputCount() const { std::lock_guard lk(sinksMu_); return int(sinks_.size()); }
    OutputSink* output(int i) const { std::lock_guard lk(sinksMu_); return i >= 0 && i < int(sinks_.size()) ? sinks_[size_t(i)].get() : nullptr; }
    // Them Output NDI luc dang chay (M5b). Chi ho tro OutputKind::Ndi (cua so man hinh phai co luc khoi dong). Goi tu UI thread; cho render thread
    // xong roi moi tra ve, nen outputCount()/output(i) thay ngay Output moi. Tra chi so Output moi, hoac -1 neu loi.
    int addNdiOutput(const OutputConfig& cfg);
    QString error() const;
    QString gpuName() const;
    uint64_t droppedFrames() const { return dropped_; }
    // Thoi gian render moi khung (ms), cap nhat moi giay tren cua so ~300 khung gan nhat.
    double frameP50() const { return p50_; }
    double frameP99() const { return p99_; }
    double frameMax() const { return maxMs_; }
    // Watchdog: render thread khong bao nhip (heartbeat) qua nguong -> treo. Man hinh giu hinh cuoi (khong swap).
    bool stalled() const;
    void setStallThresholdMs(int ms) { stallMs_ = ms; }
    // Anh monitor (cap nhat ~15 lan/giay) cua Program/Preview cua mot Channel.
    struct MonitorFrame { QImage program, preview; };
    MonitorFrame monitor(int channel) const;
    // Render thread (goi trong post): chep cau hinh Output hien tai ve Project de luu file.
    void syncOutputsToProject();
    // Mixer am thanh (M5a). Engine dung ke hoach tron moi khung (render thread); thiet bi do ben ngoai mo bang audio().start(id).
    AudioMixer& audio() { return audio_; }
    // Kho Effect/Transition dung chung (UI nap plug-in vao day; render thread bien dich + dung).
    EffectRegistry& registry() { return *registry_; }

protected:
    void run() override;

private:
    Project& project_;
    std::vector<std::unique_ptr<OutputSink>> sinks_;      // chi them (khong xoa) khi dang chay; sinksMu_ bao ve vector (con tro Output ben vung cho den stopEngine)
    mutable std::mutex sinksMu_;
    std::unique_ptr<QOffscreenSurface> surface_;
    AudioMixer audio_;
    std::shared_ptr<AudioPlan> lastPlan_;
    EffectRegistry ownRegistry_;
    EffectRegistry* registry_ = &ownRegistry_;
    std::mutex mutex_;
    std::vector<std::function<void(double)>> commands_;
    std::atomic<double> fps_{0};
    std::atomic<bool> stop_{false};
    QString error_, gpu_;
    std::atomic<uint64_t> dropped_{0};
    std::atomic<double> p50_{0}, p99_{0}, maxMs_{0};
    std::atomic<int64_t> heartbeatNs_{0};
    std::atomic<int> stallMs_{2000};
    mutable std::mutex monitorMutex_;
    std::vector<MonitorFrame> monitors_;
    qint64 t0_ = 0;
};

} // namespace lc
