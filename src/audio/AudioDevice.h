#pragma once
#include <QString>
#include "core/I18n.h"
#include <functional>
#include <memory>
#include <vector>

namespace lc {

struct AudioDeviceInfo {
    QString id;            // dinh danh ben vung cua he dieu hanh ("" = thiet bi mac dinh; "null" = backend Null)
    QString name;          // ten hien thi
    bool isDefault = false;
};

// Dau ra am thanh: lop truu tuong. Backend: WASAPI (Windows), Null (thu nghiem / nen tang chua ho tro).
// Callback chay tren thread cua thiet bi; KHONG duoc cap phat bo nho hay khoa mutex trong do.
class AudioDevice {
public:
    // out: `frames` khung float32 stereo xen ke o kAudioRate. Phai dien DAY du lieu (im lang = 0).
    using RenderFn = std::function<void(float* out, int frames)>;
    virtual ~AudioDevice() = default;
    virtual bool start(RenderFn fn, QString* err = nullptr) = 0;
    virtual void stop() = 0;
    virtual QString name() const = 0;
    virtual double latencySeconds() const = 0;     // do tre bo dem cua thiet bi (giay)
};

// Liet ke thiet bi ra. Luon co muc dau tien la thiet bi mac dinh (id rong).
std::vector<AudioDeviceInfo> listAudioDevices();
// Tao thiet bi theo id ("" = mac dinh; khong thay id -> thu mac dinh, ghi canh bao). Khong bao gio tra nullptr: backend Null la phuong an cuoi.
// id == "null" luon cho backend Null.
std::unique_ptr<AudioDevice> createAudioDevice(const QString& id, QString* err = nullptr);

// Backend Null: mot thread "tieu thu" mau theo dong ho thuc (nhu thiet bi that) de mixer va dong ho am thanh chay nhu that.
// Dung cho test va cho Linux/macOS truoc khi co backend that. Co the dem so khung da tieu thu.
class NullAudioDevice : public AudioDevice {
public:
    NullAudioDevice();
    ~NullAudioDevice() override;
    bool start(RenderFn fn, QString* err = nullptr) override;
    void stop() override;
    QString name() const override { return LC_TR("Null (không phát ra loa)"); }
    double latencySeconds() const override { return 0.02; }
    uint64_t consumedFrames() const;
private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

#ifdef _WIN32
std::vector<AudioDeviceInfo> listWasapiDevices();
std::unique_ptr<AudioDevice> createWasapiDevice(const QString& id, QString* err);
#endif

} // namespace lc
