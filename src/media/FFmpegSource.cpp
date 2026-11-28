#include "FFmpegSource.h"
#include "core/I18n.h"
#include <QDebug>
#include <QFileInfo>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace lc {

using Clock = std::chrono::steady_clock;

// Display matrix (video quay doc bang dien thoai luu hinh ngang + co xoay) -> do xoay theo chieu kim dong ho.
// av_display_rotation_get tra goc nguoc chieu kim dong ho can xoay de HIEN THI dung.
static int rotationFromMatrix(const uint8_t* data, size_t size) {
    if (!data || size < 9 * sizeof(int32_t)) return 0;
    const double a = av_display_rotation_get(reinterpret_cast<const int32_t*>(data));
    if (std::isnan(a)) return 0;
    int cw = int(std::lround(-a));
    cw = ((cw % 360) + 360) % 360;
    return ((cw + 45) / 90 * 90) % 360;                  // chi ho tro boi so cua 90
}

// ---------------- Tuy chon giai ma ----------------

static std::mutex gOptMu;
static VideoDecodeOptions gOpt;
static bool gOptSet = false;

VideoDecodeOptions VideoDecodeOptions::fromEnvironment() {
    VideoDecodeOptions o;
    const QString hw = qEnvironmentVariable("LUMACAST_HWDEC").trimmed().toLower();
    if (!hw.isEmpty()) o.hwDecode = hw;
    if (qEnvironmentVariable("LUMACAST_GPU_YUV").trimmed() == "0") o.gpuColor = false;
    if (qEnvironmentVariable("LUMACAST_HAP").trimmed() == "0") o.hapDirect = false;
    return o;
}
VideoDecodeOptions VideoDecodeOptions::defaults() {
    std::lock_guard lk(gOptMu);
    if (!gOptSet) { gOpt = fromEnvironment(); gOptSet = true; }
    return gOpt;
}
void VideoDecodeOptions::setDefaults(const VideoDecodeOptions& o) {
    std::lock_guard lk(gOptMu);
    gOpt = o;
    gOptSet = true;
}

// Bo nho dung lai cho anh YUV (tranh cap phat vai chuc MB moi khung 4K): frame tra ve ho khi het nguoi giu.
struct FFmpegSource::BufPool {
    std::mutex m;
    std::vector<std::unique_ptr<YuvImage>> free;
    std::shared_ptr<YuvImage> get(const std::shared_ptr<BufPool>& self) {
        std::unique_ptr<YuvImage> img;
        {
            std::lock_guard lk(m);
            if (!free.empty()) { img = std::move(free.back()); free.pop_back(); }
        }
        if (!img) img = std::make_unique<YuvImage>();
        return std::shared_ptr<YuvImage>(img.release(), [self](YuvImage* p) {
            std::lock_guard lk(self->m);
            if (self->free.size() < 4) self->free.emplace_back(p);
            else delete p;
        });
    }
};

FFmpegSource::FFmpegSource(QString path, bool loop, VideoDecodeOptions opts)
    : path_(std::move(path)), name_(QFileInfo(path_).fileName()), loop_(loop), opts_(std::move(opts)) {
    audio_ = std::make_unique<AudioStream>();
    pool_ = std::make_shared<BufPool>();
    thread_ = std::thread([this] { run(); });
}

QString FFmpegSource::decodeInfo() const {
    std::lock_guard lk(m_);
    return info_;
}

// Anh YUV -> QImage thu nho (chi cho snapshot cua UI; lay mau gan nhat, cung cong thuc voi shader).
static QImage yuvToImage(const YuvImage& y, int maxWidth) {
    const int tw = std::max(1, std::min(maxWidth, y.w)), th = std::max(1, int(std::lround(double(tw) * y.h / y.w)));
    QImage img(tw, th, QImage::Format_RGBA8888);
    const int bps = y.fmt.bytesPerSample();
    const bool semi = y.fmt.layout == YuvLayout::SemiPlanar;
    auto sample = [&](const std::vector<uint8_t>& pl, size_t idx) -> double {
        if (bps == 1) return pl[idx];
        const int v = pl[idx * 2] | (pl[idx * 2 + 1] << 8);
        return y.fmt.msb ? v >> (16 - y.fmt.bits) : v;
    };
    for (int j = 0; j < th; ++j) {
        const int sy = std::min(y.h - 1, int((j + 0.5) * y.h / th));
        uint8_t* row = img.scanLine(j);
        for (int i = 0; i < tw; ++i) {
            const int sx = std::min(y.w - 1, int((i + 0.5) * y.w / tw));
            const size_t cidx = size_t(sy >> y.fmt.chromaShiftY) * y.chromaW() + (sx >> y.fmt.chromaShiftX);
            const double yy = sample(y.plane[0], size_t(sy) * y.w + sx);
            const double u = semi ? sample(y.plane[1], cidx * 2) : sample(y.plane[1], cidx);
            const double v = semi ? sample(y.plane[1], cidx * 2 + 1) : sample(y.plane[2], cidx);
            double rgb[3];
            YuvTexture::convertPixel(y.fmt, yy, u, v, rgb);
            row[i * 4] = uint8_t(std::lround(rgb[0] * 255)); row[i * 4 + 1] = uint8_t(std::lround(rgb[1] * 255));
            row[i * 4 + 2] = uint8_t(std::lround(rgb[2] * 255)); row[i * 4 + 3] = 255;
        }
    }
    return img;
}

