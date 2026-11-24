#include "NdiFake.h"
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>

namespace lc {

namespace {
using Clock = std::chrono::steady_clock;

struct NetEvent {
    LiveInput::Event kind = LiveInput::Event::None;
    LiveVideoFrame v;
    LiveAudioBlock a;
};

// Mot receiver dang nghe mot nguon (theo TEN, noi lai tu dong khi nguon xuat hien lai — giong NDI that).
struct Sub {
    std::mutex m;
    std::condition_variable cv;
    std::deque<NetEvent> q;
    size_t audioQueued = 0;
    void push(NetEvent e) {
        {
            std::lock_guard lk(m);
            if (e.kind == LiveInput::Event::Video) {          // hinh: chi giu vai khung moi nhat (nhu receiver that)
                size_t nv = 0;
                for (const auto& x : q) nv += x.kind == LiveInput::Event::Video;
                if (nv >= 4) for (auto it = q.begin(); it != q.end(); ++it) if (it->kind == LiveInput::Event::Video) { q.erase(it); break; }
            } else if (e.kind == LiveInput::Event::Audio) {
                audioQueued += e.a.data.size() / size_t(std::max(1, e.a.channels));
                while (audioQueued > 96000 && !q.empty()) {          // > 2 s: bo cu
                    if (q.front().kind == LiveInput::Event::Audio) audioQueued -= q.front().a.data.size() / size_t(std::max(1, q.front().a.channels));
                    q.pop_front();
                }
            }
            q.push_back(std::move(e));
        }
        cv.notify_one();
    }
};

LiveVideoFrame makeFrame(int w, int h, uint64_t n) {
    LiveVideoFrame f;
    f.w = w; f.h = h;
    f.rgba.resize(size_t(w) * h * 4);
    const int bar = int((n * 4) % uint64_t(std::max(1, w)));           // vach doc chay ngang: nhin la biet khung co chuyen dong
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t* p = &f.rgba[(size_t(y) * w + x) * 4];
            const bool on = std::abs(x - bar) < std::max(2, w / 32);
            p[0] = on ? 255 : uint8_t(x * 255 / std::max(1, w - 1));
            p[1] = on ? 255 : uint8_t(y * 255 / std::max(1, h - 1));
            p[2] = on ? 255 : 64;
            p[3] = 255;
        }
    return f;
}
} // namespace

struct FakeSource {
    QString name;
    FakeNdiBackend::SenderCfg cfg;
    std::atomic<bool> alive{true};
    std::atomic<bool> stop{false};
    std::atomic<double> ppm{0};
    std::thread th;
    std::mutex m;
    std::vector<std::weak_ptr<Sub>> subs;
    // chi cho nguoi gui "NDI ra" (khong co thread tong hop)
    bool external = false;
    std::shared_ptr<FakeNdiBackend::SentStats> sent;
    LiveVideoFrame lastSent;
    std::mutex lastM;

