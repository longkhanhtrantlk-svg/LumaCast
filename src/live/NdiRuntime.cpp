// Backend NDI THAT: nap dong runtime NDI (QLibrary) va goi cac ham C qua khai bao toi thieu duoi day.
//
// !!! CHUA KIEM CHUNG !!! Cac khai bao ABI (ten ham, bo cuc struct, gia tri enum) duoc viet theo header cong khai cua NDI SDK tu tri nho, KHONG
// duoc doi chieu voi header chinh thuc va chua chay voi runtime that (phien lam viec khong co NDI). Cac bien phap an toan:
//  - static_assert kich thuoc struct tren 64-bit; neu thieu bat ky ham nao thi coi nhu runtime khong tuong thich (tinh nang tu tat, khong crash);
//  - LUMACAST_NDI_DISABLE=1 tat han NDI; LUMACAST_NDI_LIB=<duong dan> chi dinh thu vien (test dung thu vien gia de kiem chung duong nap).
// Truoc khi phat hanh PHAI doi chieu tung dong voi Processing.NDI.Lib.h cua SDK ban dang dung va thu T-NDI-IN / T-NDI-OUT tren Windows.
// Khong co file nao cua NDI SDK nam trong repo. NDI® la thuong hieu dang ky cua Vizrt NDI AB.
#include "live/Ndi.h"
#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QLibrary>
#include <QMutex>
#include <algorithm>
#include <cstring>

