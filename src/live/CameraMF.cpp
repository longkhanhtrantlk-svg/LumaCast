// Camera/card capture bang Media Foundation (chi Windows; chi Win32/COM, KHONG Qt Multimedia).
// !!! CHUA CHAY tren Windows that: phien phat trien chi bien dich kiem cu phap bang mingw. Thu T-CAM / T-CAM-HOT truoc khi tin.
// Mo hinh: IMFSourceReader BAT DONG BO (callback) -> OnReadSample day khung RGB32 -> RGBA vao o "moi nhat"; capture() cho tren condvar co han, nen
// ket noi treo khong bao gio chan LiveSource. Rut thiet bi -> OnReadSample nhan loi/co ERROR -> capture() tra Lost.
#ifdef _WIN32
#define NOMINMAX
#include "live/Camera.h"
#include "core/I18n.h"
#include <QDebug>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>

namespace lc {
namespace {

template <class T>
struct Com {
    T* p = nullptr;
    Com() = default;
    explicit Com(T* x) : p(x) {}
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    Com(Com&& o) noexcept : p(o.p) { o.p = nullptr; }
    ~Com() { if (p) p->Release(); }
    T** put() { if (p) { p->Release(); p = nullptr; } return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

struct ComInit {
    bool ok = false;
    ComInit() { const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); ok = hr == S_OK || hr == S_FALSE; }   // RPC_E_CHANGED_MODE (STA cua Qt): van dung duoc
    ~ComInit() { if (ok) CoUninitialize(); }
};
void ensureCom() { thread_local ComInit c; (void)c; }

QString fromWide(const wchar_t* w, UINT32 n) { return QString::fromWCharArray(w, int(n)); }

struct DevEntry { QString id, name; Com<IMFActivate> act; };

std::vector<DevEntry> enumerate() {
    std::vector<DevEntry> out;
    ensureCom();
    Com<IMFAttributes> attr;
    if (FAILED(MFCreateAttributes(attr.put(), 1))) return out;
    attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devs = nullptr;
    UINT32 n = 0;
    if (FAILED(MFEnumDeviceSources(attr.p, &devs, &n))) return out;
    for (UINT32 i = 0; i < n; ++i) {
        DevEntry e;
        e.act.p = devs[i];                               // chuyen quyen so huu
        WCHAR* s = nullptr; UINT32 len = 0;
        if (SUCCEEDED(devs[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &s, &len))) { e.name = fromWide(s, len); CoTaskMemFree(s); }
        s = nullptr; len = 0;
        if (SUCCEEDED(devs[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &s, &len))) { e.id = fromWide(s, len); CoTaskMemFree(s); }
        out.push_back(std::move(e));
    }
    CoTaskMemFree(devs);
    return out;
}

struct Shared {                                          // trang thai chung giua LiveInput va callback (callback co the tre hon LiveInput)
    std::mutex m;
    std::condition_variable cv;
    LiveVideoFrame frame;
    bool hasFrame = false, lost = false, closing = false, flushed = false;
    QString err;
    int w = 0, h = 0;
    LONG stride = 0;                                     // am = anh nguoc (bottom-up)
};

class Callback : public IMFSourceReaderCallback {
public:
    explicit Callback(std::shared_ptr<Shared> s) : s_(std::move(s)) {}
    void setReader(IMFSourceReader* r) { std::lock_guard lk(s_->m); reader_ = r; }          // khong giu tham chieu (tranh vong)
    STDMETHODIMP QueryInterface(REFIID iid, void** v) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFSourceReaderCallback)) { *v = static_cast<IMFSourceReaderCallback*>(this); AddRef(); return S_OK; }
        *v = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() override { const ULONG r = --ref_; if (r == 0) delete this; return r; }
    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }
    STDMETHODIMP OnFlush(DWORD) override { { std::lock_guard lk(s_->m); s_->flushed = true; } s_->cv.notify_all(); return S_OK; }
    STDMETHODIMP OnReadSample(HRESULT hr, DWORD, DWORD flags, LONGLONG, IMFSample* sample) override {
        IMFSourceReader* reader = nullptr;
        {
            std::lock_guard lk(s_->m);
            if (s_->closing) return S_OK;
            reader = reader_;
        }
        if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))) {
            { std::lock_guard lk(s_->m); s_->lost = true; s_->err = QString("HRESULT 0x%1").arg(quint32(hr), 8, 16, QChar('0')); }
            s_->cv.notify_all();
            return S_OK;
        }
        if (sample) {
            Com<IMFMediaBuffer> buf;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(buf.put()))) {
                BYTE* data = nullptr; DWORD maxLen = 0, curLen = 0;
                if (SUCCEEDED(buf->Lock(&data, &maxLen, &curLen))) {
                    std::lock_guard lk(s_->m);
                    const int w = s_->w, h = s_->h;
                    const size_t rowBytes = size_t(std::abs(s_->stride));
                    if (w > 0 && h > 0 && rowBytes >= size_t(w) * 4 && curLen >= rowBytes * size_t(h)) {
                        s_->frame.w = w; s_->frame.h = h;
                        s_->frame.rgba.resize(size_t(w) * size_t(h) * 4);
                        for (int y = 0; y < h; ++y) {
                            const BYTE* src = data + size_t(s_->stride < 0 ? h - 1 - y : y) * rowBytes;      // stride am: hang dau trong bo nho la hang DUOI
                            uint8_t* d = &s_->frame.rgba[size_t(y) * size_t(w) * 4];
                            for (int x = 0; x < w; ++x) { d[x * 4] = src[x * 4 + 2]; d[x * 4 + 1] = src[x * 4 + 1]; d[x * 4 + 2] = src[x * 4]; d[x * 4 + 3] = 255; }   // BGRX -> RGBA
                        }
                        s_->hasFrame = true;
                    }
                    buf->Unlock();
                }
            }
            s_->cv.notify_all();
        }
        if (reader) reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr);
        return S_OK;
    }
