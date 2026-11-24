#include "Engine.h"
#include "audio/AudioPlan.h"
#include "render/FrameStats.h"
#include "render/NdiOutput.h"
#include "render/OutputWindow.h"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
#include <chrono>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>
#endif

namespace lc {

using Clock = std::chrono::steady_clock;

RenderEngine::RenderEngine(Project& p, QObject* parent) : QThread(parent), project_(p) { ownRegistry_.loadBuiltins(); }

RenderEngine::~RenderEngine() { stopEngine(); }

double RenderEngine::now() const {
    static const auto origin = Clock::now();
    return std::chrono::duration<double>(Clock::now() - origin).count();
}

static int64_t steadyNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

bool RenderEngine::stalled() const {
    if (!isRunning() || heartbeatNs_ == 0) return false;
    return steadyNs() - heartbeatNs_ > int64_t(stallMs_) * 1000000;
}

QString RenderEngine::error() const { return error_; }

QString RenderEngine::gpuName() const {
    std::lock_guard lk(monitorMutex_);
    return gpu_;
}

RenderEngine::MonitorFrame RenderEngine::monitor(int ch) const {
    std::lock_guard lk(monitorMutex_);
    return ch >= 0 && ch < int(monitors_.size()) ? monitors_[ch] : MonitorFrame{};
}

StageReport RenderEngine::stages() const {
    std::lock_guard lk(stageMu_);
    return stages_;
}

void RenderEngine::startRecording() {
    std::lock_guard lk(recMu_);
    rec_.clear();
    rec_.reserve(1 << 15);
    recording_ = true;
}

std::vector<RenderEngine::FrameSample> RenderEngine::takeRecording() {
    recording_ = false;
    std::lock_guard lk(recMu_);
    return std::move(rec_);
}

void RenderEngine::setProfileSync(bool on) { GlProfile::setSync(on); }

int RenderEngine::addSink(std::unique_ptr<OutputSink> sink) {
    if (!sink) return -1;
    auto box = std::make_shared<std::unique_ptr<OutputSink>>(std::move(sink));
    auto idx = std::make_shared<std::atomic<int>>(-1);
    postAndWait([this, box, idx](double) {
        std::lock_guard lk(sinksMu_);
        idx->store(int(sinks_.size()));
        sinks_.push_back(std::move(*box));
    });
    if (*box) return -1;
    return idx->load();
}

int RenderEngine::addNdiOutput(const OutputConfig& cfgIn) {
    OutputConfig cfg = cfgIn;
    cfg.kind = OutputKind::Ndi;
    if (cfg.channel < 0 || cfg.channel >= int(project_.channels.size())) return -1;
    auto sink = std::make_shared<std::unique_ptr<OutputSink>>(std::make_unique<NdiOutputSink>(cfg, project_.fps, &audio_));
    auto idx = std::make_shared<std::atomic<int>>(-1);
    postAndWait([this, sink, idx, cfg](double) {
        std::lock_guard lk(sinksMu_);
        idx->store(int(sinks_.size()));
        sinks_.push_back(std::move(*sink));
        project_.outputs.push_back(sinks_.back()->config());
    });
    if (*sink) return -1;                                   // lenh chua chay (het thoi gian): sink van con o day -> bo
    return idx->load();
}

void RenderEngine::syncOutputsToProject() {
    std::lock_guard lk(sinksMu_);
    for (size_t i = 0; i < sinks_.size() && i < project_.outputs.size(); ++i)
        project_.outputs[i] = sinks_[i]->config();
}

void RenderEngine::post(std::function<void(double)> fn) {
    std::lock_guard lk(mutex_);
    commands_.push_back(std::move(fn));
}

bool RenderEngine::postAndWait(std::function<void(double)> fn, int timeoutMs) {
    if (!isRunning() || QThread::currentThread() == this) { fn(now()); return true; }
    struct Sync { std::mutex m; std::condition_variable cv; bool done = false; };
    auto sync = std::make_shared<Sync>();
    post([fn = std::move(fn), sync](double t) {
        fn(t);
        { std::lock_guard lk(sync->m); sync->done = true; }
        sync->cv.notify_all();
    });
    std::unique_lock lk(sync->m);
    return sync->cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] { return sync->done; });
}