    void publish(const NetEvent& e) {
        std::vector<std::shared_ptr<Sub>> live;
        {
            std::lock_guard lk(m);
            for (auto it = subs.begin(); it != subs.end();) {
                if (auto s = it->lock()) { live.push_back(s); ++it; } else it = subs.erase(it);
            }
        }
        for (auto& s : live) s->push(e);
    }
    size_t subCount() {
        std::lock_guard lk(m);
        size_t n = 0;
        for (auto& w : subs) n += !w.expired();
        return n;
    }
    void producer() {
        const auto t0 = Clock::now();
        uint64_t vN = 0;
        double aT = 0, aPhase = 0;                    // aT: giay (theo dong ho gia) da phat; thuc te nhanh/cham theo ppm
        const double vPeriod = 1.0 / cfg.fps;
        uint64_t aSamples = 0;
        while (!stop) {
            const double now = std::chrono::duration<double>(Clock::now() - t0).count();
            bool did = false;
            if (alive && now >= double(vN) * vPeriod) {
                NetEvent e; e.kind = LiveInput::Event::Video; e.v = makeFrame(cfg.w, cfg.h, vN);
                publish(e);
                did = true;
            }
            if (now >= double(vN) * vPeriod) ++vN;
            if (cfg.audio) {
                // mau thu `aSamples` den han luc aSamples / (48000 * (1 + ppm))
                const double rate = 48000.0 * (1.0 + ppm.load() * 1e-6);
                while (now >= double(aSamples + uint64_t(cfg.audioBlock)) / rate) {
                    if (alive) {
                        NetEvent e; e.kind = LiveInput::Event::Audio;
                        e.a.rate = 48000; e.a.channels = 2;
                        e.a.data.resize(size_t(cfg.audioBlock) * 2);
                        for (int i = 0; i < cfg.audioBlock; ++i) {
                            const float v = 0.25f * float(std::sin(aPhase));
                            aPhase += 2 * 3.14159265358979323846 * cfg.audioHz / 48000.0;
                            e.a.data[size_t(i) * 2] = v; e.a.data[size_t(i) * 2 + 1] = v;
                        }
                        publish(e);
                        did = true;
                    }
                    aSamples += uint64_t(cfg.audioBlock);
                }
            }
            (void)aT; (void)did;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
};

struct FakeNdiBackend::Net {
    std::mutex m;
    std::map<QString, std::shared_ptr<FakeSource>> sources;
    std::shared_ptr<FakeSource> find(const QString& n) {
        std::lock_guard lk(m);
        auto it = sources.find(n);
        return it == sources.end() ? nullptr : it->second;
    }
};

namespace {
class FakeInput : public LiveInput {
public:
    FakeInput(std::shared_ptr<FakeNdiBackend::Net> net, QString name) : net_(std::move(net)), name_(std::move(name)), sub_(std::make_shared<Sub>()) {}
    Event capture(int timeoutMs, LiveVideoFrame& v, LiveAudioBlock& a) override {
        if (!attached_ || !src_.lock()) attach();
        std::unique_lock lk(sub_->m);
        if (sub_->q.empty()) sub_->cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] { return !sub_->q.empty(); });
        if (sub_->q.empty()) return Event::None;
        NetEvent e = std::move(sub_->q.front());
        sub_->q.pop_front();
        if (e.kind == Event::Video) { v = std::move(e.v); return Event::Video; }
        if (e.kind == Event::Audio) {
            sub_->audioQueued -= std::min(sub_->audioQueued, e.a.data.size() / size_t(std::max(1, e.a.channels)));
            a = std::move(e.a);
            return Event::Audio;
        }
        return e.kind;
    }
    QString info() const override { return name_; }
private:
    void attach() {
        if (auto s = net_->find(name_)) {
            std::lock_guard lk(s->m);
            s->subs.push_back(sub_);
            src_ = s;
            attached_ = true;
        } else {
            attached_ = false;
        }
    }
    std::shared_ptr<FakeNdiBackend::Net> net_;
    QString name_;
    std::shared_ptr<Sub> sub_;
    std::weak_ptr<FakeSource> src_;
    bool attached_ = false;
};

class FakeSender : public NdiSender {
public:
    FakeSender(std::shared_ptr<FakeNdiBackend::Net> net, std::shared_ptr<FakeSource> s) : net_(std::move(net)), src_(std::move(s)) {}
    ~FakeSender() override {
        std::lock_guard lk(net_->m);
        auto it = net_->sources.find(src_->name);
        if (it != net_->sources.end() && it->second == src_) net_->sources.erase(it);
    }
    bool sendVideo(const uint8_t* rgba, int w, int h, int stride, int fpsNum, int fpsDen) override {
        NetEvent e; e.kind = LiveInput::Event::Video;
        e.v.w = w; e.v.h = h;
        e.v.rgba.resize(size_t(w) * h * 4);
        for (int y = 0; y < h; ++y) std::memcpy(&e.v.rgba[size_t(y) * w * 4], rgba + size_t(y) * stride, size_t(w) * 4);
        {
            std::lock_guard lk(src_->lastM);
            src_->lastSent = e.v;
        }
        src_->sent->lastW = w; src_->sent->lastH = h; src_->sent->fpsNum = fpsNum; src_->sent->fpsDen = fpsDen;
        ++src_->sent->videoFrames;
        src_->publish(e);
        return true;
    }
    bool sendAudio(const float* in, int frames, int rate, int channels) override {
        NetEvent e; e.kind = LiveInput::Event::Audio;
        e.a.rate = rate; e.a.channels = channels;
        e.a.data.assign(in, in + size_t(frames) * size_t(channels));
        src_->sent->audioFrames += uint64_t(frames);
        src_->sent->audioRate = rate; src_->sent->audioChannels = channels;
        src_->publish(e);
        return true;
    }
    int connections() const override { return int(src_->subCount()); }
private:
    std::shared_ptr<FakeNdiBackend::Net> net_;
    std::shared_ptr<FakeSource> src_;
};
} // namespace

