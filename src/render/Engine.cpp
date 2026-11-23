#include "Engine.h"
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

RenderEngine::RenderEngine(Project& p, QObject* parent) : QThread(parent), project_(p) {}

RenderEngine::~RenderEngine() { stopEngine(); }

double RenderEngine::now() const {
    static const auto origin = Clock::now();
    return std::chrono::duration<double>(Clock::now() - origin).count();
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

void RenderEngine::syncOutputsToProject() {
    for (size_t i = 0; i < sinks_.size() && i < project_.outputs.size(); ++i)
        project_.outputs[i] = sinks_[i]->config();
}

void RenderEngine::post(std::function<void(double)> fn) {
    std::lock_guard lk(mutex_);
    commands_.push_back(std::move(fn));
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
        auto w = std::make_unique<OutputWindow>(cfg);
        if (cfg.fullscreen) {
            w->showFullScreen();
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
    {
        std::lock_guard lk(monitorMutex_);
        gpu_ = QString::fromUtf8(reinterpret_cast<const char*>(comp.gl().glGetString(GL_RENDERER)));
        monitors_.assign(project_.channels.size(), MonitorFrame{});
    }
    const int monitorEvery = std::max(1, int(project_.fps / 15.0));
    int monitorTick = 0;

    const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / project_.fps));
    auto next = Clock::now();
    int frames = 0;
    auto fpsMark = Clock::now();
    std::vector<RenderTarget*> chanOut(project_.channels.size(), nullptr);

    while (!stop_) {
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
        pool.beginFrame();
        for (size_t i = 0; i < project_.channels.size(); ++i)
            chanOut[i] = project_.channels[i]->render(comp, pool, t);

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

        // 3) dua ra tung Output
        for (auto& sink : sinks_) {
            const int ch = sink->config().channel;
            sink->present(ctx, comp, (ch >= 0 && ch < int(chanOut.size())) ? chanOut[ch] : nullptr);
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
    pool.clear();
    comp.shutdown();
    ctx.doneCurrent();
#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

} // namespace lc