namespace lc {
namespace {

namespace abi {
struct source_t { const char* p_ndi_name; const char* p_url_address; };
struct find_create_t { bool show_local_sources; const char* p_groups; const char* p_extra_ips; };
enum recv_color_format_e { RGBX_RGBA = 2 };                     // NDIlib_recv_color_format_RGBX_RGBA
enum recv_bandwidth_e { bandwidth_highest = 100 };               // NDIlib_recv_bandwidth_highest
struct recv_create_v3_t {
    source_t source_to_connect_to;
    int color_format;
    int bandwidth;
    bool allow_video_fields;
    const char* p_ndi_recv_name;
};
enum frame_type_e { frame_type_none = 0, frame_type_video = 1, frame_type_audio = 2, frame_type_metadata = 3, frame_type_error = 4 };
constexpr uint32_t fourcc(char a, char b, char c, char d) { return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24; }
constexpr uint32_t FOURCC_RGBA = fourcc('R', 'G', 'B', 'A'), FOURCC_RGBX = fourcc('R', 'G', 'B', 'X');
constexpr uint32_t FOURCC_BGRA = fourcc('B', 'G', 'R', 'A'), FOURCC_BGRX = fourcc('B', 'G', 'R', 'X');
struct video_frame_v2_t {
    int xres, yres;
    uint32_t FourCC;
    int frame_rate_N, frame_rate_D;
    float picture_aspect_ratio;
    int frame_format_type;
    int64_t timecode;
    uint8_t* p_data;
    int line_stride_in_bytes;
    const char* p_metadata;
    int64_t timestamp;
};
struct audio_frame_v2_t {
    int sample_rate, no_channels, no_samples;
    int64_t timecode;
    float* p_data;
    int channel_stride_in_bytes;
    const char* p_metadata;
    int64_t timestamp;
};
struct send_create_t { const char* p_ndi_name; const char* p_groups; bool clock_video; bool clock_audio; };
constexpr int64_t timecode_synthesize = INT64_MAX;
constexpr int frame_format_progressive = 1;
static_assert(sizeof(void*) != 8 || sizeof(video_frame_v2_t) == 72, "NDIlib_video_frame_v2_t phai 72 byte tren 64-bit");
static_assert(sizeof(void*) != 8 || sizeof(audio_frame_v2_t) == 56, "NDIlib_audio_frame_v2_t phai 56 byte tren 64-bit");
static_assert(sizeof(void*) != 8 || sizeof(source_t) == 16, "NDIlib_source_t phai 16 byte tren 64-bit");
static_assert(sizeof(void*) != 8 || sizeof(recv_create_v3_t) == 40, "NDIlib_recv_create_v3_t phai 40 byte tren 64-bit");
} // namespace abi

using Inst = void*;
struct Api {
    bool (*initialize)() = nullptr;
    void (*destroy)() = nullptr;
    const char* (*version)() = nullptr;
    Inst (*find_create_v2)(const abi::find_create_t*) = nullptr;
    void (*find_destroy)(Inst) = nullptr;
    bool (*find_wait_for_sources)(Inst, uint32_t) = nullptr;
    const abi::source_t* (*find_get_current_sources)(Inst, uint32_t*) = nullptr;
    Inst (*recv_create_v3)(const abi::recv_create_v3_t*) = nullptr;
    void (*recv_destroy)(Inst) = nullptr;
    int (*recv_capture_v2)(Inst, abi::video_frame_v2_t*, abi::audio_frame_v2_t*, void*, uint32_t) = nullptr;
    void (*recv_free_video_v2)(Inst, const abi::video_frame_v2_t*) = nullptr;
    void (*recv_free_audio_v2)(Inst, const abi::audio_frame_v2_t*) = nullptr;
    Inst (*send_create)(const abi::send_create_t*) = nullptr;
    void (*send_destroy)(Inst) = nullptr;
    void (*send_send_video_v2)(Inst, const abi::video_frame_v2_t*) = nullptr;
    void (*send_send_audio_v2)(Inst, const abi::audio_frame_v2_t*) = nullptr;
    int (*send_get_no_connections)(Inst, uint32_t) = nullptr;
};

class RealInput : public LiveInput {
public:
    RealInput(std::shared_ptr<Api> api, Inst recv, QString name) : api_(std::move(api)), recv_(recv), name_(std::move(name)) {}
    ~RealInput() override { if (recv_) api_->recv_destroy(recv_); }
    Event capture(int timeoutMs, LiveVideoFrame& v, LiveAudioBlock& a) override {
        abi::video_frame_v2_t vf{};
        abi::audio_frame_v2_t af{};
        const int t = api_->recv_capture_v2(recv_, &vf, &af, nullptr, uint32_t(std::max(0, timeoutMs)));
        if (t == abi::frame_type_video) {
            const bool ok = convertVideo(vf, v);
            api_->recv_free_video_v2(recv_, &vf);
            return ok ? Event::Video : Event::None;
        }
        if (t == abi::frame_type_audio) {
            const bool ok = convertAudio(af, a);
            api_->recv_free_audio_v2(recv_, &af);
            return ok ? Event::Audio : Event::None;
        }
        if (t == abi::frame_type_error) return Event::Lost;
        return Event::None;
    }
    QString info() const override { return name_; }
private:
    bool convertVideo(const abi::video_frame_v2_t& f, LiveVideoFrame& out) {
        if (!f.p_data || f.xres <= 0 || f.yres <= 0 || f.line_stride_in_bytes < f.xres * 4) return false;
        const bool rgb = f.FourCC == abi::FOURCC_RGBA || f.FourCC == abi::FOURCC_RGBX;
        const bool bgr = f.FourCC == abi::FOURCC_BGRA || f.FourCC == abi::FOURCC_BGRX;
        if (!rgb && !bgr) {                                  // da yeu cau RGBX_RGBA: dinh dang khac = runtime la, bo qua
            if (!warnedFmt_) { warnedFmt_ = true; qWarning() << "NDI: dinh dang hinh khong ho tro (FourCC" << f.FourCC << ")"; }
            return false;
        }
        const bool hasAlpha = f.FourCC == abi::FOURCC_RGBA || f.FourCC == abi::FOURCC_BGRA;
        out.w = f.xres; out.h = f.yres;
        out.rgba.resize(size_t(f.xres) * size_t(f.yres) * 4);
        for (int y = 0; y < f.yres; ++y) {
            const uint8_t* s = f.p_data + size_t(y) * size_t(f.line_stride_in_bytes);
            uint8_t* d = &out.rgba[size_t(y) * size_t(f.xres) * 4];
            if (rgb && hasAlpha) std::memcpy(d, s, size_t(f.xres) * 4);
            else for (int x = 0; x < f.xres; ++x) {
                const uint8_t* p = s + x * 4;
                d[x * 4 + 0] = bgr ? p[2] : p[0];
                d[x * 4 + 1] = p[1];
                d[x * 4 + 2] = bgr ? p[0] : p[2];
                d[x * 4 + 3] = hasAlpha ? p[3] : 255;
            }
        }
        return true;
    }
    bool convertAudio(const abi::audio_frame_v2_t& f, LiveAudioBlock& out) {      // NDI: float PLANAR (moi kenh mot dai, cach nhau channel_stride_in_bytes)
        if (!f.p_data || f.no_samples <= 0 || f.no_channels <= 0 || f.sample_rate <= 0) return false;
        const int nch = std::min(f.no_channels, 2);                                // >2 kenh: lay 2 kenh dau (gioi han da ghi trong KienTruc)
        const size_t stride = size_t(f.channel_stride_in_bytes) / sizeof(float);
        if (stride < size_t(f.no_samples)) return false;
        out.rate = f.sample_rate; out.channels = nch;
        out.data.resize(size_t(f.no_samples) * size_t(nch));
        for (int c = 0; c < nch; ++c) {
            const float* s = f.p_data + size_t(c) * stride;
            for (int i = 0; i < f.no_samples; ++i) out.data[size_t(i) * size_t(nch) + size_t(c)] = s[i];
        }
        return true;
    }
    std::shared_ptr<Api> api_;
    Inst recv_;
    QString name_;
    bool warnedFmt_ = false;
};

class RealSender : public NdiSender {
public:
    RealSender(std::shared_ptr<Api> api, Inst s) : api_(std::move(api)), s_(s) {}
    ~RealSender() override { if (s_) api_->send_destroy(s_); }
    bool sendVideo(const uint8_t* rgba, int w, int h, int stride, int fpsNum, int fpsDen) override {
        abi::video_frame_v2_t f{};
        f.xres = w; f.yres = h;
        f.FourCC = abi::FOURCC_RGBA;
        f.frame_rate_N = fpsNum; f.frame_rate_D = fpsDen;
        f.picture_aspect_ratio = float(w) / float(std::max(1, h));
        f.frame_format_type = abi::frame_format_progressive;
        f.timecode = abi::timecode_synthesize;
        f.p_data = const_cast<uint8_t*>(rgba);
        f.line_stride_in_bytes = stride;
        api_->send_send_video_v2(s_, &f);            // dong bo: tra ve khi da sao chep xong
        return true;
    }
    bool sendAudio(const float* in, int frames, int rate, int channels) override {
        planar_.resize(size_t(frames) * size_t(channels));
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < frames; ++i) planar_[size_t(c) * size_t(frames) + size_t(i)] = in[size_t(i) * size_t(channels) + size_t(c)];
        abi::audio_frame_v2_t f{};
        f.sample_rate = rate; f.no_channels = channels; f.no_samples = frames;
        f.timecode = abi::timecode_synthesize;
        f.p_data = planar_.data();
        f.channel_stride_in_bytes = frames * int(sizeof(float));
        api_->send_send_audio_v2(s_, &f);
        return true;
    }
    int connections() const override { return api_->send_get_no_connections ? api_->send_get_no_connections(s_, 0) : -1; }
private:
    std::shared_ptr<Api> api_;
    Inst s_;
    std::vector<float> planar_;
};

class RealBackend : public NdiBackend {
public:
    RealBackend(std::unique_ptr<QLibrary> lib, std::shared_ptr<Api> api, QString ver) : lib_(std::move(lib)), api_(std::move(api)), ver_(std::move(ver)) {}
    ~RealBackend() override {
        if (finder_) api_->find_destroy(finder_);
        api_->destroy();
    }
    QString id() const override { return "real"; }
    bool available() const override { return true; }
    QString unavailableReason() const override { return {}; }
    QString version() const override { return ver_; }
    std::vector<NdiSourceInfo> listSources(int waitMs) override {
        QMutexLocker lk(&mu_);
        if (!finder_) {
            abi::find_create_t c{true, nullptr, nullptr};
            finder_ = api_->find_create_v2(&c);
            if (!finder_) return {};
        }
        api_->find_wait_for_sources(finder_, uint32_t(std::max(0, waitMs)));
        uint32_t n = 0;
        const abi::source_t* s = api_->find_get_current_sources(finder_, &n);
        std::vector<NdiSourceInfo> v;
        for (uint32_t i = 0; s && i < n; ++i)
            v.push_back({QString::fromUtf8(s[i].p_ndi_name ? s[i].p_ndi_name : ""), QString::fromUtf8(s[i].p_url_address ? s[i].p_url_address : "")});
        return v;
    }
    LiveOpenResult openReceiver(const QString& name, const QString& url) override {
        LiveOpenResult r;
        const QByteArray n = name.toUtf8(), u = url.toUtf8(), rn = QByteArray("LumaCast");
        abi::recv_create_v3_t c{};
        c.source_to_connect_to.p_ndi_name = n.constData();
        c.source_to_connect_to.p_url_address = u.isEmpty() ? nullptr : u.constData();
        c.color_format = abi::RGBX_RGBA;
        c.bandwidth = abi::bandwidth_highest;
        c.allow_video_fields = false;
        c.p_ndi_recv_name = rn.constData();
        Inst h = api_->recv_create_v3(&c);               // NDI sao chep chuoi vao ben trong
        if (!h) { r.error = "Không tạo được bộ nhận NDI"; return r; }
        r.input = std::make_unique<RealInput>(api_, h, name);
        return r;
    }
    std::unique_ptr<NdiSender> createSender(const QString& name, QString* err) override {
        const QByteArray n = name.toUtf8();
        abi::send_create_t c{n.constData(), nullptr, false, false};
        Inst h = api_->send_create(&c);
        if (!h) { if (err) *err = "Không tạo được nguồn NDI ra (tên trùng hoặc NDI lỗi)"; return nullptr; }
        return std::make_unique<RealSender>(api_, h);
    }
private:
    std::unique_ptr<QLibrary> lib_;
    std::shared_ptr<Api> api_;
    QString ver_;
    QMutex mu_;
    Inst finder_ = nullptr;
};

QStringList candidateLibs() {
    QStringList c;
    const QByteArray ov = qgetenv("LUMACAST_NDI_LIB");
    if (!ov.isEmpty()) return {QString::fromLocal8Bit(ov)};              // chi dinh ro: chi thu cai nay
#ifdef _WIN32
    const char* kName = "Processing.NDI.Lib.x64.dll";
    for (const char* ev : {"NDI_RUNTIME_DIR_V6", "NDI_RUNTIME_DIR_V5", "NDI_RUNTIME_DIR_V4", "NDI_RUNTIME_DIR_V3"}) {   // runtime dat bien nay khi cai (chua kiem chung ten)
        const QByteArray d = qgetenv(ev);
        if (!d.isEmpty()) c << QDir(QString::fromLocal8Bit(d)).filePath(kName);
    }
    c << kName;
#elif defined(__APPLE__)
    c << "libndi.dylib" << "/usr/local/lib/libndi.dylib";
#else
    c << "libndi.so.6" << "libndi.so.5" << "libndi.so";
#endif
    return c;
}

} // namespace