FakeNdiBackend::FakeNdiBackend() : net_(std::make_shared<Net>()) {}

FakeNdiBackend::~FakeNdiBackend() {
    std::map<QString, std::shared_ptr<FakeSource>> all;
    {
        std::lock_guard lk(net_->m);
        all.swap(net_->sources);
    }
    for (auto& [n, s] : all) {
        s->stop = true;
        if (s->th.joinable()) s->th.join();
    }
}

std::vector<NdiSourceInfo> FakeNdiBackend::listSources(int) {
    std::vector<NdiSourceInfo> v;
    std::lock_guard lk(net_->m);
    for (auto& [n, s] : net_->sources) v.push_back({n, QString("fake://%1").arg(n)});
    return v;
}

LiveOpenResult FakeNdiBackend::openReceiver(const QString& name, const QString&) {
    LiveOpenResult r;
    if (unavailable_) { r.unavailable = true; r.error = unavailableReason(); return r; }
    r.input = std::make_unique<FakeInput>(net_, name);
    return r;
}

std::unique_ptr<NdiSender> FakeNdiBackend::createSender(const QString& name, QString* err) {
    if (unavailable_) { if (err) *err = unavailableReason(); return nullptr; }
    auto s = std::make_shared<FakeSource>();
    s->name = name;
    s->external = true;
    s->sent = std::make_shared<SentStats>();
    {
        std::lock_guard lk(net_->m);
        if (net_->sources.count(name)) { if (err) *err = "Trùng tên nguồn NDI: " + name; return nullptr; }
        net_->sources[name] = s;
    }
    return std::make_unique<FakeSender>(net_, s);
}

void FakeNdiBackend::addSource(const QString& name, SenderCfg cfg) {
    auto s = std::make_shared<FakeSource>();
    s->name = name;
    s->cfg = cfg;
    s->ppm = cfg.audioPpm;
    {
        std::lock_guard lk(net_->m);
        net_->sources[name] = s;
    }
    s->th = std::thread([s] { s->producer(); });
}

void FakeNdiBackend::removeSource(const QString& name) {
    std::shared_ptr<FakeSource> s;
    {
        std::lock_guard lk(net_->m);
        auto it = net_->sources.find(name);
        if (it == net_->sources.end()) return;
        s = it->second;
        net_->sources.erase(it);
    }
    s->stop = true;
    if (s->th.joinable()) s->th.join();
}

void FakeNdiBackend::setSourceAlive(const QString& name, bool alive) { if (auto s = net_->find(name)) s->alive = alive; }
void FakeNdiBackend::setSourceAudioPpm(const QString& name, double ppm) { if (auto s = net_->find(name)) s->ppm = ppm; }
bool FakeNdiBackend::hasSource(const QString& name) const { return net_->find(name) != nullptr; }
std::shared_ptr<FakeNdiBackend::SentStats> FakeNdiBackend::senderStats(const QString& name) const {
    auto s = net_->find(name);
    return s ? s->sent : nullptr;
}
bool FakeNdiBackend::lastSentFrame(const QString& name, LiveVideoFrame& out) const {
    auto s = net_->find(name);
    if (!s) return false;
    std::lock_guard lk(s->lastM);
    if (s->lastSent.rgba.empty()) return false;
    out = s->lastSent;
    return true;
}
size_t FakeNdiBackend::receiverCount(const QString& name) const {
    auto s = net_->find(name);
    return s ? s->subCount() : 0;
}

} // namespace lc
