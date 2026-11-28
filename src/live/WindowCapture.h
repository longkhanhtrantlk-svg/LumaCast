#pragma once
#include "live/LiveInput.h"
#include <QRect>
#include <QString>
#include <memory>
#include <vector>

namespace lc {

class LiveSource;

// Bat hinh MOT cua so cua chuong trinh khac (SRC-09), chu yeu la slideshow PowerPoint chay "trong cua so" tren cung may.
// Cua so co the bi che, nhung khong duoc thu nho (Windows khong ve cua so thu nho).

struct CaptureWindow {
    quint64 handle = 0;          // HWND (chi co gia tri trong phien nay, KHONG luu vao project)
    QString app;                 // ten file chay, vd "POWERPNT.EXE"
    QString windowClass;
    QString title;
    bool slideShow = false;      // cua so slideshow PowerPoint (toan man hinh hoac trong cua so)
};

// Muc tieu luu trong project: du de tim lai cua so o lan mo sau.
struct WindowTarget {
    QString app;                 // ten file chay, hoac kSlideShowTarget
    QString windowClass;
    QString title;
    bool isSlideShow() const;
    bool operator==(const WindowTarget& o) const;
};

// Muc tieu dac biet: bat ky cua so slideshow PowerPoint nao, ke ca slideshow mo SAU khi them nguon.
inline constexpr const char* kSlideShowTarget = "powerpoint-slideshow";

// Chi so cua so khop muc tieu, -1 neu khong co. Slideshow: cua so slideshow dau tien. Con lai: cung app + lop cua so; uu tien
// dung tieu de, khong thi chi nhan khi chi co MOT cua so nhu vay (tranh bat nham cua so khac len man LED).
int findCaptureWindow(const std::vector<CaptureWindow>& windows, const WindowTarget& target);

// Hinh chu nhat lon nhat co ti le `aspect` (rong/cao) nam giua `area` (bo vien den quanh slide). aspect <= 0: tra ve area.
QRect fitAspectRect(const QRect& area, double aspect);

class WindowCaptureBackend {
public:
    virtual ~WindowCaptureBackend() = default;
    virtual QString id() const = 0;                     // "wgc" | "fake" | "none"
    virtual bool available() const = 0;
    virtual QString unavailableReason() const = 0;
    virtual std::vector<CaptureWindow> list() = 0;       // cua so bat duoc (khong gom cua so cua chinh LumaCast)
    virtual LiveOpenResult open(const WindowTarget& target) = 0;
};

std::shared_ptr<WindowCaptureBackend> windowCaptureBackend();
void setWindowCaptureBackend(std::shared_ptr<WindowCaptureBackend> b);       // test; nullptr = mac dinh
std::shared_ptr<LiveSource> makeWindowSource(const WindowTarget& target, const QString& label = {});

std::shared_ptr<WindowCaptureBackend> createUnavailableWindowCapture(const QString& reason);
#ifdef _WIN32
std::shared_ptr<WindowCaptureBackend> createWgcWindowCapture();      // Windows Graphics Capture; nullptr neu he dieu hanh khong ho tro
#endif

} // namespace lc
