#include "live/WindowCapture.h"
#include "core/I18n.h"
#include "live/LiveSource.h"
#include "live/WindowCaptureFake.h"
#include <QMutex>
#include <algorithm>
#include <climits>
#include <cmath>

namespace lc {

bool WindowTarget::isSlideShow() const { return app == QLatin1String(kSlideShowTarget); }

bool WindowTarget::operator==(const WindowTarget& o) const {
    return app.compare(o.app, Qt::CaseInsensitive) == 0 && windowClass == o.windowClass && title == o.title;
}

int findCaptureWindow(const std::vector<CaptureWindow>& windows, const WindowTarget& t) {
    int only = -1, count = 0;
    for (int i = 0; i < int(windows.size()); ++i) {
        const CaptureWindow& w = windows[size_t(i)];
        if (t.isSlideShow()) {
            if (w.slideShow) return i;
            continue;
        }
        if (w.app.compare(t.app, Qt::CaseInsensitive) != 0 || w.windowClass != t.windowClass) continue;
        if (w.title == t.title) return i;
        only = i;
        ++count;
    }
    return count == 1 ? only : -1;
}

QRect fitAspectRect(const QRect& area, double aspect) {
    if (!std::isfinite(aspect) || aspect <= 0 || area.width() <= 0 || area.height() <= 0) return area;
    int w = area.width(), h = int(std::lround(w / aspect));
    if (h > area.height()) {
        h = area.height();
        w = int(std::lround(h * aspect));
    }
    w = std::clamp(w, 1, area.width());
    h = std::clamp(h, 1, area.height());
    return QRect(area.x() + (area.width() - w) / 2, area.y() + (area.height() - h) / 2, w, h);
}

namespace {
QMutex g_mu;
auto& g_backend = *new std::shared_ptr<WindowCaptureBackend>;      // khong huy khi thoat (an toan thu tu huy)
auto& g_override = *new std::shared_ptr<WindowCaptureBackend>;

std::shared_ptr<WindowCaptureBackend> makeDefault() {
    if (qgetenv("LUMACAST_WINDOW_BACKEND") == "fake") {
        auto f = std::make_shared<FakeWindowCapture>();
        f->addWindow({1, "POWERPNT.EXE", "PPTFrameClass", "PowerPoint Slide Show - Mau", true}, 1280, 720);
        f->addWindow({2, "notepad.exe", "Notepad", "Ghi chu - Notepad", false}, 640, 480);
        return f;
    }
#ifdef _WIN32
    if (auto b = createWgcWindowCapture()) return b;
#endif
    return createUnavailableWindowCapture(LC_TR("Bắt cửa sổ cần Windows 10 bản 1903 trở lên"));
}
} // namespace

std::shared_ptr<WindowCaptureBackend> windowCaptureBackend() {
    QMutexLocker lk(&g_mu);
    if (g_override) return g_override;
    if (!g_backend) g_backend = makeDefault();
    return g_backend;
}

void setWindowCaptureBackend(std::shared_ptr<WindowCaptureBackend> b) {
    QMutexLocker lk(&g_mu);
    g_override = std::move(b);
}

std::shared_ptr<WindowCaptureBackend> createUnavailableWindowCapture(const QString& reason) {
    class None : public WindowCaptureBackend {
    public:
        explicit None(QString r) : r_(std::move(r)) {}
        QString id() const override { return "none"; }
        bool available() const override { return false; }
        QString unavailableReason() const override { return r_; }
        std::vector<CaptureWindow> list() override { return {}; }
        LiveOpenResult open(const WindowTarget&) override { LiveOpenResult r; r.unavailable = true; r.error = r_; return r; }
    private:
        QString r_;
    };
    return std::make_shared<None>(reason);
}

std::shared_ptr<LiveSource> makeWindowSource(const WindowTarget& target, const QString& label) {
    auto factory = [target]() -> LiveOpenResult {
        auto b = windowCaptureBackend();
        if (!b->available()) { LiveOpenResult r; r.unavailable = true; r.error = b->unavailableReason(); return r; }
        return b->open(target);
    };
    LiveOptions o;
    o.audio = false;                 // tieng cua chuong trinh do di thang ra thiet bi mac dinh cua Windows, khong qua mixer
    o.stallMs = INT_MAX / 2;         // slide dung yen = khong co khung moi la BINH THUONG; mat cua so do backend bao
    o.retryMs = 300;
    o.retryMaxMs = 1000;
    o.unavailableRetryMs = 700;      // dang cho slideshow mo: noi nhanh sau khi bam F5
    QString name = label;
    if (name.isEmpty())
        name = target.isSlideShow() ? QString(LC_TR("Slide PowerPoint"))
                                    : QString(LC_TR("Cửa sổ: %1")).arg(target.title.isEmpty() ? target.app : target.title);
    return std::make_shared<LiveSource>(name, "window", std::move(factory), o);
}

} // namespace lc
