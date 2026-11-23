#include "FFmpegSource.h"
#include <QFileInfo>
#include <chrono>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace lc {

using Clock = std::chrono::steady_clock;

FFmpegSource::FFmpegSource(QString path, bool loop)
    : path_(std::move(path)), name_(QFileInfo(path_).fileName()), loop_(loop) {
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
    }
    return {tex_.id(), tex_.w(), tex_.h()};
}

void FFmpegSource::run() {
    AVFormatContext* fmt = nullptr;
    AVCodecContext* cc = nullptr;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();
    SwsContext* sws = nullptr;
    uint64_t seq = 0;

    auto cleanup = [&] {
        sws_freeContext(sws);
        av_frame_free(&fr);
        av_packet_free(&pkt);
        avcodec_free_context(&cc);
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

    bool haveBase = false;
    Clock::time_point base;
    double basePts = 0, lastPts = 0;
    const double fps = av_q2d(av_guess_frame_rate(fmt, st, nullptr));
    const double frameDur = fps > 1 ? 1.0 / fps : 1.0 / 25.0;

    // Lay het khung hinh dang cho trong decoder, dong bo theo PTS roi dua len `latest_`.
    auto drain = [&] {
        while (!stop_ && avcodec_receive_frame(cc, fr) == 0) {
            double pts = fr->best_effort_timestamp == AV_NOPTS_VALUE
                             ? lastPts + frameDur
                             : fr->best_effort_timestamp * av_q2d(st->time_base);
            lastPts = pts;
            if (!haveBase) { base = Clock::now(); basePts = pts; haveBase = true; }
            const auto due = base + std::chrono::duration_cast<Clock::duration>(
                                        std::chrono::duration<double>(pts - basePts));
            auto now = Clock::now();
            if (now - due > std::chrono::milliseconds(200)) {      // tre qua -> dong bo lai
                base = now; basePts = pts;
            }
            while (!stop_ && (now = Clock::now()) < due)
                std::this_thread::sleep_for(std::min<Clock::duration>(due - now, std::chrono::milliseconds(4)));
            if (stop_) break;

            sws = sws_getCachedContext(sws, fr->width, fr->height, AVPixelFormat(fr->format), fr->width,
                                       fr->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (sws) {
                auto f = std::make_shared<Frame>();
                f->w = fr->width; f->h = fr->height;
                f->rgba.resize(size_t(f->w) * f->h * 4);
                uint8_t* dst[4] = {f->rgba.data(), nullptr, nullptr, nullptr};
                int ls[4] = {f->w * 4, 0, 0, 0};
                sws_scale(sws, fr->data, fr->linesize, 0, fr->height, dst, ls);
                f->seq = ++seq;
                std::lock_guard lk(m_);
                latest_ = std::move(f);
            }
            av_frame_unref(fr);
        }
    };

    while (!stop_) {
        const int r = av_read_frame(fmt, pkt);
        if (r < 0) {                                   // het file
            avcodec_send_packet(cc, nullptr);          // xa not frame con ket trong decoder
            drain();
            if (!loop_ || stop_) break;
            if (av_seek_frame(fmt, vs, 0, AVSEEK_FLAG_BACKWARD) < 0) break;
            avcodec_flush_buffers(cc);
            haveBase = false;
            continue;
        }
        if (pkt->stream_index == vs) {
            avcodec_send_packet(cc, pkt);
            drain();
        }
        av_packet_unref(pkt);
    }
    cleanup();
}

} // namespace lc
