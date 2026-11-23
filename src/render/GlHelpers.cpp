#include "GlHelpers.h"
#include <QOpenGLFunctions>

namespace lc {

static void setupTex(GL& gl) {
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void TargetPool::beginFrame() {
    for (auto& t : targets_) t->busy = false;
}

RenderTarget* TargetPool::acquire(int w, int h) {
    for (auto& t : targets_)
        if (!t->busy && t->w == w && t->h == h) { t->busy = true; return t.get(); }

    auto t = std::make_unique<RenderTarget>();
    t->w = w; t->h = h; t->busy = true;
    gl_.glGenTextures(1, &t->tex);
    gl_.glBindTexture(GL_TEXTURE_2D, t->tex);
    gl_.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    setupTex(gl_);
    gl_.glGenFramebuffers(1, &t->fbo);
    gl_.glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    gl_.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->tex, 0);
    targets_.push_back(std::move(t));
    return targets_.back().get();
}

void TargetPool::clear() {
    for (auto& t : targets_) {
        gl_.glDeleteFramebuffers(1, &t->fbo);
        gl_.glDeleteTextures(1, &t->tex);
    }
    targets_.clear();
}

GlTexture::~GlTexture() {
    if (id_ && QOpenGLContext::currentContext())
        QOpenGLContext::currentContext()->functions()->glDeleteTextures(1, &id_);
}

void GlTexture::upload(GL& gl, const uint8_t* rgba, int w, int h) {
    const bool fresh = (id_ == 0);
    if (fresh) gl.glGenTextures(1, &id_);
    gl.glBindTexture(GL_TEXTURE_2D, id_);
    gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (fresh || w != w_ || h != h_) {
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        setupTex(gl);
        w_ = w; h_ = h;
    } else {
        gl.glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
}

} // namespace lc