FFmpegSource::~FFmpegSource() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

QString FFmpegSource::error() const {
    std::lock_guard lk(m_);
    return error_;
}

void FFmpegSource::setError(const QString& e) {
    std::lock_guard lk(m_);
    error_ = e;
}

TexRef FFmpegSource::acquire(GL& gl) {
    std::shared_ptr<Frame> f;
    {
        std::lock_guard lk(m_);
        if (latest_ && latest_->seq != uploaded_) f = latest_;
    }
    if (f) {
        if (f->hap) {
            const HapGlFormat gf = f->hap->format == HapFormat::Dxt1 ? HapGlFormat::Dxt1
                                 : f->hap->format == HapFormat::Dxt5 ? HapGlFormat::Dxt5 : HapGlFormat::YCoCgDxt5;
            if (hapTex_.upload(gl, gf, f->hap->w, f->hap->h, f->hap->data.data(), f->hap->data.size())) cur_ = Cur::Hap;
        } else if (f->yuv) {
            if (yuvTex_.upload(gl, *f->yuv)) cur_ = Cur::Yuv;
        } else {
            tex_.upload(gl, f->rgba.data(), f->w, f->h);
            cur_ = Cur::Rgba;
        }
        uploaded_ = f->seq;
        rotation_ = f->rotation;
        waitFirst_ = false;
    }
    if (waitFirst_) return {};
    if (cur_ == Cur::Hap) return {hapTex_.id(), hapTex_.w(), hapTex_.h(), rotation_};
    if (cur_ == Cur::Yuv) return {yuvTex_.id(), yuvTex_.w(), yuvTex_.h(), rotation_};
    return {tex_.id(), tex_.w(), tex_.h(), rotation_};
}

QImage FFmpegSource::snapshot(int maxWidth) const {
    std::shared_ptr<Frame> f;
    {
        std::lock_guard lk(m_);
        f = latest_;
    }
    if (!f) return {};
    if (f->hap) {
        const int tw = std::max(1, std::min(maxWidth, f->w)), th = std::max(1, int(std::lround(double(tw) * f->h / f->w)));
        QImage img(tw, th, QImage::Format_RGBA8888);
        for (int j = 0; j < th; ++j)
            for (int i = 0; i < tw; ++i)
                hapSamplePixel(*f->hap, int((i + 0.5) * f->w / tw), int((j + 0.5) * f->h / th), img.scanLine(j) + i * 4);
        return f->rotation ? img.transformed(QTransform().rotate(f->rotation)) : img;
    }
    if (f->yuv) {
        const QImage small = yuvToImage(*f->yuv, std::min(maxWidth, f->w));
        return f->rotation ? small.transformed(QTransform().rotate(f->rotation)) : small;
    }
    QImage img(f->rgba.data(), f->w, f->h, f->w * 4, QImage::Format_RGBA8888);   // f giu bo nho song
    img = img.scaledToWidth(std::min(maxWidth, f->w), Qt::SmoothTransformation);   // scaled* tao ban sao
    return f->rotation ? img.transformed(QTransform().rotate(f->rotation)) : img;
}

void FFmpegSource::restart() {
    {
        std::lock_guard lk(m_);
        latest_.reset();
    }
    waitFirst_ = true;
    seekReq_ = -1.0;
    paused_ = false;
    ended_ = false;
    position_ = 0;
    audio_->setPaused(false);
    audio_->holdUntilFlush();                  // im lang ngay (khong phat not am thanh cu) cho den khi thread giai ma seek + flush
    restartReq_ = true;
}

void FFmpegSource::rewind() {
    {
        std::lock_guard lk(m_);
        if (first_) latest_ = first_;          // chua co (video vua nhap): giu khung dang hien
        rewindPending_ = true;
    }
    seekReq_ = -1.0;
    paused_ = false;
    ended_ = false;
    position_ = 0;
    audio_->setPaused(false);
    audio_->holdUntilFlush();                  // khong phat tiep tieng cua doan cu
    rewindReq_ = true;
}

void FFmpegSource::setPaused(bool on) {
    if (!on && ended_) seekTo(0);              // het file (khong lap): "phat" la phat lai tu dau
    paused_ = on;
    audio_->setPaused(on);
}

void FFmpegSource::seekTo(double seconds) {
    if (!std::isfinite(seconds)) return;
    const double d = duration_;
    seconds = std::clamp(seconds, 0.0, d > 0 ? d : 0.0);
    ended_ = false;
    position_ = seconds;
    audio_->holdUntilFlush();
    seekReq_ = seconds;
}

void FFmpegSource::publish(std::shared_ptr<Frame> out, bool nearStart) {
    std::lock_guard lk(m_);
    if (!first_ && nearStart) first_ = out;    // khung dau file (cho rewind); khong lay nham khung sau khi tua
    if (!rewindPending_) latest_ = std::move(out);
}

