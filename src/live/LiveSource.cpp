#include "LiveSource.h"
#include "core/I18n.h"
#include <QImage>
#include <chrono>
#include <cmath>

namespace lc {

const char* liveStateId(LiveState s) {
    switch (s) {
    case LiveState::Connecting: return "connecting";
    case LiveState::Live: return "live";
    case LiveState::Lost: return "lost";
    case LiveState::Unavailable: return "unavailable";
    }
    return "?";
}

LiveSource::LiveSource(QString name, QString kind, LiveInputFactory factory, LiveOptions opts)
    : name_(std::move(name)), kind_(std::move(kind)), factory_(std::move(factory)), opts_(opts) {
    status_ = LC_TR("Đang kết nối...");
    thread_ = std::thread([this] { run(); });
}

LiveSource::~LiveSource() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

void LiveSource::setStatus(LiveState s, const QString& msg) {
    {
        std::lock_guard lk(m_);
        status_ = msg;
    }
    state_ = s;
}

QString LiveSource::statusText() const {
    std::lock_guard lk(m_);
    return status_;
}

void LiveSource::reconnect() { reconnectReq_ = true; }

TexRef LiveSource::acquire(GL& gl) {
    std::shared_ptr<Frame> f;
    {
        std::lock_guard lk(m_);
        if (latest_ && latest_->seq != uploaded_) f = latest_;
    }
    if (f) {
        tex_.upload(gl, f->rgba.data(), f->w, f->h);
        if (f->seq > uploaded_ + 1) droppedVideo_ += f->seq - uploaded_ - 1;      // khung bi de truoc khi kip len GPU
        uploaded_ = f->seq;
        latencyMs_ = double(audioSteadyNs() - f->arriveNs) / 1e6;
    }
    return {tex_.id(), tex_.w(), tex_.h(), 0};          // mat nguon: texture cu van con -> giu khung cuoi
}

QImage LiveSource::snapshot(int maxWidth) const {
    std::shared_ptr<Frame> f;
    {
        std::lock_guard lk(m_);
        f = latest_;
    }
    if (!f) return {};
    QImage img(f->rgba.data(), f->w, f->h, f->w * 4, QImage::Format_RGBA8888);
    // .copy(): khi khong phai thu nho, scaledToWidth tra ve chinh `img` dang muon bo nho cua `f`
    return img.scaledToWidth(std::min(maxWidth, f->w), Qt::SmoothTransformation).copy();
}

LiveSource::Stats LiveSource::stats() const {
    Stats s;
    s.videoFrames = videoFrames_;
    s.audioBlocks = audioBlocks_;
    s.droppedVideo = droppedVideo_;
    s.reconnects = reconnects_;
    s.audioResyncs = audioResyncs_;
    s.audioFillMs = fillMs_;
    s.audioRatioPpm = ratioPpm_;
    s.latencyMs = latencyMs_;
    const int64_t t = lastFrameNs_;
    s.frameAgeMs = t ? double(audioSteadyNs() - t) / 1e6 : -1;
    s.width = w_;
    s.height = h_;
    s.fps = fps_;
    return s;
}

void LiveSource::run() {
    DriftResampler drift(audio_, opts_.audioTargetMs);
    std::unique_ptr<LiveInput> in;
    LiveVideoFrame vf;
    LiveAudioBlock ab;
    uint64_t seq = 0;
    int backoff = opts_.retryMs;
    int64_t lastDataNs = 0;
    int64_t fpsMark = audioSteadyNs();
    uint64_t fpsCount = 0;
    bool everConnected = false;

    auto sleepInterruptible = [&](int ms) {
        for (int waited = 0; waited < ms && !stop_ && !reconnectReq_; waited += 20)
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(20, ms - waited)));
    };

    while (!stop_) {
        reconnectReq_ = false;
        if (!in) {
            if (!everConnected) setStatus(LiveState::Connecting, LC_TR("Đang kết nối..."));
            LiveOpenResult r = factory_();
            if (stop_) break;
            if (!r.input) {
                setStatus(r.unavailable ? LiveState::Unavailable : LiveState::Lost, r.error.isEmpty() ? LC_TR("Không kết nối được") : r.error);
                sleepInterruptible(r.unavailable ? opts_.unavailableRetryMs : backoff);
                backoff = std::min(backoff * 2, opts_.retryMaxMs);
                continue;
            }
            in = std::move(r.input);
            drift.reset();
            lastDataNs = audioSteadyNs();
            ++reconnects_;
            everConnected = true;
            if (state_ != LiveState::Lost) setStatus(LiveState::Connecting, LC_TR("Đang chờ tín hiệu..."));   // da mo nhung chua co khung; dang "Mat tin hieu" thi giu nguyen cho den khi co hinh
        }

        const LiveInput::Event ev = in->capture(20, vf, ab);
        const int64_t now = audioSteadyNs();
        if (reconnectReq_.exchange(false)) { in.reset(); backoff = opts_.retryMs; continue; }
        bool lost = ev == LiveInput::Event::Lost;
        if (ev == LiveInput::Event::Video && vf.w > 0 && vf.h > 0 && vf.rgba.size() >= size_t(vf.w) * size_t(vf.h) * 4) {
            auto f = std::make_shared<Frame>();
            f->w = vf.w; f->h = vf.h;
            f->rgba = std::move(vf.rgba);
            f->arriveNs = now;
            f->seq = ++seq;
            {
                std::lock_guard lk(m_);
                latest_ = std::move(f);
            }
            lastDataNs = now;
            lastFrameNs_ = now;
            w_ = vf.w; h_ = vf.h;
            ++videoFrames_;
            ++fpsCount;
            if (state_ != LiveState::Live) { setStatus(LiveState::Live, QString(LC_TR("Trực tiếp %1x%2")).arg(vf.w).arg(vf.h)); backoff = opts_.retryMs; }
            vf = LiveVideoFrame();
        } else if (ev == LiveInput::Event::Audio && !ab.data.empty()) {
            lastDataNs = now;
            if (opts_.audio) {
                drift.process(ab.data.data(), ab.data.size() / size_t(std::max(1, ab.channels)), ab.rate, ab.channels);
                hasAudio_ = true;
                ++audioBlocks_;
                fillMs_ = drift.fillMs();
                ratioPpm_ = drift.ratioPpm();
                audioResyncs_ = drift.resyncs();
            }
        } else if (ev == LiveInput::Event::None && now - lastDataNs > int64_t(opts_.stallMs) * 1000000) {
            lost = true;
        }
        if (now - fpsMark >= 1000000000) {
            fps_ = double(fpsCount) * 1e9 / double(now - fpsMark);
            fpsCount = 0;
            fpsMark = now;
        }
        if (lost) {                          // giu khung cuoi (latest_ khong bi xoa), im lang, noi lai
            in.reset();
            setStatus(LiveState::Lost, LC_TR("Mất tín hiệu — đang thử nối lại"));
            fps_ = 0;
            sleepInterruptible(backoff);
            backoff = std::min(backoff * 2, opts_.retryMaxMs);
        }
    }
}

} // namespace lc
