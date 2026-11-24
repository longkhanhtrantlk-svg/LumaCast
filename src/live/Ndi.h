#pragma once
#include "live/LiveInput.h"
#include <QString>
#include <memory>
#include <vector>

namespace lc {

// NDI qua lop truu tuong: that (nap dong runtime) hoac gia (test, khong can NDI). Xem docs/KienTruc.md muc 15.
// KHONG commit/khong lien ket header hay thu vien cua NDI SDK: runtime duoc nap luc chay bang QLibrary.
// NDI® la thuong hieu cua Vizrt NDI AB — xem docs/SPEC.md muc 5 ve dieu khoan.

struct NdiSourceInfo {
    QString name;        // "MAY (Ten nguon)" nhu NDI bao
    QString url;         // dia chi (co the rong)
    bool operator==(const NdiSourceInfo& o) const { return name == o.name && url == o.url; }
};

// Mot nguoi gui (NDI ra). Moi ham goi tu MOT thread (thread gui cua NdiOutputSink).
class NdiSender {
public:
    virtual ~NdiSender() = default;
    // rgba: hang dau = tren cung; stride tinh theo byte. Chan den khi da sao chep/gui xong (dong bo).
    virtual bool sendVideo(const uint8_t* rgba, int w, int h, int stride, int fpsNum, int fpsDen) = 0;
    // interleaved float32, `channels` kenh, `rate` Hz.
    virtual bool sendAudio(const float* interleaved, int frames, int rate, int channels) = 0;
    virtual int connections() const { return 0; }      // so nguoi nhan dang noi (-1 = khong biet)
};

class NdiBackend {
public:
    virtual ~NdiBackend() = default;
    virtual QString id() const = 0;                    // "real" | "fake" | "none"
    virtual bool available() const = 0;
    virtual QString unavailableReason() const = 0;     // tieng Viet; rong neu available()
    virtual QString version() const { return {}; }
    // Liet ke nguon tren mang; cho toi da waitMs de phat hien. Goi tu thread bat ky (khong phai render thread).
    virtual std::vector<NdiSourceInfo> listSources(int waitMs) = 0;
    // Tao ket noi nhan. Luon tra input neu backend san sang (nguon chua co thi cho den khi co, nhu NDI that).
    virtual LiveOpenResult openReceiver(const QString& name, const QString& url) = 0;
    virtual std::unique_ptr<NdiSender> createSender(const QString& name, QString* err) = 0;
};

// Backend dang dung. Mac dinh: runtime that neu nap duoc, nguoc lai backend "none" (bao "Chưa cài NDI Runtime").
// Bien moi truong LUMACAST_NDI_BACKEND=fake chon backend gia (dev/demo tren may khong co NDI); LUMACAST_NDI_DISABLE=1 tat han NDI.
std::shared_ptr<NdiBackend> ndiBackend();
// true neu file thuc thi co ma NDI (CMake LUMACAST_ENABLE_NDI). false: giao dien an moi muc NDI.
constexpr bool ndiCompiledIn() {
#ifdef LUMACAST_NO_NDI
    return false;
#else
    return true;
#endif
}
void setNdiBackend(std::shared_ptr<NdiBackend> b);     // test: dat backend gia; nullptr = quay lai mac dinh

// Nguon NDI de dua vao Project / Media Pool.
std::shared_ptr<class LiveSource> makeNdiSource(const QString& name, const QString& url, const QString& label = {});

// Runtime that (NdiRuntime.cpp). Tra nullptr neu khong nap duoc; reason nhan ly do.
std::shared_ptr<NdiBackend> createRealNdiBackend(QString* reason);
// Backend "none": luon khong san sang voi ly do da cho.
std::shared_ptr<NdiBackend> createUnavailableNdiBackend(const QString& reason);

} // namespace lc
