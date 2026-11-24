#include "FFmpegSource.h"
#include <QFileInfo>
#include <chrono>
#include <cmath>
#include <deque>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
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

FFmpegSource::FFmpegSource(QString path, bool loop)
    : path_(std::move(path)), name_(QFileInfo(path_).fileName()), loop_(loop) {
    audio_ = std::make_unique<AudioStream>();
    thread_ = std::thread([this] { run(); });
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
        tex_.upload(gl, f->rgba.data(), f->w, f->h);
        uploaded_ = f->seq;
        rotation_ = f->rotation;
        waitFirst_ = false;
    }
    if (waitFirst_) return {};
    return {tex_.id(), tex_.w(), tex_.h(), rotation_};
}

QImage FFmpegSource::snapshot(int maxWidth) const {
    std::shared_ptr<Frame> f;
    {
        std::lock_guard lk(m_);
        f = latest_;
    }
    if (!f) return {};
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
    restartReq_ = true;
    audio_->holdUntilFlush();                  // im lang ngay (khong phat not am thanh cu) cho den khi thread giai ma seek + flush
}

namespace {
struct Pend {
    AVFrame* f = nullptr;
    double pts = 0;          // PTS goc cua file (giay)
    double cont = 0;         // moc tren dong thoi gian lien tuc (hop le khi hasCont)
    bool hasCont = false;
};
} // namespace

void FFmpegSource::run() {
    AVFormatContext* fmt = nullptr;
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
        avformat_close_input(&fmt);
    };

    if (avformat_open_input(&fmt, path_.toUtf8().constData(), nullptr, nullptr) < 0) {
        setError("Khong mo duoc file: " + path_);
        return cleanup();
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        setError("Khong doc duoc thong tin stream");
        return cleanup();
    }
    const int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vs < 0) {
        setError("Khong co stream video");
        return cleanup();
    }
    AVStream* st = fmt->streams[vs];
    if (fmt->duration > 0) duration_ = double(fmt->duration) / AV_TIME_BASE;
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    cc = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!cc || avcodec_parameters_to_context(cc, st->codecpar) < 0) {
        setError("Khong ho tro codec");
        return cleanup();
    }
    cc->thread_count = 0;
    cc->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    if (avcodec_open2(cc, codec, nullptr) < 0) {
        setError("Khong mo duoc decoder");
        return cleanup();
    }

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
            if (awaitAudio) resolveOffset(aCont() - pts);       // mau dau cua doan nay nam o aCont() (truoc khi cong khung nay)
            convertAudio(afr);
            av_frame_unref(afr);
        }
        pumpBacklog();
    };

    // ----- hinh: giai ma vao hang cho (pend) -----
    auto drainVideo = [&] {
        while (!stop_ && avcodec_receive_frame(cc, fr) == 0) {
            const double pts = fr->best_effort_timestamp == AV_NOPTS_VALUE ? lastVpts + frameDur : double(fr->best_effort_timestamp) * vtb;
            lastVpts = pts;
            Pend p;
            p.f = av_frame_clone(fr);
            p.pts = pts;
            av_frame_unref(fr);
            if (!p.f) continue;
            if (!haveAudio && !offsetKnown) resolveOffset(fallbackOffset(pts));      // khong co tieng: dong ho tuong
            if (offsetKnown) { p.cont = pts + vOffset; p.hasCont = true; }
            pend.push_back(p);
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

    // dua khung den han len `latest_`
    auto present = [&](double clk) {
        Pend last;
        while (!pend.empty() && pend.front().hasCont && pend.front().cont <= clk + 0.002) {
            if (last.f) av_frame_free(&last.f);
            last = pend.front();
            pend.pop_front();
        }
        if (!last.f) return;
        lastCont = last.cont;
        AVFrame* f = last.f;
        sws = sws_getCachedContext(sws, f->width, f->height, AVPixelFormat(f->format), f->width, f->height, AV_PIX_FMT_RGBA,
                                   SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (sws) {
            auto out = std::make_shared<Frame>();
            out->w = f->width; out->h = f->height;
            out->rotation = streamRotation;
            if (const AVFrameSideData* fd = av_frame_get_side_data(f, AV_FRAME_DATA_DISPLAYMATRIX))
                out->rotation = rotationFromMatrix(fd->data, fd->size);
            out->rgba.resize(size_t(out->w) * out->h * 4);
            uint8_t* dst[4] = {out->rgba.data(), nullptr, nullptr, nullptr};
            int ls[4] = {out->w * 4, 0, 0, 0};
            sws_scale(sws, f->data, f->linesize, 0, f->height, dst, ls);
            out->seq = ++seq;
            std::lock_guard lk(m_);
            latest_ = std::move(out);
        }
        presentedCont_ = last.cont;
        lateMs_ = (clk - last.cont) * 1000.0;
        av_frame_free(&last.f);
    };

    while (!stop_) {
        if (restartReq_.exchange(false)) {                 // phat lai tu dau (Stinger)
            av_seek_frame(fmt, vs, 0, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(cc);
            clearPending();
            resetAudioState();
            clockPos = 0; lastCont = 0;
            newSegment();
            continue;
        }

        // 1) dong ho
        const auto tNow = Clock::now();
        const double dt = std::chrono::duration<double>(tNow - lastIter).count();
        lastIter = tNow;
        bool useAudio = false;
        if (haveAudio && audio_->consumerActive()) {
            const double hp = audio_->heardPosition();
            if (hp != lastHp) { lastHp = hp; lastHpAdvance = tNow; }
            useAudio = tNow - lastHpAdvance < std::chrono::milliseconds(250);      // am thanh dung (underrun) -> dong ho tuong
            if (useAudio) clockPos = hp;
        }
        if (!useAudio) clockPos += dt;
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
                if (pkt->stream_index == vs) {
                    avcodec_send_packet(cc, pkt);
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
            didWork = true;
        }
        // khung am thanh dau cua doan khong den (stream thua / khong co): dung moc rut ra tu hinh
        if (awaitAudio && !pend.empty() && Clock::now() - segStart > std::chrono::milliseconds(400))
            resolveOffset(fallbackOffset(pend.front().pts));

        // 3) dua khung den han len man hinh
        const size_t before = pend.size();
        present(clockPos);
        if (pend.size() != before) didWork = true;

        if (!didWork) {
            double waitMs = 2.0;
            if (!pend.empty() && pend.front().hasCont) waitMs = std::min(4.0, std::max(0.5, (pend.front().cont - clockPos) * 1000.0));
            std::this_thread::sleep_for(std::chrono::microseconds(int(waitMs * 1000)));
        }
    }
    cleanup();
}

} // namespace lc