void RenderEngine::startEngine() {
    now();                                                       // khoi tao moc thoi gian
    surface_ = std::make_unique<QOffscreenSurface>();            // phai tao tren thread chinh
    surface_->setFormat(QSurfaceFormat::defaultFormat());
    surface_->create();
    // Che do cua so (phat trien): xep cac output thanh hang ngang duoi bang dieu khien, xuong dong khi het cho.
    const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
    int x = avail.left() + 20, y = avail.top() + 420, rowH = 0;
    for (const OutputConfig& cfg : project_.outputs) {
        if (cfg.kind == OutputKind::Ndi) {
            sinks_.push_back(std::make_unique<NdiOutputSink>(cfg, project_.fps, &audio_));
            continue;
        }
        auto w = std::make_unique<OutputWindow>(cfg);
        if (cfg.fullscreen) {
            if (w->canShow()) w->showFullScreen();
            else qWarning().noquote() << QString("Output '%1': man hinh '%2' (so %3) khong ton tai - KHONG hien hinh de tranh len nham man hinh khac")
                                              .arg(cfg.name, cfg.screenName).arg(cfg.screenIndex);
        } else {
            // Cua so thu nghiem: thu nho neu lon hon man hinh (giu ti le), xep hang ngang, luon nam trong man hinh.
            QSize sz = cfg.size;
            const QSize maxSz(int(avail.width() * 0.6), int(avail.height() * 0.5));
            if (sz.width() > maxSz.width() || sz.height() > maxSz.height()) sz = sz.scaled(maxSz, Qt::KeepAspectRatio);
            w->resize(sz);
            if (x + sz.width() > avail.right() && x > avail.left() + 20) { x = avail.left() + 20; y += rowH + 30; rowH = 0; }
            const int py = std::min(y, std::max(avail.top(), avail.bottom() - sz.height() - 40));
            w->setPosition(std::min(x, std::max(avail.left(), avail.right() - sz.width())), py);
            x += sz.width() + 12;
            rowH = std::max(rowH, sz.height());
            w->show();
        }
        sinks_.push_back(std::move(w));
    }
    stop_ = false;
    start();
}

void RenderEngine::stopEngine() {
    if (isRunning()) {
        stop_ = true;
        wait();
    }
    audio_.stop();
    audio_.releasePlan();
    lastPlan_.reset();
    std::lock_guard lk(sinksMu_);
    sinks_.clear();
}

