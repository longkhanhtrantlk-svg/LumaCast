#include "live/WindowCaptureFake.h"
#include "core/I18n.h"
#include <chrono>
#include <mutex>
#include <thread>

namespace lc {

struct FakeWindowCapture::State {
    struct Win {
        CaptureWindow info;
        int w = 0, h = 0;
        quint32 rgb = 0x204080;
        uint64_t version = 1;
        bool minimized = false;
    };
    std::mutex m;
    std::vector<Win> wins;
    Win* find(quint64 handle) {
        for (auto& w : wins) if (w.info.handle == handle) return &w;
        return nullptr;
    }
};

namespace {
class FakeInput : public LiveInput {
public:
    FakeInput(std::shared_ptr<FakeWindowCapture::State> s, quint64 handle) : s_(std::move(s)), handle_(handle) {}
    Event capture(int timeoutMs, LiveVideoFrame& v, LiveAudioBlock&) override {
        {
            std::lock_guard lk(s_->m);
            auto* w = s_->find(handle_);
            if (!w || w->minimized) return Event::Lost;
            if (w->version != sent_) {
                sent_ = w->version;
                v.w = w->w;
                v.h = w->h;
                v.rgba.resize(size_t(w->w) * size_t(w->h) * 4);
                for (size_t i = 0; i < v.rgba.size(); i += 4) {
                    v.rgba[i] = uint8_t(w->rgb >> 16);
                    v.rgba[i + 1] = uint8_t(w->rgb >> 8);
                    v.rgba[i + 2] = uint8_t(w->rgb);
                    v.rgba[i + 3] = 255;
                }
                return Event::Video;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(timeoutMs, 5)));
        return Event::None;
    }
private:
    std::shared_ptr<FakeWindowCapture::State> s_;
    quint64 handle_;
    uint64_t sent_ = 0;
};
} // namespace

FakeWindowCapture::FakeWindowCapture() : s_(std::make_shared<State>()) {}

std::vector<CaptureWindow> FakeWindowCapture::list() {
    std::lock_guard lk(s_->m);
    std::vector<CaptureWindow> out;
    for (const auto& w : s_->wins) out.push_back(w.info);
    return out;
}

LiveOpenResult FakeWindowCapture::open(const WindowTarget& target) {
    LiveOpenResult r;
    const std::vector<CaptureWindow> wins = list();
    const int i = findCaptureWindow(wins, target);
    if (i < 0) {
        r.unavailable = true;
        r.error = target.isSlideShow() ? LC_TR("Đang chờ slideshow PowerPoint") : LC_TR("Chưa thấy cửa sổ cần bắt");
        return r;
    }
    std::lock_guard lk(s_->m);
    auto* w = s_->find(wins[size_t(i)].handle);
    if (!w) { r.error = LC_TR("Chưa thấy cửa sổ cần bắt"); return r; }
    if (w->minimized) { r.error = LC_TR("Cửa sổ đang thu nhỏ — mở lại cửa sổ đó"); return r; }
    r.input = std::make_unique<FakeInput>(s_, w->info.handle);
    return r;
}

void FakeWindowCapture::addWindow(const CaptureWindow& info, int width, int height) {
    std::lock_guard lk(s_->m);
    State::Win w;
    w.info = info;
    w.w = width;
    w.h = height;
    s_->wins.push_back(w);
}

void FakeWindowCapture::closeWindow(quint64 handle) {
    std::lock_guard lk(s_->m);
    std::erase_if(s_->wins, [handle](const State::Win& w) { return w.info.handle == handle; });
}

void FakeWindowCapture::setColor(quint64 handle, quint32 rgb) {
    std::lock_guard lk(s_->m);
    if (auto* w = s_->find(handle)) { w->rgb = rgb; ++w->version; }
}

void FakeWindowCapture::setMinimized(quint64 handle, bool on) {
    std::lock_guard lk(s_->m);
    if (auto* w = s_->find(handle)) { w->minimized = on; if (!on) ++w->version; }
}

} // namespace lc
