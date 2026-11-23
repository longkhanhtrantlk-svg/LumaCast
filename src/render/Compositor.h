#pragma once
#include "core/Scene.h"
#include "render/GlHelpers.h"
#include <QOpenGLShaderProgram>
#include <memory>

namespace lc {

// Toan bo ve len GPU: Scene -> FBO, Transition(A,B) -> FBO, Channel -> cua so Output.
class Compositor {
public:
    bool init();                              // can context GL hien hanh; false neu loi shader
    GL& gl() { return gl_; }

    void clear(RenderTarget& dst, const QColor& c);
    void renderScene(const Scene& scene, RenderTarget& dst);
    void renderTransition(const RenderTarget& a, const RenderTarget& b, TransitionType type, float t,
                          RenderTarget& dst);
    // Ve texture Channel vao framebuffer DANG BUOC (cua so hoac FBO) kich thuoc winW x winH.
    void drawToCurrent(GLuint tex, int texW, int texH, const OutputConfig& cfg, int winW, int winH);
    void shutdown();

private:
    void drawQuad(QOpenGLShaderProgram& prog, QVector4D clipRect, QVector4D uvRect, float rotDeg);

    GL gl_;
    GLuint vao_ = 0;
    std::unique_ptr<QOpenGLShaderProgram> blit_, out_, trans_;
};

} // namespace lc
