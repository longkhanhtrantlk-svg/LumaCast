#pragma once
#include "live/LiveInput.h"
#include <QString>
#include <memory>
#include <vector>

namespace lc {

struct CameraFormat {
    int w = 0, h = 0;
    double fps = 0;
    bool operator==(const CameraFormat& o) const { return w == o.w && h == o.h && std::abs(fps - o.fps) < 0.01; }
    QString text() const { return QString("%1x%2 @ %3 fps").arg(w).arg(h).arg(fps, 0, 'f', fps == int(fps) ? 0 : 2); }
};

struct CameraDevice {
    QString id;        // dinh danh ben vung cua he dieu hanh (Windows: symbolic link) — luu trong project
    QString name;      // ten hien thi (luu kem de nhan ra lai neu id doi)
};

// Camera/card capture qua lop truu tuong: Media Foundation (Windows), Gia (test). Linux/macOS: chua co (backend "none").
class CameraBackend {
public:
    virtual ~CameraBackend() = default;
    virtual QString id() const = 0;                     // "mf" | "fake" | "none"
    virtual bool available() const = 0;
    virtual QString unavailableReason() const = 0;
    virtual std::vector<CameraDevice> list() = 0;       // thread bat ky (khong phai render thread)
    virtual std::vector<CameraFormat> formats(const QString& deviceId) = 0;   // cac che do nguoi dung chon (doi tuong khong nhan duoc -> rong)
    // Mo thiet bi voi che do gan nhat `want` (w,h,fps = 0 -> de thiet bi chon). Khong thay theo id thi thu theo ten.
    virtual LiveOpenResult open(const QString& deviceId, const QString& deviceName, CameraFormat want) = 0;
};

std::shared_ptr<CameraBackend> cameraBackend();
void setCameraBackend(std::shared_ptr<CameraBackend> b);       // test; nullptr = mac dinh
std::shared_ptr<class LiveSource> makeCameraSource(const QString& deviceId, const QString& deviceName, CameraFormat fmt, const QString& label = {});

std::shared_ptr<CameraBackend> createUnavailableCameraBackend(const QString& reason);
#ifdef _WIN32
std::shared_ptr<CameraBackend> createMediaFoundationCamera();
#endif

} // namespace lc
