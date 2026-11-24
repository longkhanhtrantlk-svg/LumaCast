// Backend WASAPI (Windows): che do shared, event-driven, dinh dang yeu cau float32 stereo 48 kHz voi AUTOCONVERTPCM
// (Windows tu doi tan so/so kenh cho thiet bi that) -> khong can resampler rieng. Chi dung Win32/COM, khong dung Qt Multimedia.
#ifdef _WIN32
#include "AudioDevice.h"
#include "AudioStream.h"
#include <QDebug>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <propidl.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace lc {
namespace {

// GUID cong khai, ghi truc tiep de khong phu thuoc vao thu vien .lib chua IID (MSVC va MinGW deu dung duoc).
const GUID kClsidEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const GUID kIidEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const GUID kIidAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
const GUID kIidRenderClient = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
const PROPERTYKEY kPkeyFriendlyName = {{0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};

template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    ~Com() { reset(); }
    void reset() { if (p) { p->Release(); p = nullptr; } }
    T** out() { reset(); return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

struct ComInit {
    bool ok;
    ComInit() { const HRESULT h = CoInitializeEx(nullptr, COINIT_MULTITHREADED); ok = SUCCEEDED(h); }   // RPC_E_CHANGED_MODE: da co COM, khong CoUninitialize
    ~ComInit() { if (ok) CoUninitialize(); }
};

QString hrText(const char* what, HRESULT hr) { return QString("%1 (0x%2)").arg(what).arg(quint32(hr), 8, 16, QChar('0')); }

bool getEnumerator(Com<IMMDeviceEnumerator>& e) {
    return SUCCEEDED(CoCreateInstance(kClsidEnumerator, nullptr, CLSCTX_ALL, kIidEnumerator, reinterpret_cast<void**>(e.out())));
}

QString deviceId(IMMDevice* d) {
    LPWSTR w = nullptr;
    QString s;
    if (SUCCEEDED(d->GetId(&w)) && w) { s = QString::fromWCharArray(w); CoTaskMemFree(w); }
    return s;
}

QString deviceName(IMMDevice* d) {
    Com<IPropertyStore> ps;
    QString s;
    if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, ps.out()))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(ps->GetValue(kPkeyFriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal) s = QString::fromWCharArray(v.pwszVal);
        PropVariantClear(&v);
    }
    return s;
}

class WasapiDevice : public AudioDevice {
public:
    explicit WasapiDevice(QString id) : id_(std::move(id)) {}
    ~WasapiDevice() override { stop(); }

    bool start(RenderFn fn, QString* err) override {
        stop();
        fn_ = std::move(fn);
        stop_ = false;
        ready_ = false;
        initErr_.clear();
        th_ = std::thread([this] { run(); });
        std::unique_lock lk(m_);
        cv_.wait_for(lk, std::chrono::seconds(5), [this] { return ready_; });
        if (!initErr_.isEmpty()) {
            if (err) *err = initErr_;
            lk.unlock();
            stop();
            return false;
        }
        return true;
    }
    void stop() override {
        stop_ = true;
        if (th_.joinable()) th_.join();
    }
    QString name() const override { std::lock_guard lk(m_); return name_; }
    double latencySeconds() const override { return latency_; }

private:
    void signalReady(const QString& err) {
        std::lock_guard lk(m_);
        initErr_ = err;
        ready_ = true;
        cv_.notify_all();
    }

