#include "Engine.h"
#include "core/I18n.h"
#include "audio/AudioPlan.h"
#include "core/Features.h"
#include "render/FrameStats.h"
#include "render/NdiOutput.h"
#include "render/OutputWindow.h"
#include "render/SceneThumbs.h"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
#include <algorithm>
#include <chrono>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>
#endif

namespace lc {

using Clock = std::chrono::steady_clock;

namespace {
// Output bi tat boi goi giay phep: giu cho (chi so + cau hinh duoc luu lai nguyen ven) nhung khong hien/khong gui gi.
class DisabledSink : public OutputSink {
public:
    explicit DisabledSink(OutputConfig c) : cfg_(std::move(c)) {}
    const OutputConfig& config() const override { return cfg_; }
    OutputConfig& mutableConfig() override { return cfg_; }
    void present(QOpenGLContext&, Compositor&, RenderTarget*) override {}
private:
    OutputConfig cfg_;
};
} // namespace

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

RenderEngine::SceneThumbFrame RenderEngine::sceneThumbs() const {
    if (sceneThumbChannel_ < 0) return {};
    std::lock_guard lk(monitorMutex_);
    return sceneThumbs_;
}

RenderEngine::SceneThumbFrame RenderEngine::listThumbs() const {
    std::lock_guard lk(monitorMutex_);
    return sceneThumbs_;
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
    if (!Entitlements::current().ndi) return -1;
    if (const int mo = Entitlements::current().maxOutputs; mo > 0 && outputCount() >= mo) return -1;
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

// UI thread. Chua hien cua so (nguoi goi hien sau khi sink da nam trong danh sach).
std::unique_ptr<OutputSink> RenderEngine::makeSink(const OutputConfig& cfg, int index) {
    const int maxOut = Entitlements::current().maxOutputs;            // 0 = khong gioi han (mac dinh)
    // Output vuot gioi han goi / NDI khong co trong goi: giu mot sink RONG (khong hien/khong gui) de chi so Output va project luu lai van khop
    if (maxOut > 0 && index >= maxOut) {
        qWarning().noquote() << QString("Output '%1' bị tắt: gói giấy phép hiện tại tối đa %2 Output").arg(cfg.name).arg(maxOut);
        return std::make_unique<DisabledSink>(cfg);
    }
    if (cfg.kind == OutputKind::Ndi) {
        if (!Entitlements::current().ndi) {
            qWarning().noquote() << QString("Output NDI '%1' bị tắt: NDI không có trong gói giấy phép").arg(cfg.name);
            return std::make_unique<DisabledSink>(cfg);
        }
        return std::make_unique<NdiOutputSink>(cfg, project_.fps, &audio_);
    }
    return std::make_unique<OutputWindow>(cfg);
}

// Che do cua so (phat trien/thu): xep cac output thanh hang ngang duoi bang dieu khien, xuong dong khi het cho.
void RenderEngine::placeWindow(OutputWindow& w, const OutputConfig& cfg) {
    if (cfg.fullscreen) {
        if (w.canShow()) w.showFullScreen();
        else qWarning().noquote() << QString("Output '%1': man hinh '%2' (so %3) khong ton tai - KHONG hien hinh de tranh len nham man hinh khac")
                                          .arg(cfg.name, cfg.screenName).arg(cfg.screenIndex);
        return;
    }
    // thu nho neu lon hon man hinh (giu ti le), luon nam trong man hinh
    const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
    QSize sz = cfg.size;
    const QSize maxSz(int(avail.width() * 0.6), int(avail.height() * 0.5));
    if (sz.width() > maxSz.width() || sz.height() > maxSz.height()) sz = sz.scaled(maxSz, Qt::KeepAspectRatio);
    w.resize(sz);
    if (winX_ + sz.width() > avail.right() && winX_ > avail.left() + 20) { winX_ = avail.left() + 20; winY_ += winRowH_ + 30; winRowH_ = 0; }
    const int py = std::min(winY_, std::max(avail.top(), avail.bottom() - sz.height() - 40));
    w.setPosition(std::min(winX_, std::max(avail.left(), avail.right() - sz.width())), py);
    winX_ += sz.width() + 12;
    winRowH_ = std::max(winRowH_, sz.height());
    w.show();
}

void RenderEngine::startEngine() {
    now();                                                       // khoi tao moc thoi gian
    surface_ = std::make_unique<QOffscreenSurface>();            // phai tao tren thread chinh
    surface_->setFormat(QSurfaceFormat::defaultFormat());
    surface_->create();
    const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
    winX_ = avail.left() + 20;
    winY_ = avail.top() + 420;
    winRowH_ = 0;
    int index = 0;
    for (const OutputConfig& cfg : project_.outputs) {
        auto sink = makeSink(cfg, index++);
        if (auto* w = dynamic_cast<OutputWindow*>(sink.get())) placeWindow(*w, cfg);
        sinks_.push_back(std::move(sink));
    }
    stop_ = false;
    start();
}

bool RenderEngine::applyOutputs(const std::vector<OutputConfig>& cfgs, std::function<void()> sameFrame) {
    std::vector<OutputSink*> old;
    {
        std::lock_guard lk(sinksMu_);
        for (const auto& s : sinks_) old.push_back(s.get());
    }
    auto compatible = [](OutputSink* s, const OutputConfig& c) {
        if (auto* w = dynamic_cast<OutputWindow*>(s)) return w->matches(c);
        if (dynamic_cast<NdiOutputSink*>(s)) {
            const OutputConfig& o = s->config();
            return c.kind == OutputKind::Ndi && o.name == c.name && o.ndiName == c.ndiName && o.channel == c.channel && o.size == c.size
                   && o.ndiAudio == c.ndiAudio;
        }
        return false;                                              // sink rong (goi giay phep): tao lai
    };
    // Chon sink cu cho tung muc moi: uu tien cung ten (xoa mot Output giua danh sach khong tao lai cac Output sau), roi cung vi
    // tri (doi ten), roi bat ky.
    std::vector<int> reuse(cfgs.size(), -1);
    std::vector<char> used(old.size(), 0);
    for (int pass = 0; pass < 3; ++pass)
        for (size_t i = 0; i < cfgs.size(); ++i)
            for (size_t j = 0; j < old.size() && reuse[i] < 0; ++j) {
                if (used[j] || (pass == 0 && old[j]->config().name != cfgs[i].name) || (pass == 1 && j != i) || !compatible(old[j], cfgs[i])) continue;
                reuse[i] = int(j);
                used[j] = 1;
            }
    bool allBlack = !old.empty();
    for (OutputSink* s : old) allBlack = allBlack && s->black;

    auto fresh = std::make_shared<std::vector<std::unique_ptr<OutputSink>>>(cfgs.size());
    std::vector<OutputWindow*> toShow;
    std::vector<OutputConfig> toShowCfg;
    for (size_t i = 0; i < cfgs.size(); ++i) {
        if (reuse[i] >= 0) continue;
        (*fresh)[i] = makeSink(cfgs[i], int(i));
        OutputSink* s = (*fresh)[i].get();
        if (i < old.size()) { s->black = old[i]->black.load(); s->freeze = false; s->testPattern = old[i]->testPattern.load(); }
        if (allBlack) s->black = true;                              // dang BLACK ALL: man moi khong duoc loe hinh
        if (auto* w = dynamic_cast<OutputWindow*>(s)) { toShow.push_back(w); toShowCfg.push_back(cfgs[i]); }
    }

    std::vector<OutputWindow*> toHide;
    for (size_t j = 0; j < old.size(); ++j)
        if (!used[j])
            if (auto* w = dynamic_cast<OutputWindow*>(old[j])) toHide.push_back(w);
    for (OutputWindow* w : toHide) w->hide();                      // an truoc khi hien cua so moi tren cung man hinh

    const bool done = postAndWait([this, cfgs, reuse, fresh, sameFrame = std::move(sameFrame)](double) {
        std::lock_guard lk(sinksMu_);
        std::vector<std::unique_ptr<OutputSink>> next(cfgs.size());
        for (size_t i = 0; i < cfgs.size(); ++i) {
            if (reuse[i] >= 0) {
                next[i] = std::move(sinks_[size_t(reuse[i])]);
                const QString screen = next[i]->config().screenName;
                next[i]->mutableConfig() = cfgs[i];
                if (cfgs[i].fullscreen && cfgs[i].screenName.isEmpty()) next[i]->mutableConfig().screenName = screen;
            } else {
                next[i] = std::move((*fresh)[i]);
            }
        }
        for (auto& s : sinks_)
            if (s) retiring_.push_back(std::move(s));
        sinks_ = std::move(next);
        project_.outputs.clear();
        for (const auto& s : sinks_) project_.outputs.push_back(s->config());
        if (sameFrame) sameFrame();
    });

    for (size_t i = 0; i < toShow.size(); ++i) placeWindow(*toShow[i], toShowCfg[i]);
    for (size_t i = 0; i < cfgs.size(); ++i) {                       // cua so giu lai: ten / kich thuoc co the doi
        if (reuse[i] < 0) continue;
        if (auto* w = dynamic_cast<OutputWindow*>(output(int(i)))) {
            w->setTitle("LumaCast (LED Display Controller) - " + cfgs[i].name);
            if (!cfgs[i].fullscreen && w->isVisible() && old[size_t(reuse[i])] == w) {
                const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
                QSize sz = cfgs[i].size;
                const QSize maxSz(int(avail.width() * 0.6), int(avail.height() * 0.5));
                if (sz.width() > maxSz.width() || sz.height() > maxSz.height()) sz = sz.scaled(maxSz, Qt::KeepAspectRatio);
                if (sz != w->size()) w->resize(sz);
            }
        }
    }
    return done;
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
    retiring_.clear();
    graveyard_.clear();
}

// Video chay ca o Preview; khi VUA len Program (CUT, bat dau transition/T-bar...) thi phat lai tu dau. Nguon dang o Program
// san (vd. video nen dung chung cho hai Scene) khong bi tua. Video thuoc mot Scene bo chon "Phat lai khi len Program" khong tua.
void RenderEngine::rewindVideoGoingOnAir() {
    std::vector<const Scene*> live;
    for (const auto& c : project_.channels) c->liveScenes(live);
    std::vector<const Source*> now;
    std::vector<Source*> entering;
    auto has = [](const std::vector<const Source*>& v, const Source* s) { return std::find(v.begin(), v.end(), s) != v.end(); };
    for (const Scene* s : live)
        for (const Layer& l : s->layers) {
            if (!l.source || has(now, l.source.get())) continue;
            now.push_back(l.source.get());
            if (!has(onAir_, l.source.get())) entering.push_back(l.source.get());
        }
    if (!entering.empty()) {
        std::vector<const Source*> continuous;
        for (const auto& s : project_.scenes)
            if (!s->restartOnAir)
                for (const Layer& l : s->layers) if (l.source) continuous.push_back(l.source.get());
        for (Source* s : entering)
            if (!has(continuous, s)) s->rewind();
    }
    onAir_ = std::move(now);
    syncVideoLoop(live);
}

// Video lap lai tru khi Scene quyet dinh no bo chon "Lap": dang tren Program thi cac Scene dang len song quyet dinh, con lai
// thi moi Scene dung no (video dung chung cho nhieu Scene chi co mot bo giai ma).
void RenderEngine::syncVideoLoop(const std::vector<const Scene*>& live) {
    struct Use { Source* src; bool onAir, offOnAir, offAny; };
    std::vector<Use> uses;
    for (const auto& s : project_.scenes) {
        const bool isLive = std::find(live.begin(), live.end(), s.get()) != live.end();
        for (const Layer& l : s->layers) {
            if (!l.source || !l.source->hasTransport()) continue;
            auto it = std::find_if(uses.begin(), uses.end(), [&](const Use& u) { return u.src == l.source.get(); });
            if (it == uses.end()) it = uses.insert(uses.end(), {l.source.get(), false, false, false});
            it->onAir = it->onAir || isLive;
            it->offOnAir = it->offOnAir || (isLive && !s->loop);
            it->offAny = it->offAny || !s->loop;
        }
    }
    for (const Use& u : uses) u.src->setLooping(!(u.onAir ? u.offOnAir : u.offAny));
}

void RenderEngine::run() {
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
    QOpenGLContext ctx;
    ctx.setFormat(QSurfaceFormat::defaultFormat());
    if (!ctx.create() || !ctx.makeCurrent(surface_.get())) {
        error_ = LC_TR("Khong tao duoc context OpenGL 3.3 core");
        return;
    }
    Compositor comp;
    if (!comp.init()) {
        error_ = LC_TR("Khong khoi tao duoc shader");
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
    SceneThumbRing sceneRing;

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
        {
            std::lock_guard lk(sinksMu_);                          // Output vua bi go: tra tai nguyen GL, giu doi tuong lai
            for (auto& s : retiring_) {
                s->shutdown(ctx, comp);
                graveyard_.push_back(std::move(s));
            }
            retiring_.clear();
        }
        GlProfile::uploadNs() = 0;
        comp.resetTiming();
        pool.beginFrame();
        comp.setTime(t);
        for (const auto& sc : project_.scenes) sc->clock.tick(t, sc->duration, sc->loop);     // dong ho keyframe cua moi Scene
        rewindVideoGoingOnAir();
        for (size_t i = 0; i < project_.channels.size(); ++i)
            chanOut[i] = project_.channels[i]->render(comp, pool, t);

        // 2a) ke hoach tron am thanh (chi day cho mixer khi co thay doi: he so, transition, Scene, Layer...)
        {
            auto plan = buildAudioPlan(project_, t);
            if (!lastPlan_ || !plan->sameAs(*lastPlan_)) { lastPlan_ = plan; audio_.setPlan(plan); }
        }
        // tieng nhac dem nguoc: ke hoach da gom mute/am luong cua kenh nen chi can bao bat/tat
        {
            std::vector<AlarmEvent> events;
            for (size_t i = 0; i < project_.channels.size(); ++i)
                alarm_.update(int(i), project_.channels[i]->overlays.frames(t), events);
            for (const AlarmEvent& e : events) {
                if (e.stop) audio_.stopTones(e.channel, e.overlayId);
                else audio_.playTone(e.channel, e.overlayId, e.kind, e.volume);
            }
        }

        const int64_t upNs = GlProfile::uploadNs(), scNs = comp.timing().sceneNs, trNs = comp.timing().transitionNs;
        const int64_t monT0 = GlProfile::nowNs();
        // 2b) anh thu nho cho monitor UI (~15 lan/giay)
        if (++monitorTick >= monitorEvery) {
            monitorTick = 0;
            std::vector<MonitorFrame> mf(project_.channels.size());
            std::vector<char> keepProg(mf.size(), 0), keepPrev(mf.size(), 0);
            for (size_t i = 0; i < project_.channels.size(); ++i) {
                // doc bat dong bo (PBO): anh tre them 1 nhip monitor (~67 ms) nhung khong chan CPU cho GPU xong khung nay
                if (chanOut[i]) { mf[i].program = comp.thumbnailAsync(pool, *chanOut[i], 480, int(i) * 2); keepProg[i] = mf[i].program.isNull(); }
                if (RenderTarget* pv = project_.channels[i]->renderPreview(comp, pool)) {
                    mf[i].preview = comp.thumbnailAsync(pool, *pv, 480, int(i) * 2 + 1);
                    keepPrev[i] = mf[i].preview.isNull();
                }
            }
            std::lock_guard lk(monitorMutex_);
            for (size_t i = 0; i < mf.size() && i < monitors_.size(); ++i) {       // anh chua ve kip (lan dau/doi kich thuoc): giu anh cu, khong nhay ve trong
                if (keepProg[i]) mf[i].program = monitors_[i].program;
                if (keepPrev[i]) mf[i].preview = monitors_[i].preview;
            }
            monitors_ = std::move(mf);
        }

        int64_t monNs = GlProfile::nowNs() - monT0;
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
        const int64_t outNs = GlProfile::nowNs() - outT0;

        // 3b) anh thu nho Scene cho Multiview: lam SAU khi da dua hinh ra Output, toi da 1 Scene/khung, tinh vao giai doan monitor
        {
            const int mv = sceneThumbChannel_, list = listThumbChannel_;
            const int tc = mv >= 0 ? mv : list;
            if (tc >= 0 || sceneRing.channel() >= 0) {
                const int64_t t3 = GlProfile::nowNs();
                const bool valid = tc >= 0 && tc < int(project_.channels.size());
                ctx.makeCurrent(surface_.get());
                if (sceneRing.tick(comp, pool, valid ? tc : -1, valid ? &project_.channels[size_t(tc)]->scenes : nullptr,
                                   project_.fps, monitorTick == 0, Clock::now() - frameStart > period / 2,
                                   mv >= 0 ? SceneThumbRing::kMaxHz : SceneThumbRing::kListHz)) {
                    std::lock_guard lk(monitorMutex_);
                    sceneThumbs_ = {sceneRing.channel(), sceneRing.images(), sceneRing.scenes()};
                }
                if (GlProfile::sync()) comp.gl().glFinish();
                monNs += GlProfile::nowNs() - t3;
            }
        }

        const double frameMs = std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count();
        stats.add(frameMs);
        if (recording_) {
            std::lock_guard lk(recMu_);
            rec_.push_back({float(frameMs), float(double(upNs) / 1e6), float(double(scNs) / 1e6), float(double(trNs) / 1e6),
                            float(double(monNs) / 1e6), float(double(outNs) / 1e6)});
        }
        stUpload.add(double(upNs) / 1e6);
        stScene.add(double(scNs) / 1e6);
        stTrans.add(double(trNs) / 1e6);
        stMonitor.add(double(monNs) / 1e6);
        stOutput.add(double(outNs) / 1e6);
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
        for (auto& s : retiring_) s->shutdown(ctx, comp);
    }
    sceneRing.release(comp.gl());
    {
        std::lock_guard lk(monitorMutex_);
        sceneThumbs_ = {};
    }
    pool.clear();
    comp.shutdown();
    ctx.doneCurrent();
#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

} // namespace lc
