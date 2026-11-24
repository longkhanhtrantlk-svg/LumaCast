#include "CameraFake.h"
#include "live/LiveSource.h"
#include <chrono>
#include <cmath>
#include <thread>

namespace lc {

namespace {
class FakeCamInput : public LiveInput {
public:
    FakeCamInput(std::shared_ptr<FakeCameraBackend::Dev> d, CameraFormat f) : d_(std::move(d)), f_(f), next_(std::chrono::steady_clock::now()) {}
    Event capture(int timeoutMs, LiveVideoFrame& v, LiveAudioBlock&) override {
        using namespace std::chrono;
        if (!d_->present) {                                   // thiet bi bi rut: nhu Media Foundation bao loi doc mau
            std::this_thread::sleep_for(milliseconds(std::min(timeoutMs, 10)));
            return Event::Lost;
        }
        const auto now = steady_clock::now();
        if (now < next_) {
            const auto wait = std::min<steady_clock::duration>(next_ - now, milliseconds(timeoutMs));
            std::this_thread::sleep_for(wait);
            if (steady_clock::now() < next_) return Event::None;
        }
        next_ += duration_cast<steady_clock::duration>(duration<double>(1.0 / std::max(1.0, f_.fps)));
        if (steady_clock::now() > next_ + milliseconds(200)) next_ = steady_clock::now();
        v.w = f_.w; v.h = f_.h;
        v.rgba.assign(size_t(f_.w) * size_t(f_.h) * 4, 255);
        const int bar = int((n_++ * 6) % uint64_t(std::max(1, f_.w)));
        for (int y = 0; y < f_.h; ++y)
            for (int x = 0; x < f_.w; ++x) {
                uint8_t* p = &v.rgba[(size_t(y) * size_t(f_.w) + size_t(x)) * 4];
                p[0] = uint8_t(x * 255 / std::max(1, f_.w - 1));
                p[1] = uint8_t(y * 255 / std::max(1, f_.h - 1));
                p[2] = std::abs(x - bar) < std::max(2, f_.w / 32) ? 255 : 40;
            }
        return Event::Video;
    }
    QString info() const override { return d_->name + " " + f_.text(); }
private:
    std::shared_ptr<FakeCameraBackend::Dev> d_;
    CameraFormat f_;
    std::chrono::steady_clock::time_point next_;
    uint64_t n_ = 0;
};
} // namespace

std::vector<CameraDevice> FakeCameraBackend::list() {
    std::lock_guard lk(m_);
    std::vector<CameraDevice> v;
    for (auto& [id, d] : devs_) if (d->present) v.push_back({d->id, d->name});
    return v;
}

std::vector<CameraFormat> FakeCameraBackend::formats(const QString& id) {
    std::lock_guard lk(m_);
    auto it = devs_.find(id);
    return it != devs_.end() && it->second->present ? it->second->formats : std::vector<CameraFormat>{};
}

LiveOpenResult FakeCameraBackend::open(const QString& id, const QString& name, CameraFormat want) {
    LiveOpenResult r;
    std::shared_ptr<Dev> d;
    {
        std::lock_guard lk(m_);
        auto it = devs_.find(id);
        if (it != devs_.end() && it->second->present) d = it->second;
        else for (auto& [k, x] : devs_) if (x->present && !name.isEmpty() && x->name == name) d = x;     // id doi nhung ten con: nhan ra lai
    }
    ++opens_;
    if (!d) { r.error = "Không thấy camera: " + (name.isEmpty() ? id : name); return r; }
    if (d->busy) { r.error = "Camera đang được chương trình khác sử dụng: " + d->name; return r; }
    CameraFormat best;
    double bestScore = 1e18;
    for (const CameraFormat& f : d->formats) {
        const double s = (want.w ? std::abs(f.w - want.w) * 10.0 : 0) + (want.h ? std::abs(f.h - want.h) * 10.0 : 0) + (want.fps > 0 ? std::abs(f.fps - want.fps) : 0);
        if (s < bestScore) { bestScore = s; best = f; }
    }
    if (best.w == 0) best = {640, 480, 30};
    { std::lock_guard lk(m_); lastFmt_ = best; }
    r.input = std::make_unique<FakeCamInput>(d, best);
    return r;
}

void FakeCameraBackend::addDevice(const QString& id, const QString& name, std::vector<CameraFormat> formats) {
    auto d = std::make_shared<Dev>();
    d->id = id; d->name = name; d->formats = std::move(formats);
    std::lock_guard lk(m_);
    devs_[id] = d;
}

void FakeCameraBackend::removeDevice(const QString& id) {
    std::lock_guard lk(m_);
    auto it = devs_.find(id);
    if (it != devs_.end()) it->second->present = false;     // giu doi tuong: input dang mo thay present=false -> Lost
}

void FakeCameraBackend::setBusy(const QString& id, bool busy) {
    std::lock_guard lk(m_);
    auto it = devs_.find(id);
    if (it != devs_.end()) it->second->busy = busy;
}

} // namespace lc
