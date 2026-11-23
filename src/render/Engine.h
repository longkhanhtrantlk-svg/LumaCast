#pragma once
#include "core/Channel.h"
#include "render/OutputSink.h"
#include <QOffscreenSurface>
#include <QThread>
#include <atomic>
#include <functional>
#include <mutex>

namespace lc {

struct Project {
    std::vector<std::shared_ptr<Scene>> scenes;
    std::vector<std::shared_ptr<Channel>> channels;
    std::vector<OutputConfig> outputs;
    double fps = 60.0;
};

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

protected:
    void run() override;

private:
    Project& project_;
    std::vector<std::unique_ptr<OutputSink>> sinks_;
    std::unique_ptr<QOffscreenSurface> surface_;
    std::mutex mutex_;
    std::vector<std::function<void(double)>> commands_;
    std::atomic<double> fps_{0};
    std::atomic<bool> stop_{false};
    QString error_;
    qint64 t0_ = 0;
};

} // namespace lc