namespace {
// get_format cua giai ma phan cung: chon dung dinh dang phan cung da xin (opaque tro toi AVPixelFormat); khong co -> de FFmpeg tu chon (phan mem).
AVPixelFormat pickHwFormat(AVCodecContext* c, const AVPixelFormat* fmts) {
    const AVPixelFormat want = *static_cast<AVPixelFormat*>(c->opaque);
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
        if (*p == want) return *p;
    return AV_PIX_FMT_NONE;                          // khong dung duoc phan cung cho luong nay -> avcodec_open2/receive loi -> ve phan mem
}

// Dinh dang AVFrame -> mo ta YUV cho shader. false = khong ho tro (RGB, gray, co alpha, 12/16 bit...) -> sws_scale RGBA tren CPU.
YuvMatrix matrixOf(const AVFrame* f) {
    switch (f->colorspace) {
    case AVCOL_SPC_BT709: return YuvMatrix::BT709;
    case AVCOL_SPC_BT470BG: case AVCOL_SPC_SMPTE170M: case AVCOL_SPC_SMPTE240M: return YuvMatrix::BT601;
    case AVCOL_SPC_BT2020_NCL: case AVCOL_SPC_BT2020_CL: return YuvMatrix::BT2020;
    default: return f->height >= 720 ? YuvMatrix::BT709 : YuvMatrix::BT601;      // khong khai bao: HD -> 709, SD -> 601
    }
}
bool fullRangeOf(const AVFrame* f) {
    switch (AVPixelFormat(f->format)) {
    case AV_PIX_FMT_YUVJ420P: case AV_PIX_FMT_YUVJ422P: case AV_PIX_FMT_YUVJ444P: case AV_PIX_FMT_YUVJ440P: return true;
    default: return f->color_range == AVCOL_RANGE_JPEG;
    }
}
bool yuvFormatOf(const AVFrame* f, YuvFormat& o) {
    o = YuvFormat();
    switch (AVPixelFormat(f->format)) {
    case AV_PIX_FMT_YUV420P: break;
    case AV_PIX_FMT_YUVJ420P: break;
    case AV_PIX_FMT_YUV422P: o.chromaShiftY = 0; break;
    case AV_PIX_FMT_YUVJ422P: o.chromaShiftY = 0; break;
    case AV_PIX_FMT_YUV444P: o.chromaShiftX = o.chromaShiftY = 0; break;
    case AV_PIX_FMT_YUVJ444P: o.chromaShiftX = o.chromaShiftY = 0; break;
    case AV_PIX_FMT_YUV420P10LE: o.bits = 10; break;
    case AV_PIX_FMT_YUV422P10LE: o.bits = 10; o.chromaShiftY = 0; break;
    case AV_PIX_FMT_YUV444P10LE: o.bits = 10; o.chromaShiftX = o.chromaShiftY = 0; break;
    case AV_PIX_FMT_NV12: o.layout = YuvLayout::SemiPlanar; break;
    case AV_PIX_FMT_P010LE: o.layout = YuvLayout::SemiPlanar; o.bits = 10; o.msb = true; break;
    default: return false;
    }
    o.fullRange = fullRangeOf(f);
    o.matrix = matrixOf(f);
    // vi tri mau sac: khai bao "giua" -> giua; khong khai bao + day du khoang (JPEG) -> giua; con lai (MPEG-2/H.264) -> lech trai
    o.chromaCenter = f->chroma_location == AVCHROMA_LOC_CENTER || (f->chroma_location == AVCHROMA_LOC_UNSPECIFIED && o.fullRange);
    return true;
}

const char* pixName(int fmt) {
    const char* n = av_get_pix_fmt_name(AVPixelFormat(fmt));
    return n ? n : "?";
}

struct Pend {
    AVFrame* f = nullptr;
    std::shared_ptr<HapFrame> hap;       // HAP truc tiep: khong co AVFrame
    double pts = 0;          // PTS goc cua file (giay)
    double cont = 0;         // moc tren dong thoi gian lien tuc (hop le khi hasCont)
    bool hasCont = false;
};
} // namespace