private:
    std::atomic<ULONG> ref_{1};
    std::shared_ptr<Shared> s_;
    IMFSourceReader* reader_ = nullptr;
};

class MfInput : public LiveInput {
public:
    MfInput(std::shared_ptr<Shared> s, Callback* cb, Com<IMFMediaSource> src, Com<IMFSourceReader> rd, QString info)
        : s_(std::move(s)), cb_(cb), src_(std::move(src)), rd_(std::move(rd)), info_(std::move(info)) {}
    ~MfInput() override {
        { std::lock_guard lk(s_->m); s_->closing = true; }
        cb_->setReader(nullptr);
        if (rd_) {
            if (SUCCEEDED(rd_->Flush(MF_SOURCE_READER_ALL_STREAMS))) {
                std::unique_lock lk(s_->m);
                s_->cv.wait_for(lk, std::chrono::seconds(1), [&] { return s_->flushed; });
            }
        }
        rd_.put();
        if (src_) src_->Shutdown();
        src_.put();
        cb_->Release();
    }
    Event capture(int timeoutMs, LiveVideoFrame& v, LiveAudioBlock&) override {
        std::unique_lock lk(s_->m);
        s_->cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] { return s_->hasFrame || s_->lost; });
        if (s_->hasFrame) { v = std::move(s_->frame); s_->frame = LiveVideoFrame(); s_->hasFrame = false; return Event::Video; }
        return s_->lost ? Event::Lost : Event::None;
    }
    QString info() const override { return info_; }
private:
    std::shared_ptr<Shared> s_;
    Callback* cb_;
    Com<IMFMediaSource> src_;
    Com<IMFSourceReader> rd_;
    QString info_;
};

