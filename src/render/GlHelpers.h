#pragma once
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <memory>
#include <vector>

namespace lc {

using GL = QOpenGLFunctions_3_3_Core;

struct RenderTarget {
    GLuint fbo = 0, tex = 0;
    int w = 0, h = 0;
    bool busy = false;
};

// Ho FBO: tai su dung theo (w,h) de khong cap phat moi khung hinh.
class TargetPool {
public:
    explicit TargetPool(GL& gl) : gl_(gl) {}
    void beginFrame();                       // danh dau tat ca la ranh
    RenderTarget* acquire(int w, int h);     // hop le den het khung hinh
    void clear();                            // xoa tai nguyen GL (can context hien hanh)
private:
    GL& gl_;
    std::vector<std::unique_ptr<RenderTarget>> targets_;
};

// Texture RGBA8 cho nguon; tu giai phong neu con context hien hanh.
class GlTexture {
public:
    GlTexture() = default;
    ~GlTexture();
    GlTexture(const GlTexture&) = delete;
    GlTexture& operator=(const GlTexture&) = delete;
    void upload(GL& gl, const uint8_t* rgba, int w, int h);
    GLuint id() const { return id_; }
    int w() const { return w_; }
    int h() const { return h_; }
private:
    GLuint id_ = 0;
    int w_ = 0, h_ = 0;
};

} // namespace lc