    // Mo thiet bi + chay vong lap phat cho den khi stop_. Tra ve false neu khong mo duoc (loi dua vao err), true neu ket thuc binh thuong
    // hoac thiet bi mat (invalidated) -> caller thu mo lai.
    bool openAndPlay(QString& err, bool& invalidated) {
        invalidated = false;
        Com<IMMDeviceEnumerator> en;
        if (!getEnumerator(en)) { err = "Không tạo được bộ liệt kê thiết bị âm thanh"; return false; }
        Com<IMMDevice> dev;
        HRESULT hr;
        if (id_.isEmpty()) hr = en->GetDefaultAudioEndpoint(eRender, eConsole, dev.out());
        else hr = en->GetDevice(reinterpret_cast<LPCWSTR>(id_.utf16()), dev.out());
        if (FAILED(hr)) { err = hrText("Không tìm thấy thiết bị âm thanh", hr); return false; }
        { std::lock_guard lk(m_); name_ = deviceName(dev.p); }
        Com<IAudioClient> cl;
        hr = dev->Activate(kIidAudioClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(cl.out()));
        if (FAILED(hr)) { err = hrText("Không kích hoạt được IAudioClient", hr); return false; }

        WAVEFORMATEXTENSIBLE wf{};
        wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wf.Format.nChannels = kAudioChannels;
        wf.Format.nSamplesPerSec = kAudioRate;
        wf.Format.wBitsPerSample = 32;
        wf.Format.nBlockAlign = WORD(kAudioChannels * 4);
        wf.Format.nAvgBytesPerSec = DWORD(kAudioRate * kAudioChannels * 4);
        wf.Format.cbSize = 22;
        wf.Samples.wValidBitsPerSample = 32;
        wf.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        wf.SubFormat = kSubtypeFloat;
        REFERENCE_TIME defPeriod = 0, minPeriod = 0;
        cl->GetDevicePeriod(&defPeriod, &minPeriod);
        if (defPeriod <= 0) defPeriod = 100000;                  // 10 ms
        hr = cl->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                            defPeriod * 2, 0, &wf.Format, nullptr);
        if (FAILED(hr)) { err = hrText("Thiết bị không nhận định dạng float32 stereo 48 kHz", hr); return false; }
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!ev) { err = "Không tạo được sự kiện"; return false; }
        struct EvGuard { HANDLE h; ~EvGuard() { CloseHandle(h); } } evGuard{ev};
        cl->SetEventHandle(ev);
        UINT32 bufFrames = 0;
        cl->GetBufferSize(&bufFrames);
        REFERENCE_TIME streamLat = 0;
        cl->GetStreamLatency(&streamLat);
        latency_ = double(bufFrames) / kAudioRate + double(streamLat) / 1e7;
        Com<IAudioRenderClient> rc;
        hr = cl->GetService(kIidRenderClient, reinterpret_cast<void**>(rc.out()));
        if (FAILED(hr)) { err = hrText("Không lấy được IAudioRenderClient", hr); return false; }
        BYTE* data = nullptr;
        if (SUCCEEDED(rc->GetBuffer(bufFrames, &data))) rc->ReleaseBuffer(bufFrames, AUDCLNT_BUFFERFLAGS_SILENT);   // nap im lang
        hr = cl->Start();
        if (FAILED(hr)) { err = hrText("Không khởi động được luồng âm thanh", hr); return false; }
        signalReady({});

        DWORD mmIdx = 0;
        HANDLE mm = AvSetMmThreadCharacteristicsW(L"Pro Audio", &mmIdx);
        while (!stop_) {
            const DWORD w = WaitForSingleObject(ev, 200);
            if (w == WAIT_TIMEOUT) continue;
            if (w != WAIT_OBJECT_0) break;
            UINT32 pad = 0;
            hr = cl->GetCurrentPadding(&pad);
            if (FAILED(hr)) { invalidated = hr == AUDCLNT_E_DEVICE_INVALIDATED; break; }
            const UINT32 avail = bufFrames - pad;
            if (avail == 0) continue;
            hr = rc->GetBuffer(avail, &data);
            if (FAILED(hr)) { invalidated = hr == AUDCLNT_E_DEVICE_INVALIDATED; break; }
            fn_(reinterpret_cast<float*>(data), int(avail));        // khong cap phat, khong khoa
            rc->ReleaseBuffer(avail, 0);
        }
        if (mm) AvRevertMmThreadCharacteristics(mm);
        cl->Stop();
        return true;
    }

    void run() {
        ComInit com;
        bool first = true;
        while (!stop_) {
            QString err;
            bool invalidated = false;
            const bool ok = openAndPlay(err, invalidated);
            if (!ok && first) { signalReady(err); return; }     // khong mo duoc ngay tu dau: bao loi cho start()
            first = false;
            if (stop_) break;
            // thiet bi bi rut / doi thiet bi mac dinh giua chung: thu mo lai moi giay
            qWarning().noquote() << "Âm thanh: thiết bị ngắt, thử mở lại..." << err;
            for (int i = 0; i < 10 && !stop_; ++i) Sleep(100);
        }
    }

    QString id_, name_, initErr_;
    RenderFn fn_;
    std::thread th_;
    std::atomic<bool> stop_{false};
    std::atomic<double> latency_{0.03};
    mutable std::mutex m_;
    std::condition_variable cv_;
    bool ready_ = false;
};

} // namespace

std::vector<AudioDeviceInfo> listWasapiDevices() {
    std::vector<AudioDeviceInfo> v;
    v.push_back({QString(), "Mặc định của hệ thống", true});
    ComInit com;
    Com<IMMDeviceEnumerator> en;
    if (!getEnumerator(en)) return v;
    QString defId;
    {
        Com<IMMDevice> d;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, d.out()))) defId = deviceId(d.p);
    }
    Com<IMMDeviceCollection> col;
    if (FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, col.out()))) return v;
    UINT n = 0;
    col->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
        Com<IMMDevice> d;
        if (FAILED(col->Item(i, d.out()))) continue;
        AudioDeviceInfo info;
        info.id = deviceId(d.p);
        info.name = deviceName(d.p);
        info.isDefault = info.id == defId;
        if (info.name.isEmpty()) info.name = info.id;
        v.push_back(info);
    }
    return v;
}

std::unique_ptr<AudioDevice> createWasapiDevice(const QString& id, QString* err) {
    Q_UNUSED(err);
    return std::make_unique<WasapiDevice>(id);
}

} // namespace lc
#endif // _WIN32
