#pragma once
#include "live/Camera.h"
#include <atomic>
#include <map>
#include <mutex>

namespace lc {

// Camera GIA cho test/demo (LUMACAST_CAMERA_BACKEND=fake): thiet bi them/rut bang tay; khung hinh sinh khi capture() theo fps cua che do.
class FakeCameraBackend : public CameraBackend {
public:
    QString id() const override { return "fake"; }
    bool available() const override { return true; }
    QString unavailableReason() const override { return {}; }
    std::vector<CameraDevice> list() override;
    std::vector<CameraFormat> formats(const QString& deviceId) override;
    LiveOpenResult open(const QString& deviceId, const QString& deviceName, CameraFormat want) override;

    void addDevice(const QString& id, const QString& name, std::vector<CameraFormat> formats);
    void removeDevice(const QString& id);              // rut thiet bi: ket noi dang mo -> Lost, list() khong con
    void setBusy(const QString& id, bool busy);        // thiet bi dang bi chuong trinh khac dung: open() that bai
    uint64_t openCount() const { return opens_; }
    CameraFormat lastOpenedFormat() const { std::lock_guard lk(m_); return lastFmt_; }

    struct Dev {
        QString id, name;
        std::vector<CameraFormat> formats;
        std::atomic<bool> present{true}, busy{false};
    };
private:
    mutable std::mutex m_;
    std::map<QString, std::shared_ptr<Dev>> devs_;
    std::atomic<uint64_t> opens_{0};
    CameraFormat lastFmt_;
};

} // namespace lc
