#include "NdiOutput.h"
#include "media/StaticSources.h"
#include <QDebug>
#include <algorithm>
#include <chrono>
#include <cstring>

namespace lc {

NdiOutputSink::NdiOutputSink(const OutputConfig& cfg, double fps, AudioMixer* mixer)
    : cfg_(cfg), fps_(fps > 1 ? fps : 60.0), mixer_(mixer) {
    cfg_.kind = OutputKind::Ndi;
    cfg_.size = QSize(std::clamp(cfg_.size.width(), 16, 8192), std::clamp(cfg_.size.height(), 16, 8192));
    nameUsed_ = cfg_.ndiName.isEmpty() ? cfg_.name : cfg_.ndiName;
    status_ = "Đang khởi động NDI...";
    if (cfg_.ndiAudio && mixer_) {
        tap_ = std::make_shared<AudioStream>(kAudioRate);
        mixer_->setTap(cfg_.channel, tap_);
    }
    th_ = std::thread([this] { senderMain(); });
}

NdiOutputSink::~NdiOutputSink() {
    if (tap_ && mixer_) mixer_->setTap(cfg_.channel, nullptr);
    stop_ = true;
    cv_.notify_all();
    if (th_.joinable()) th_.join();
}

QString NdiOutputSink::statusText() const {
    std::lock_guard lk(statusM_);
    return status_;
}

void NdiOutputSink::setStatus(const QString& s) {
    std::lock_guard lk(statusM_);
    status_ = s;
}

void NdiOutputSink::shutdown(QOpenGLContext&, Compositor& comp) {
    GL& gl = comp.gl();
    if (fbo_) gl.glDeleteFramebuffers(1, &fbo_);
    if (tex_) gl.glDeleteTextures(1, &tex_);
    for (GLuint& p : pbo_) if (p) { gl.glDeleteBuffers(1, &p); p = 0; }
    fbo_ = tex_ = 0;
    testSize_ = QSize();
}

void NdiOutputSink::present(QOpenGLContext&, Compositor& comp, RenderTarget* src) {
    if (freeze && !black && !testPattern) return;             // khong cap nhat hinh: thread gui giu khung cuoi
    GL& gl = comp.gl();
    const int W = cfg_.size.width(), H = cfg_.size.height();
    if (!fbo_ || glW_ != W || glH_ != H) {
        shutdown(*QOpenGLContext::currentContext(), comp);
        gl.glGenTextures(1, &tex_);
        gl.glBindTexture(GL_TEXTURE_2D, tex_);
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl.glGenFramebuffers(1, &fbo_);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_, 0);
        gl.glGenBuffers(2, pbo_);
        for (GLuint p : pbo_) {
            gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, p);
            gl.glBufferData(GL_PIXEL_PACK_BUFFER, GLsizeiptr(W) * H * 4, nullptr, GL_STREAM_READ);
        }
        gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glW_ = W; glH_ = H;
        pboFilled_[0] = pboFilled_[1] = false;
    }
    gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    if (black || (!src && !testPattern)) {
        gl.glViewport(0, 0, W, H);
        gl.glClearColor(0, 0, 0, 1);
        gl.glClear(GL_COLOR_BUFFER_BIT);
    } else if (testPattern) {
        if (!testTex_.id() || testSize_ != QSize(W, H)) {
            const QImage img = makeOutputTestPattern(W, H, cfg_.name).convertToFormat(QImage::Format_RGBA8888).mirrored(false, true);   // GL: hang dau = day
            testTex_.upload(gl, img.constBits(), W, H);
            testSize_ = QSize(W, H);
            gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        }
        OutputConfig plain;
        plain.scale = ScaleMode::Stretch;
        comp.drawToCurrent(testTex_.id(), W, H, plain, W, H);
    } else {
        comp.drawToCurrent(src->tex, src->w, src->h, cfg_, W, H);
    }
    // doc nguoc bat dong bo: lenh doc khung hien tai vao pbo[i], nhan khung TRUOC tu pbo[1-i]
    gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[pboIdx_]);
    gl.glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    pboFilled_[pboIdx_] = true;
    const int prev = 1 - pboIdx_;
    if (pboFilled_[prev]) {
        gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[prev]);
        if (const uint8_t* p = static_cast<const uint8_t*>(gl.glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY))) {
            back_.resize(size_t(W) * size_t(H) * 4);
            const size_t row = size_t(W) * 4;
            for (int y = 0; y < H; ++y) std::memcpy(&back_[size_t(y) * row], p + size_t(H - 1 - y) * row, row);    // GL goc duoi-trai -> hang dau = tren
            gl.glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            {
                std::lock_guard lk(m_);
                if (slotNew_) ++dropped_;                          // thread gui chua kip lay khung truoc: bo no
                slot_.swap(back_);
                slotW_ = W; slotH_ = H;
                slotNew_ = true;
            }
            cv_.notify_one();
        }
    }
    gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    pboIdx_ = prev;
    ++presented;
}

void NdiOutputSink::senderMain() {
    using Clock = std::chrono::steady_clock;
    std::unique_ptr<NdiSender> snd;
    std::vector<uint8_t> frame;
    std::vector<float> ablk(size_t(kMixBlock) * kAudioChannels);
    int fw = 0, fh = 0;
    auto lastOpenTry = Clock::now() - std::chrono::seconds(10);
    auto lastSend = Clock::now();
    int fpsN = int(fps_ * 1000 + 0.5), fpsD = 1000;
    while (!stop_) {
        if (!snd) {                                              // (chua) co nguoi gui: thu tao, lui 3 giay khi that bai
            if (Clock::now() - lastOpenTry >= std::chrono::seconds(3)) {
                lastOpenTry = Clock::now();
                auto b = ndiBackend();
                QString err;
                if (!b->available()) setStatus(b->unavailableReason());
                else if ((snd = b->createSender(nameUsed_, &err))) { setStatus("Đang phát NDI: " + nameUsed_); sending_ = true; }
                else setStatus("Không phát được NDI: " + err);
            }
        }
        bool haveFrame = false;
        {
            std::unique_lock lk(m_);
            cv_.wait_for(lk, std::chrono::milliseconds(snd ? 5 : 100), [&] { return slotNew_ || stop_; });
            if (slotNew_) {
                frame.swap(slot_);
                fw = slotW_; fh = slotH_;
                slotNew_ = false;
                haveFrame = true;
            }
        }
        if (!snd) continue;
        if (haveFrame || (fw > 0 && Clock::now() - lastSend > std::chrono::seconds(1))) {      // khong co khung moi qua 1 s: gui lai khung cuoi (giu ket noi/nhan thay hinh)
            snd->sendVideo(frame.data(), fw, fh, fw * 4, fpsN, fpsD);
            lastSend = Clock::now();
            ++sentFrames_;
            conns_ = snd->connections();
            setStatus(QString("Đang phát NDI: %1 (%2 người nhận)").arg(nameUsed_).arg(std::max(0, conns_.load())));
        }
        if (tap_) {
            // chi gui am thanh khi co nguoi nhan hoac khong biet (tranh tich tru); lay theo khoi 10 ms
            while (tap_->queued() >= size_t(kMixBlock)) {
                tap_->pull(ablk.data(), size_t(kMixBlock));
                snd->sendAudio(ablk.data(), kMixBlock, kAudioRate, kAudioChannels);
            }
        }
    }
    sending_ = false;
}

} // namespace lc