std::shared_ptr<NdiBackend> createUnavailableNdiBackend(const QString& reason) {
    class None : public NdiBackend {
    public:
        explicit None(QString r) : r_(std::move(r)) {}
        QString id() const override { return "none"; }
        bool available() const override { return false; }
        QString unavailableReason() const override { return r_; }
        std::vector<NdiSourceInfo> listSources(int) override { return {}; }
        LiveOpenResult openReceiver(const QString&, const QString&) override { LiveOpenResult r; r.unavailable = true; r.error = r_; return r; }
        std::unique_ptr<NdiSender> createSender(const QString&, QString* err) override { if (err) *err = r_; return nullptr; }
    private:
        QString r_;
    };
    return std::make_shared<None>(reason);
}

std::shared_ptr<NdiBackend> createRealNdiBackend(QString* reason) {
    auto fail = [&](const QString& m) -> std::shared_ptr<NdiBackend> { if (reason) *reason = m; return nullptr; };
    QString lastErr;
    for (const QString& cand : candidateLibs()) {
        auto lib = std::make_unique<QLibrary>(cand);
        if (!lib->load()) { lastErr = lib->errorString(); continue; }
        auto api = std::make_shared<Api>();
        bool ok = true;
#define LC_NDI_SYM(field, name) api->field = reinterpret_cast<decltype(api->field)>(lib->resolve(name)); ok = ok && api->field != nullptr
        LC_NDI_SYM(initialize, "NDIlib_initialize");
        LC_NDI_SYM(destroy, "NDIlib_destroy");
        LC_NDI_SYM(version, "NDIlib_version");
        LC_NDI_SYM(find_create_v2, "NDIlib_find_create_v2");
        LC_NDI_SYM(find_destroy, "NDIlib_find_destroy");
        LC_NDI_SYM(find_wait_for_sources, "NDIlib_find_wait_for_sources");
        LC_NDI_SYM(find_get_current_sources, "NDIlib_find_get_current_sources");
        LC_NDI_SYM(recv_create_v3, "NDIlib_recv_create_v3");
        LC_NDI_SYM(recv_destroy, "NDIlib_recv_destroy");
        LC_NDI_SYM(recv_capture_v2, "NDIlib_recv_capture_v2");
        LC_NDI_SYM(recv_free_video_v2, "NDIlib_recv_free_video_v2");
        LC_NDI_SYM(recv_free_audio_v2, "NDIlib_recv_free_audio_v2");
        LC_NDI_SYM(send_create, "NDIlib_send_create");
        LC_NDI_SYM(send_destroy, "NDIlib_send_destroy");
        LC_NDI_SYM(send_send_video_v2, "NDIlib_send_send_video_v2");
        LC_NDI_SYM(send_send_audio_v2, "NDIlib_send_send_audio_v2");
#undef LC_NDI_SYM
        api->send_get_no_connections = reinterpret_cast<decltype(api->send_get_no_connections)>(lib->resolve("NDIlib_send_get_no_connections"));   // tuy chon
        if (!ok) { lastErr = "Runtime NDI thiếu hàm cần dùng (phiên bản không tương thích): " + cand; lib->unload(); continue; }
        if (!api->initialize()) { lastErr = "NDIlib_initialize thất bại (CPU không được NDI hỗ trợ?)"; lib->unload(); continue; }
        const QString ver = QString::fromUtf8(api->version());
        qInfo().noquote() << "NDI Runtime nạp từ" << cand << "| phiên bản" << ver << "(ABI tự khai báo, chưa đối chiếu header chính thức)";
        return std::make_shared<RealBackend>(std::move(lib), std::move(api), ver);
    }
    return fail(lastErr.isEmpty() ? QString("Chưa cài NDI Runtime") : "Chưa cài NDI Runtime (" + lastErr + ")");
}

} // namespace lc
