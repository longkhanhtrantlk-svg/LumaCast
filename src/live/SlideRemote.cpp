#include "live/SlideRemote.h"
#include "core/I18n.h"
#include <QMutex>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace lc {

namespace {
QMutex g_mu;
auto& g_backend = *new std::shared_ptr<SlideBackend>;
auto& g_override = *new std::shared_ptr<SlideBackend>;

class NoSlides : public SlideBackend {
public:
    SlideStep step(int) override {
        SlideStep r;
        r.error = LC_TR("Điều khiển slide chỉ có trên Windows có cài PowerPoint");
        return r;
    }
};
} // namespace

std::shared_ptr<SlideBackend> slideBackend() {
    QMutexLocker lk(&g_mu);
    if (g_override) return g_override;
    if (!g_backend) {
#ifdef _WIN32
        g_backend = createPowerPointBackend();
#else
        g_backend = std::make_shared<NoSlides>();
#endif
    }
    return g_backend;
}

void setSlideBackend(std::shared_ptr<SlideBackend> b) {
    QMutexLocker lk(&g_mu);
    g_override = std::move(b);
}

struct SlideRemote::State {
    std::mutex m;
    std::condition_variable cv;
    std::deque<int> queue;
    SlideRemote* owner = nullptr;
    bool stop = false;
    bool started = false;       // thread chi tao khi dung lan dau
};

SlideRemote::SlideRemote(QObject* parent) : QObject(parent), s_(std::make_shared<State>()) {
    s_->owner = this;
}

SlideRemote::~SlideRemote() {
    std::lock_guard lk(s_->m);
    s_->owner = nullptr;
    s_->stop = true;
    s_->cv.notify_all();
}

void SlideRemote::step(int direction) {
    std::lock_guard lk(s_->m);
    if (s_->queue.size() >= 3) return;                 // PowerPoint dang treo: khong don lenh de sau do nhay mot loat slide
    s_->queue.push_back(direction);
    s_->cv.notify_one();
    if (s_->started) return;
    s_->started = true;
    // tach rieng (detach): thoat app khong phai cho COM
    std::thread([s = s_] {
        for (;;) {
            int dir = 0;
            {
                std::unique_lock lk(s->m);
                s->cv.wait(lk, [&] { return s->stop || !s->queue.empty(); });
                if (s->stop) return;
                dir = s->queue.front();
                s->queue.pop_front();
            }
            const SlideStep r = slideBackend()->step(dir);
            std::lock_guard lk(s->m);
            if (!s->owner) return;
            SlideRemote* o = s->owner;
            QMetaObject::invokeMethod(o, [o, dir, r] { emit o->stepped(dir, r.ok, r.position, r.count, r.error); }, Qt::QueuedConnection);
        }
    }).detach();
}

} // namespace lc