class MfBackend : public CameraBackend {
public:
    MfBackend() { ensureCom(); ok_ = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL)); }
    ~MfBackend() override { if (ok_) MFShutdown(); }
    QString id() const override { return "mf"; }
    bool available() const override { return ok_; }
    QString unavailableReason() const override { return ok_ ? QString() : LC_TR("Media Foundation không khởi động được"); }
    std::vector<CameraDevice> list() override {
        std::vector<CameraDevice> v;
        for (auto& e : enumerate()) v.push_back({e.id, e.name});
        return v;
    }
    std::vector<CameraFormat> formats(const QString& deviceId) override {
        std::vector<CameraFormat> out;
        auto devs = enumerate();
        for (auto& e : devs) {
            if (e.id != deviceId) continue;
            Com<IMFMediaSource> src;
            if (FAILED(e.act->ActivateObject(IID_PPV_ARGS(src.put())))) return out;
            Com<IMFSourceReader> rd;
            if (SUCCEEDED(MFCreateSourceReaderFromMediaSource(src.p, nullptr, rd.put()))) {
                for (DWORD i = 0;; ++i) {
                    Com<IMFMediaType> mt;
                    if (FAILED(rd->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, mt.put()))) break;
                    UINT32 w = 0, h = 0, n = 0, d = 1;
                    MFGetAttributeSize(mt.p, MF_MT_FRAME_SIZE, &w, &h);
                    MFGetAttributeRatio(mt.p, MF_MT_FRAME_RATE, &n, &d);
                    if (!w || !h || !d) continue;
                    CameraFormat f{int(w), int(h), double(n) / double(d)};
                    bool dup = false;
                    for (const auto& x : out) dup = dup || x == f;
                    if (!dup) out.push_back(f);
                }
            }
            rd.put();
            src->Shutdown();
            break;
        }
        return out;
    }
    LiveOpenResult open(const QString& deviceId, const QString& deviceName, CameraFormat want) override {
        LiveOpenResult r;
        auto fail = [&](const QString& m) { r.error = m; return std::move(r); };
        ensureCom();
        auto devs = enumerate();
        DevEntry* d = nullptr;
        for (auto& e : devs) if (e.id == deviceId) d = &e;
        if (!d && !deviceName.isEmpty()) for (auto& e : devs) if (!d && e.name == deviceName) d = &e;      // id doi, ten con
        if (!d) return fail(LC_TR("Không thấy camera: ") + (deviceName.isEmpty() ? deviceId : deviceName));
        Com<IMFMediaSource> src;
        if (FAILED(d->act->ActivateObject(IID_PPV_ARGS(src.put())))) return fail(LC_TR("Không mở được camera (đang được chương trình khác dùng?): ") + d->name);
        auto sh = std::make_shared<Shared>();
        auto* cb = new Callback(sh);                      // ref = 1, thuoc ve MfInput
        Com<IMFAttributes> attr;
        MFCreateAttributes(attr.put(), 2);
        attr->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, cb);
        attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);   // cho phep doi YUY2/NV12/MJPG -> RGB32
        Com<IMFSourceReader> rd;
        if (FAILED(MFCreateSourceReaderFromMediaSource(src.p, attr.p, rd.put()))) { cb->Release(); src->Shutdown(); return fail(LC_TR("Không tạo được bộ đọc camera")); }
        // chon che do goc gan nhat voi `want`
        double best = 1e18;
        Com<IMFMediaType> bestMt;
        for (DWORD i = 0;; ++i) {
            Com<IMFMediaType> mt;
            if (FAILED(rd->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, mt.put()))) break;
            UINT32 w = 0, h = 0, n = 0, dd = 1;
            MFGetAttributeSize(mt.p, MF_MT_FRAME_SIZE, &w, &h);
            MFGetAttributeRatio(mt.p, MF_MT_FRAME_RATE, &n, &dd);
            if (!w || !h || !dd) continue;
            const double score = (want.w ? std::abs(int(w) - want.w) * 10.0 : 0) + (want.h ? std::abs(int(h) - want.h) * 10.0 : 0) +
                                 (want.fps > 0 ? std::abs(double(n) / dd - want.fps) : 0);
            if (score < best) { best = score; bestMt.put(); bestMt.p = mt.p; mt.p = nullptr; }
        }
        if (bestMt) rd->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, bestMt.p);
        UINT32 fw = 0, fh = 0;
        if (bestMt) MFGetAttributeSize(bestMt.p, MF_MT_FRAME_SIZE, &fw, &fh);
        Com<IMFMediaType> out;
        MFCreateMediaType(out.put());
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (FAILED(rd->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, out.p))) {
            cb->Release(); rd.put(); src->Shutdown();
            return fail(LC_TR("Camera không xuất được RGB32"));
        }
        Com<IMFMediaType> cur;
        UINT32 w = 0, h = 0;
        LONG stride = 0;
        if (SUCCEEDED(rd->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, cur.put()))) {
            MFGetAttributeSize(cur.p, MF_MT_FRAME_SIZE, &w, &h);
            UINT32 st = 0;
            if (SUCCEEDED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE, &st))) stride = LONG(INT32(st));
        }
        if (!w || !h) { w = fw; h = fh; }
        if (stride == 0) stride = LONG(w) * 4;                     // khong biet: coi nhu xuoi, khong dao
        sh->w = int(w); sh->h = int(h); sh->stride = stride;
        cb->setReader(rd.p);
        if (FAILED(rd->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr))) {
            cb->setReader(nullptr); cb->Release(); rd.put(); src->Shutdown();
            return fail(LC_TR("Không bắt đầu đọc được camera"));
        }
        r.input = std::make_unique<MfInput>(sh, cb, std::move(src), std::move(rd), d->name + QString(" %1x%2").arg(w).arg(h));
        return r;
    }
private:
    bool ok_ = false;
};
} // namespace

std::shared_ptr<CameraBackend> createMediaFoundationCamera() {
    auto b = std::make_shared<MfBackend>();
    if (!b->available()) return nullptr;
    return b;
}

} // namespace lc
#endif