void RenderEngine::run() {
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
    QOpenGLContext ctx;
    ctx.setFormat(QSurfaceFormat::defaultFormat());
    if (!ctx.create() || !ctx.makeCurrent(surface_.get())) {
        error_ = "Khong tao duoc context OpenGL 3.3 core";
        return;
    }
    Compositor comp;
    if (!comp.init()) {
        error_ = "Khong khoi tao duoc shader";
        return;
    }
    TargetPool pool(comp.gl());
    comp.setEffectContext(registry_, &pool);
    {
        std::lock_guard lk(monitorMutex_);
        gpu_ = QString::fromUtf8(reinterpret_cast<const char*>(comp.gl().glGetString(GL_RENDERER)));
        monitors_.assign(project_.channels.size(), MonitorFrame{});
    }
    const int monitorEvery = std::max(1, int(project_.fps / 15.0));
    int monitorTick = 0;

    FrameStats stats, stUpload, stScene, stTrans, stMonitor, stOutput;
    auto statsMark = Clock::now();
    heartbeatNs_ = steadyNs();
    qInfo().noquote() << "Engine bat dau: GPU" << gpuName() << "| output:" << int(project_.outputs.size())
                      << "| kenh:" << int(project_.channels.size()) << "| fps:" << project_.fps;
    const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / project_.fps));
    auto next = Clock::now();
    int frames = 0;
    auto fpsMark = Clock::now();
    std::vector<RenderTarget*> chanOut(project_.channels.size(), nullptr);

    while (!stop_) {
        const auto frameStart = Clock::now();
        heartbeatNs_ = steadyNs();
        // 1) lenh tu UI
        std::vector<std::function<void(double)>> cmds;
        {
            std::lock_guard lk(mutex_);
            cmds.swap(commands_);
        }
        const double t = now();
        for (auto& c : cmds) c(t);

        // 2) render tat ca Channel vao FBO
        ctx.makeCurrent(surface_.get());
        GlProfile::uploadNs() = 0;
        comp.resetTiming();
        pool.beginFrame();
        comp.setTime(t);
        for (const auto& sc : project_.scenes) sc->clock.tick(t, sc->duration, sc->loop);     // dong ho keyframe cua moi Scene
        for (size_t i = 0; i < project_.channels.size(); ++i)
            chanOut[i] = project_.channels[i]->render(comp, pool, t);

        // 2a) ke hoach tron am thanh (chi day cho mixer khi co thay doi: he so, transition, Scene, Layer...)
        {
            auto plan = buildAudioPlan(project_, t);
            if (!lastPlan_ || !plan->sameAs(*lastPlan_)) { lastPlan_ = plan; audio_.setPlan(plan); }
        }

        const int64_t upNs = GlProfile::uploadNs(), scNs = comp.timing().sceneNs, trNs = comp.timing().transitionNs;
        const int64_t monT0 = GlProfile::nowNs();
        // 2b) anh thu nho cho monitor UI (~15 lan/giay)
        if (++monitorTick >= monitorEvery) {
            monitorTick = 0;
            std::vector<MonitorFrame> mf(project_.channels.size());
            for (size_t i = 0; i < project_.channels.size(); ++i) {
                if (chanOut[i]) mf[i].program = comp.thumbnail(pool, *chanOut[i], 480);
                if (RenderTarget* pv = project_.channels[i]->renderPreview(comp, pool))
                    mf[i].preview = comp.thumbnail(pool, *pv, 480);
            }
            std::lock_guard lk(monitorMutex_);
            monitors_ = std::move(mf);
        }

        const int64_t monNs = GlProfile::nowNs() - monT0;
        const int64_t outT0 = GlProfile::nowNs();
        // 3) dua ra tung Output
        {
            std::vector<OutputSink*> cur;
            {
                std::lock_guard lk(sinksMu_);
                for (auto& s : sinks_) cur.push_back(s.get());
            }
            for (OutputSink* sink : cur) {
                const int ch = sink->config().channel;
                sink->present(ctx, comp, (ch >= 0 && ch < int(chanOut.size())) ? chanOut[ch] : nullptr);
            }
            if (GlProfile::sync()) comp.gl().glFinish();
        }

        const double frameMs = std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count();
        stats.add(frameMs);
        if (recording_) {
            std::lock_guard lk(recMu_);
            rec_.push_back({float(frameMs), float(double(upNs) / 1e6), float(double(scNs) / 1e6), float(double(trNs) / 1e6),
                            float(double(monNs) / 1e6), float(double(GlProfile::nowNs() - outT0) / 1e6)});
        }
        stUpload.add(double(upNs) / 1e6);
        stScene.add(double(scNs) / 1e6);
        stTrans.add(double(trNs) / 1e6);
        stMonitor.add(double(monNs) / 1e6);
        stOutput.add(double(GlProfile::nowNs() - outT0) / 1e6);
        if (Clock::now() - statsMark >= std::chrono::seconds(1)) {
            p50_ = stats.percentile(0.50);
            p99_ = stats.percentile(0.99);
            maxMs_ = stats.max();
            auto st = [](const FrameStats& f) { return StageStat{f.percentile(0.50), f.percentile(0.99), f.max()}; };
            {
                std::lock_guard lk(stageMu_);
                stages_ = StageReport{st(stats), st(stUpload), st(stScene), st(stTrans), st(stMonitor), st(stOutput)};
            }
            statsMark = Clock::now();
        }

        // 4) nhip: ngu gan het roi quay tron 1ms cuoi cho chinh xac
        ++frames;
        const auto nowC = Clock::now();
        if (nowC - fpsMark >= std::chrono::seconds(1)) {
            fps_ = frames / std::chrono::duration<double>(nowC - fpsMark).count();
            frames = 0;
            fpsMark = nowC;
        }
        next += period;
        if (nowC > next) dropped_ += 1 + uint64_t((nowC - next) / period);   // khung xong sau han chot = roi khung
        if (next < nowC - period * 4) next = nowC;               // tre qua nhieu -> bo qua, khong don
        const auto wake = next - std::chrono::milliseconds(1);
        if (Clock::now() < wake) std::this_thread::sleep_until(wake);
        while (Clock::now() < next) {}
    }

    ctx.makeCurrent(surface_.get());
    {
        std::lock_guard lk(sinksMu_);
        for (auto& s : sinks_) s->shutdown(ctx, comp);
    }
    pool.clear();
    comp.shutdown();
    ctx.doneCurrent();
#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

} // namespace lc