void FFmpegSource::run() {
    AVFormatContext* fmt = nullptr;
    AVBufferRef* hwDev = nullptr;
    AVCodecContext *cc = nullptr, *ac = nullptr;
    AVPacket* pkt = av_packet_alloc();
    AVFrame *fr = av_frame_alloc(), *afr = av_frame_alloc();
    SwsContext* sws = nullptr;
    SwrContext* swr = nullptr;
    uint64_t seq = 0;
    std::deque<Pend> pend;

    auto cleanup = [&] {
        for (Pend& p : pend) av_frame_free(&p.f);
        sws_freeContext(sws);
        swr_free(&swr);
        av_frame_free(&fr);
        av_frame_free(&afr);
        av_packet_free(&pkt);
        avcodec_free_context(&cc);
        avcodec_free_context(&ac);
        av_buffer_unref(&hwDev);
        avformat_close_input(&fmt);
    };

    if (avformat_open_input(&fmt, path_.toUtf8().constData(), nullptr, nullptr) < 0) {
        setError(LC_TR("Khong mo duoc file: ") + path_);
        return cleanup();
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        setError(LC_TR("Khong doc duoc thong tin stream"));
        return cleanup();
    }
    const int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vs < 0) {
        setError(LC_TR("Khong co stream video"));
        return cleanup();
    }
    AVStream* st = fmt->streams[vs];
    if (fmt->duration > 0) duration_ = double(fmt->duration) / AV_TIME_BASE;
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        setError(LC_TR("Khong ho tro codec"));
        return cleanup();
    }

    // ----- mo decoder: thu phan cung (neu bat) -> that bai thi phan mem -----
    AVPixelFormat hwPixFmt = AV_PIX_FMT_NONE;
    bool hwActive = false;
    int hwNoFrame = 0;                                  // so goi tin da gui ma chua ra khung nao (phan cung) -> that bai im lang
    auto hwTypeName = [&]() -> QString {
        const QString o = opts_.hwDecode.trimmed().toLower();
        if (o == "off" || o == "0" || o == "none" || o == "false") return {};
        if (o == "auto" || o.isEmpty()) {
#ifdef _WIN32
            return "d3d11va";
#else
            return {};
#endif
        }
        return o;
    };
    auto openCodec = [&](bool wantHw, QString* why) -> bool {
        avcodec_free_context(&cc);
        av_buffer_unref(&hwDev);
        hwActive = false;
        cc = avcodec_alloc_context3(codec);
        if (!cc || avcodec_parameters_to_context(cc, st->codecpar) < 0) { if (why) *why = "khong tao duoc context"; return false; }
        if (wantHw) {
            const QString tn = hwTypeName();
            const AVHWDeviceType type = av_hwdevice_find_type_by_name(tn.toUtf8().constData());
            if (type == AV_HWDEVICE_TYPE_NONE) { if (why) *why = "thiet bi '" + tn + "' khong duoc FFmpeg ho tro"; return false; }
            hwPixFmt = AV_PIX_FMT_NONE;
            for (int i = 0;; ++i) {
                const AVCodecHWConfig* c = avcodec_get_hw_config(codec, i);
                if (!c) break;
                if ((c->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && c->device_type == type) { hwPixFmt = c->pix_fmt; break; }
            }
            if (hwPixFmt == AV_PIX_FMT_NONE) { if (why) *why = QString("codec %1 khong co %2").arg(codec->name, tn); return false; }
            if (av_hwdevice_ctx_create(&hwDev, type, nullptr, nullptr, 0) < 0) { if (why) *why = "khong tao duoc thiet bi " + tn; return false; }
            cc->hw_device_ctx = av_buffer_ref(hwDev);
            cc->opaque = &hwPixFmt;
            cc->get_format = pickHwFormat;
            cc->extra_hw_frames = 4;                    // du cho hang cho phat lai (khung duoc chep xuong RAM ngay nen it)
        } else {
            cc->thread_count = 0;
            cc->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        }
        if (avcodec_open2(cc, codec, nullptr) < 0) { if (why) *why = "avcodec_open2 that bai"; return false; }
        hwActive = wantHw;
        return true;
    };
    {
        QString why;
        bool ok = false;
        if (!hwTypeName().isEmpty()) {
            ok = openCodec(true, &why);
            if (!ok) qInfo().noquote() << QString("Giải mã phần cứng không dùng được cho '%1' (%2) → phần mềm").arg(name_, why);
        }
        if (!ok && !openCodec(false, nullptr)) {
            setError(LC_TR("Khong mo duoc decoder"));
            return cleanup();
        }
    }
    // HAP truc tiep: can S3TC tren GPU (Compositor::init() ghi HapSupport) - doi toi ~1,5 s neu engine chua kip khoi dong
    bool hapDirect = false, hapWarned = false;
    if (opts_.hapDirect && st->codecpar->codec_id == AV_CODEC_ID_HAP && cc->width % 4 == 0 && cc->height % 4 == 0) {
        for (int i = 0; i < 150 && !stop_ && HapSupport::state() == HapSupport::Unknown; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        hapDirect = HapSupport::state() == HapSupport::Yes;
        if (!hapDirect) qInfo().noquote() << QString("HAP '%1': GPU khong co S3TC (hoac chua biet) → giai ma bang FFmpeg").arg(name_);
    }
    hwActive_ = hwActive;
    gpuColorActive_ = opts_.gpuColor;
    auto setInfo = [&](const char* swFmt) {
        const QString hwName = hwActive ? hwTypeName().toUpper() + " (phần cứng)" : QString("phần mềm");
        const QString path = !opts_.gpuColor ? "RGBA (CPU, sws_scale)" : QString("%1 → shader").arg(swFmt);
        std::lock_guard lk(m_);
        info_ = QString("%1 → %2").arg(hwName, path);
    };
    auto fallbackToSoftware = [&](const char* reason) {   // loi phan cung giua chung: mo lai bang phan mem, ve dau file
        qWarning().noquote() << QString("Giải mã phần cứng lỗi '%1' (%2) → chuyển sang phần mềm").arg(name_, reason);
        if (!openCodec(false, nullptr)) { setError(LC_TR("Khong mo duoc decoder (phan mem)")); stop_ = true; return; }
        hwActive_ = false;
        restartReq_ = true;
    };

    // am thanh (tuy chon): khong co / khong giai ma duoc -> nguon im lang, KHONG bao loi
    int as = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, vs, nullptr, 0);
    AVStream* ast = nullptr;
    if (as >= 0) {
        ast = fmt->streams[as];
        const AVCodec* acodec = avcodec_find_decoder(ast->codecpar->codec_id);
        ac = acodec ? avcodec_alloc_context3(acodec) : nullptr;
        if (!ac || avcodec_parameters_to_context(ac, ast->codecpar) < 0 || avcodec_open2(ac, acodec, nullptr) < 0) {
            avcodec_free_context(&ac);
            as = -1;
        }
    }
    const bool haveAudio = ac != nullptr;
    hasAudio_ = haveAudio;

    int streamRotation = 0;
    if (const AVPacketSideData* sd = av_packet_side_data_get(st->codecpar->coded_side_data,
                                                             st->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX))
        streamRotation = rotationFromMatrix(sd->data, sd->size);

    const double fps = av_q2d(av_guess_frame_rate(fmt, st, nullptr));
    const double frameDur = fps > 1 ? 1.0 / fps : 1.0 / 25.0;
    const double vtb = av_q2d(st->time_base);
    const double atb = ast ? av_q2d(ast->time_base) : 0;
    const double startSec = st->start_time != AV_NOPTS_VALUE ? double(st->start_time) * vtb : 0.0;     // PTS cua khung dau

    // ----- tua -----
    constexpr double kNoSkip = -1e18;
    double skipUntil = kNoSkip;          // PTS: sau khi tua toi giua file, bo moi khung truoc moc nay (av_seek chi toi duoc keyframe)
    bool needFrame = false;              // vua tua: dua ra ngay mot khung, ke ca khi dang tam dung

    // ----- trang thai dong ho -----
    // "Dong thoi gian lien tuc" (cont): am thanh ghi mau theo aCont(); hinh co cont = pts + vOffset, vOffset dat o khung am thanh dau
    // tien cua moi doan (dau file / sau seek / sau moi vong lap) de giu nguyen do lech hinh-tieng vao cua file.
    double aBase = 0;                    // cont cua mau dau tien sau lan flush gan nhat
    uint64_t aConv = 0;                  // so khung am thanh da doi (ke ca chua day vao ring)
    std::vector<float> backlog;          // am thanh da doi nhung ring chua con cho
    size_t backlogOff = 0;               // khung (x2 float) da day
    auto backlogFrames = [&] { return backlog.size() / kAudioChannels - backlogOff; };
    auto aCont = [&] { return aBase + double(aConv) / kAudioRate; };
    bool offsetKnown = false, awaitAudio = haveAudio;
    double vOffset = 0, lastCont = 0, lastApts = 0, lastVpts = 0;
    double clockPos = 0, lastHp = -1e9;
    Clock::time_point lastIter = Clock::now(), lastHpAdvance = lastIter, segStart = lastIter;
    bool eof = false, drainedAtEof = false;
    AVSampleFormat swrFmt = AV_SAMPLE_FMT_NONE;
    int swrRate = 0, swrCh = 0;

    auto resolveOffset = [&](double v) {
        vOffset = v;
        offsetKnown = true;
        awaitAudio = false;
        for (Pend& p : pend) if (!p.hasCont) { p.cont = p.pts + vOffset; p.hasCont = true; }
    };
    auto newSegment = [&] {                           // sau seek / lap: cho khung am thanh dau de dat lai vOffset
        offsetKnown = false;
        awaitAudio = haveAudio;
        segStart = Clock::now();
        eof = false;
        drainedAtEof = false;
    };
    auto fallbackOffset = [&](double firstPts) {
        return (haveAudio ? aCont() : std::max(clockPos, lastCont + frameDur)) - firstPts;
    };

    // ----- am thanh: doi -> backlog -> ring -----
    auto pumpBacklog = [&] {
        if (backlogFrames() == 0) { backlog.clear(); backlogOff = 0; return; }
        // Khong ai lay am thanh (mixer chua chay / nguon khong nam trong Scene nao): chi giu ~100 ms moi nhat,
        // de khi mixer bat dau khong phat am thanh cu va thoi gian khong lech so voi hinh.
        if (!audio_->consumerActive() && audio_->queued() + backlogFrames() > size_t(kAudioRate / 10)) {
            const double front = aCont() - double(backlogFrames()) / kAudioRate;
            audio_->flush(front);
            aBase = front;
            aConv = backlogFrames();
        }
        const size_t n = audio_->push(backlog.data() + backlogOff * kAudioChannels, backlogFrames());
        backlogOff += n;
        if (backlogFrames() == 0) { backlog.clear(); backlogOff = 0; }
    };
    auto convertAudio = [&](AVFrame* af) {            // af -> float32 stereo 48 kHz noi vao backlog (af == nullptr: xa not swresample)
        if (af) {
            if (af->sample_rate <= 0 || af->ch_layout.nb_channels <= 0) return;
            if (!swr || swrFmt != AVSampleFormat(af->format) || swrRate != af->sample_rate || swrCh != af->ch_layout.nb_channels) {
                swr_free(&swr);
                AVChannelLayout in{}, out{};
                av_channel_layout_copy(&in, &af->ch_layout);
                if (in.order == AV_CHANNEL_ORDER_UNSPEC) av_channel_layout_default(&in, in.nb_channels);
                av_channel_layout_default(&out, kAudioChannels);
                const int r = swr_alloc_set_opts2(&swr, &out, AV_SAMPLE_FMT_FLT, kAudioRate, &in, AVSampleFormat(af->format),
                                                  af->sample_rate, 0, nullptr);
                av_channel_layout_uninit(&in);
                av_channel_layout_uninit(&out);
                if (r < 0 || swr_init(swr) < 0) { swr_free(&swr); return; }
                swrFmt = AVSampleFormat(af->format); swrRate = af->sample_rate; swrCh = af->ch_layout.nb_channels;
            }
        }
        if (!swr) return;
        const int cap = swr_get_out_samples(swr, af ? af->nb_samples : 0) + 64;
        const size_t at = backlog.size();
        backlog.resize(at + size_t(cap) * kAudioChannels);
        uint8_t* outp = reinterpret_cast<uint8_t*>(backlog.data() + at);
        const int got = swr_convert(swr, &outp, cap, af ? const_cast<const uint8_t**>(af->extended_data) : nullptr, af ? af->nb_samples : 0);
        backlog.resize(at + size_t(std::max(0, got)) * kAudioChannels);
        if (got > 0) aConv += uint64_t(got);
    };
    auto drainAudio = [&] {
        while (!stop_ && avcodec_receive_frame(ac, afr) == 0) {
            double pts = afr->pts == AV_NOPTS_VALUE ? lastApts : double(afr->pts) * atb;
            lastApts = pts + double(afr->nb_samples) / std::max(1, afr->sample_rate);
            if (lastApts <= skipUntil) { av_frame_unref(afr); continue; }          // doan truoc diem tua
            if (awaitAudio) resolveOffset(aCont() - pts);       // mau dau cua doan nay nam o aCont() (truoc khi cong khung nay)
            convertAudio(afr);
            av_frame_unref(afr);
        }
        pumpBacklog();
    };

    // ----- hinh: giai ma vao hang cho (pend) -----
    auto enqueue = [&](Pend& p) {
        if (!haveAudio && !offsetKnown) resolveOffset(fallbackOffset(p.pts));      // khong co tieng: dong ho tuong
        if (offsetKnown) { p.cont = p.pts + vOffset; p.hasCont = true; }
        pend.push_back(p);
    };
    auto drainVideo = [&] {
        while (!stop_) {
            const int rr = avcodec_receive_frame(cc, fr);
            if (rr < 0) {
                if (rr != AVERROR(EAGAIN) && rr != AVERROR_EOF && hwActive) fallbackToSoftware("receive_frame");
                break;
            }
            hwNoFrame = 0;
            const double pts = fr->best_effort_timestamp == AV_NOPTS_VALUE ? lastVpts + frameDur : double(fr->best_effort_timestamp) * vtb;
            lastVpts = pts;
            if (pts < skipUntil - frameDur * 0.5) { av_frame_unref(fr); continue; }      // chi giai ma, khong chep/khong dua ra
            Pend p;
            if (hwActive && fr->format == int(hwPixFmt)) {      // khung o VRAM: chep xuong RAM (NV12/P010) ngay de tra khung cho bo giai ma
                AVFrame* sw = av_frame_alloc();
                if (!sw || av_hwframe_transfer_data(sw, fr, 0) < 0) {
                    av_frame_free(&sw);
                    av_frame_unref(fr);
                    fallbackToSoftware("transfer_data");
                    break;
                }
                av_frame_copy_props(sw, fr);
                p.f = sw;
            } else {
                p.f = av_frame_clone(fr);
            }
            p.pts = pts;
            av_frame_unref(fr);
            if (!p.f) continue;
            enqueue(p);
        }
    };
    auto clearPending = [&] {
        for (Pend& p : pend) av_frame_free(&p.f);
        pend.clear();
    };
    auto resetAudioState = [&] {
        swr_free(&swr);
        if (ac) avcodec_flush_buffers(ac);
        backlog.clear(); backlogOff = 0;
        aBase = 0; aConv = 0; lastApts = 0;
        audio_->flush(0);
    };

    int infoFmt = -2;
    bool infoHw = false, infoGpu = false;
    // dua khung den han len `latest_`
    auto present = [&](double clk) {
        Pend last;
        while (!pend.empty() && pend.front().hasCont && pend.front().cont <= clk + 0.002) {
            if (last.f) av_frame_free(&last.f);
            last = pend.front();
            pend.pop_front();
        }
        if (!last.f && !last.hap) return;
        lastCont = last.cont;
        const bool nearStart = last.pts - startSec < 0.5;
        if (seekReq_ < 0 && !restartReq_ && !rewindReq_) position_ = std::max(0.0, last.pts - startSec);   // khong ghi de diem vua yeu cau
        if (last.hap) {                                       // HAP truc tiep: khong doi mau tren CPU
            auto out = std::make_shared<Frame>();
            out->w = last.hap->w; out->h = last.hap->h;
            out->rotation = streamRotation;
            out->hap = std::move(last.hap);
            out->seq = ++seq;
            if (infoFmt != -3) {
                infoFmt = -3;
                const char* n = out->hap->format == HapFormat::Dxt1 ? "HAP (DXT1)" : out->hap->format == HapFormat::Dxt5 ? "HAP Alpha (DXT5)" : "HAP Q (YCoCg DXT5)";
                {
                    std::lock_guard lk(m_);
                    info_ = QString("%1 → GPU trực tiếp (không giải mã CPU)").arg(n);
                }
                qInfo().noquote() << QString("Video '%1': %2").arg(name_, decodeInfo());
            }
            publish(std::move(out), nearStart);
            presentedCont_ = last.cont;
            lateMs_ = (clk - last.cont) * 1000.0;
            return;
        }
        AVFrame* f = last.f;
        if (infoFmt != f->format || infoHw != hwActive || infoGpu != opts_.gpuColor) {
            infoFmt = f->format; infoHw = hwActive; infoGpu = opts_.gpuColor;
            setInfo(pixName(f->format));
            qInfo().noquote() << QString("Video '%1': giải mã %2").arg(name_, decodeInfo());
        }
        auto out = std::make_shared<Frame>();
        out->w = f->width; out->h = f->height;
        out->rotation = streamRotation;
        if (const AVFrameSideData* fd = av_frame_get_side_data(f, AV_FRAME_DATA_DISPLAYMATRIX))
            out->rotation = rotationFromMatrix(fd->data, fd->size);
        bool ok = false;
        YuvFormat yf;
        if (opts_.gpuColor && yuvFormatOf(f, yf)) {
            // duong GPU: chep plane (bo padding) -> shader doi mau. Rat re so voi sws_scale RGBA (1,5 byte/diem anh thay vi 4 + phep nhan).
            auto img = pool_->get(pool_);
            img->w = f->width; img->h = f->height; img->fmt = yf;
            const int n = yf.layout == YuvLayout::SemiPlanar ? 2 : 3;
            for (int i = 0; i < 3; ++i) img->plane[i].resize(i < n ? img->planeBytes(i) : 0);
            const int bps = yf.bytesPerSample();
            for (int i = 0; i < n; ++i) {
                const int pw = (i == 0 ? f->width : img->chromaW()), ph = (i == 0 ? f->height : img->chromaH());
                const size_t rowBytes = size_t(pw) * bps * (yf.layout == YuvLayout::SemiPlanar && i == 1 ? 2 : 1);
                uint8_t* dst = img->plane[i].data();
                for (int y = 0; y < ph; ++y) std::memcpy(dst + rowBytes * y, f->data[i] + ptrdiff_t(f->linesize[i]) * y, rowBytes);
            }
            out->yuv = std::move(img);
            ok = true;
        }
        if (!ok) {
            sws = sws_getCachedContext(sws, f->width, f->height, AVPixelFormat(f->format), f->width, f->height, AV_PIX_FMT_RGBA,
                                       SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (sws) {
                const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(AVPixelFormat(f->format));
                if (d && !(d->flags & AV_PIX_FMT_FLAG_RGB) && d->nb_components >= 3) {      // YUV: dung cung ma tran/khoang voi shader (mac dinh cua sws la 601 gioi han)
                    const YuvMatrix m = matrixOf(f);
                    const int cs = m == YuvMatrix::BT709 ? SWS_CS_ITU709 : m == YuvMatrix::BT2020 ? SWS_CS_BT2020 : SWS_CS_ITU601;
                    const bool full = fullRangeOf(f);
                    sws_setColorspaceDetails(sws, sws_getCoefficients(cs), full ? 1 : 0, sws_getCoefficients(cs), 1, 0, 1 << 16, 1 << 16);
                }
                out->rgba.resize(size_t(out->w) * out->h * 4);
                uint8_t* dst[4] = {out->rgba.data(), nullptr, nullptr, nullptr};
                int ls[4] = {out->w * 4, 0, 0, 0};
                sws_scale(sws, f->data, f->linesize, 0, f->height, dst, ls);
                ok = true;
            }
        }
        if (ok) {
            out->seq = ++seq;
            publish(std::move(out), nearStart);
        }
        presentedCont_ = last.cont;
        lateMs_ = (clk - last.cont) * 1000.0;
        av_frame_free(&last.f);
    };

    while (!stop_) {
        const bool restarted = restartReq_.exchange(false);
        const bool rewound = rewindReq_.exchange(false);
        const double seek = seekReq_.exchange(-1.0);
        if (restarted || rewound || seek >= 0) {           // phat lai tu dau (Stinger, playlist, len Program) hoac tua
            {
                std::lock_guard lk(m_);
                if (restarted && waitFirst_) latest_.reset();   // bo khung cu co the vua dua len sau restart()
                rewindPending_ = false;
            }
            // sat cuoi file khong con khung nao de hien: lui lai mot chut (seekTo sau restart/rewind thang)
            const double target = seek > 0 ? std::min(seek, std::max(0.0, double(duration_) - std::max(0.1, 2 * frameDur))) : 0.0;
            av_seek_frame(fmt, vs, target > 0 ? int64_t((startSec + target) / vtb) : 0, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(cc);
            clearPending();
            resetAudioState();
            clockPos = 0; lastCont = 0;
            newSegment();
            skipUntil = target > 0 ? startSec + target : kNoSkip;
            needFrame = true;
            ended_ = false;
            continue;
        }

        // 1) dong ho
        const auto tNow = Clock::now();
        const double dt = std::chrono::duration<double>(tNow - lastIter).count();
        lastIter = tNow;
        const bool paused = paused_;
        bool useAudio = false;
        if (paused) {
            lastHpAdvance = tNow;                          // dong ho dung han
        } else {
            if (haveAudio && audio_->consumerActive()) {
                const double hp = audio_->heardPosition();
                if (hp != lastHp) { lastHp = hp; lastHpAdvance = tNow; }
                useAudio = tNow - lastHpAdvance < std::chrono::milliseconds(250);      // am thanh dung (underrun) -> dong ho tuong
                if (useAudio) clockPos = hp;
            }
            if (!useAudio) clockPos += dt;
        }
        followAudio_ = useAudio;

        // 2) doc goi tin tiep theo khi con cho (hang cho hinh chua day, am thanh khong ket)
        const bool audioLow = haveAudio && audio_->consumerActive() && audio_->queued() < size_t(kAudioRate / 20);
        const size_t cap = audioLow ? 30 : 6;
        bool didWork = false;
        if (haveAudio) pumpBacklog();
        if (!eof && backlogFrames() == 0 && pend.size() < cap) {
            const int r = av_read_frame(fmt, pkt);
            didWork = true;
            if (r < 0) {
                eof = true;
            } else {
                if (pkt->stream_index == vs && hapDirect) {
                    HapFrame hf;
                    QString herr;
                    if (hapParseFrame(pkt->data, size_t(pkt->size), cc->width, cc->height, hf, &herr)) {
                        Pend p;
                        p.hap = std::make_shared<HapFrame>(std::move(hf));
                        p.pts = pkt->pts == AV_NOPTS_VALUE ? lastVpts + frameDur : double(pkt->pts) * vtb;
                        lastVpts = p.pts;
                        if (p.pts >= skipUntil - frameDur * 0.5) enqueue(p);
                    } else {                                  // khung khong ho tro: dung bo giai ma FFmpeg (co the lan voi khung truc tiep, HAP la intra-only)
                        if (!hapWarned) { hapWarned = true; qInfo().noquote() << QString("HAP '%1': %2 → bo giai ma FFmpeg").arg(name_, herr); }
                        avcodec_send_packet(cc, pkt);
                        drainVideo();
                    }
                } else if (pkt->stream_index == vs) {
                    avcodec_send_packet(cc, pkt);
                    if (hwActive && ++hwNoFrame > 60) fallbackToSoftware("khong ra khung");   // phan cung nuot goi tin nhung khong tra hinh
                    drainVideo();
                } else if (ac && pkt->stream_index == as) {
                    avcodec_send_packet(ac, pkt);
                    drainAudio();
                }
                av_packet_unref(pkt);
            }
        }
        if (eof && !drainedAtEof) {                        // xa not frame con ket trong decoder
            avcodec_send_packet(cc, nullptr);
            drainVideo();
            if (ac) {
                avcodec_send_packet(ac, nullptr);
                drainAudio();
                convertAudio(nullptr);
                pumpBacklog();
            }
            avcodec_flush_buffers(cc);                      // de lan seek ke tiep nhan goi tin moi (sau EOF decoder o che do drain)
            if (ac) avcodec_flush_buffers(ac);
            drainedAtEof = true;
            didWork = true;
        }
        if (eof && drainedAtEof && loop_ && pend.size() < cap && backlogFrames() == 0 &&
            av_seek_frame(fmt, vs, 0, AVSEEK_FLAG_BACKWARD) >= 0) {
            swr_free(&swr);                                 // am thanh lap: KHONG flush ring (noi lien), chi dat lai bo doi
            lastApts = 0;
            newSegment();
            skipUntil = kNoSkip;
            ended_ = false;
            didWork = true;
        }
        if (eof && drainedAtEof && !loop_ && pend.empty() && backlogFrames() == 0 && seekReq_ < 0 && !ended_) {
            ended_ = true;                                  // khong lap: giu khung cuoi
            position_ = double(duration_);
        }
        // khung am thanh dau cua doan khong den (stream thua / khong co): dung moc rut ra tu hinh
        if (awaitAudio && !pend.empty() && Clock::now() - segStart > std::chrono::milliseconds(400))
            resolveOffset(fallbackOffset(pend.front().pts));

        // 3) dua khung den han len man hinh
        const size_t before = pend.size();
        // tua khi dang tam dung: dong ho dung nen phai day khung dau tien sau diem tua ra ngay
        if (needFrame && paused && !pend.empty() && pend.front().hasCont) clockPos = pend.front().cont;
        present(clockPos);
        if (pend.size() != before) { didWork = true; needFrame = false; }

        if (!didWork) {
            double waitMs = 2.0;
            if (paused) waitMs = needFrame ? 2.0 : 8.0;
            else if (!pend.empty() && pend.front().hasCont) waitMs = std::min(4.0, std::max(0.5, (pend.front().cont - clockPos) * 1000.0));
            std::this_thread::sleep_for(std::chrono::microseconds(int(waitMs * 1000)));
        }
    }
    cleanup();
}

} // namespace lc
