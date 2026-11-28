#include "live/Camera.h"
#include "core/I18n.h"
#include "live/CameraFake.h"
#include "live/LiveSource.h"
#include <QMutex>

namespace lc {

namespace {
QMutex g_mu;
auto& g_backend = *new std::shared_ptr<CameraBackend>;      // khong huy khi thoat (an toan thu tu huy)
auto& g_override = *new std::shared_ptr<CameraBackend>;

std::shared_ptr<CameraBackend> makeDefault() {
    if (qgetenv("LUMACAST_CAMERA_BACKEND") == "fake") {
        auto f = std::make_shared<FakeCameraBackend>();
        f->addDevice("fake:cam1", LC_TR("Camera mẫu 1"), {{1280, 720, 30}, {640, 480, 30}, {1920, 1080, 30}});
        f->addDevice("fake:cam2", LC_TR("Camera mẫu 2"), {{640, 480, 15}});
        return f;
    }
#ifdef _WIN32
    if (auto b = createMediaFoundationCamera()) return b;
#endif
    return createUnavailableCameraBackend(LC_TR("Camera chưa được hỗ trợ trên nền tảng này (hiện chỉ Windows qua Media Foundation)"));
}
} // namespace

std::shared_ptr<CameraBackend> cameraBackend() {
    QMutexLocker lk(&g_mu);
    if (g_override) return g_override;
    if (!g_backend) g_backend = makeDefault();
    return g_backend;
}

void setCameraBackend(std::shared_ptr<CameraBackend> b) {
    QMutexLocker lk(&g_mu);
    g_override = std::move(b);
}

std::shared_ptr<CameraBackend> createUnavailableCameraBackend(const QString& reason) {
    class None : public CameraBackend {
    public:
        explicit None(QString r) : r_(std::move(r)) {}
        QString id() const override { return "none"; }
        bool available() const override { return false; }
        QString unavailableReason() const override { return r_; }
        std::vector<CameraDevice> list() override { return {}; }
        std::vector<CameraFormat> formats(const QString&) override { return {}; }
        LiveOpenResult open(const QString&, const QString&, CameraFormat) override { LiveOpenResult r; r.unavailable = true; r.error = r_; return r; }
    private:
        QString r_;
    };
    return std::make_shared<None>(reason);
}

std::shared_ptr<LiveSource> makeCameraSource(const QString& deviceId, const QString& deviceName, CameraFormat fmt, const QString& label) {
    auto factory = [deviceId, deviceName, fmt]() -> LiveOpenResult {
        auto b = cameraBackend();
        if (!b->available()) { LiveOpenResult r; r.unavailable = true; r.error = b->unavailableReason(); return r; }
        return b->open(deviceId, deviceName, fmt);
    };
    LiveOptions o;
    o.audio = false;                 // camera: chi hinh (xem KienTruc muc 15: am thanh capture de sau)
    o.stallMs = 2500;
    return std::make_shared<LiveSource>(label.isEmpty() ? "Camera: " + (deviceName.isEmpty() ? deviceId : deviceName) : label, "camera", std::move(factory), o);
}

} // namespace lc
