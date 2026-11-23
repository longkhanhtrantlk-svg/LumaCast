#pragma once
#include "core/Project.h"
#include "render/EffectRegistry.h"
#include "render/OutputSink.h"
#include <QImage>
#include <QOffscreenSurface>
#include <QThread>
#include <atomic>
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
    double now() const;
    double fps() const { return fps_; }
    int outputCount() const { return int(sinks_.size()); }
    OutputSink* output(int i) const { return sinks_[i].get(); }
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
    // Kho Effect/Transition dung chung (UI nap plug-in vao day; render thread bien dich + dung).
    EffectRegistry& registry() { return *registry_; }

protected:
    void run() override;

private:
    Project& project_;
    std::vector<std::unique_ptr<OutputSink>> sinks_;
    std::unique_ptr<QOffscreenSurface> surface_;
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
