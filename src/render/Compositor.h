#pragma once
#include "core/Scene.h"
#include "core/Transition.h"
#include "render/EffectRegistry.h"
#include "render/GlHelpers.h"
#include <QImage>
#include <QOpenGLShaderProgram>
#include <map>
#include <memory>

namespace lc {

// Toan bo ve len GPU: Scene -> FBO, Transition(A,B) -> FBO, Channel -> cua so Output.
// Vung `d` (chuan hoa, goc tren-trai, THEO HINH DA XOAY) -> vung tuong ung tren texture goc (chua xoay).
// rotation: 0/90/180/270 theo chieu kim dong ho (video dien thoai).
QRectF cropToTexture(const QRectF& d, int rotation);

class Compositor {
public:
    bool init();                              // can context GL hien hanh; false neu loi shader
    GL& gl() { return gl_; }

    void clear(RenderTarget& dst, const QColor& c);
    void renderScene(const Scene& scene, RenderTarget& dst);
    void renderTransition(const RenderTarget& a, const RenderTarget& b, TransitionType type, float t,
                          RenderTarget& dst);
    // Day du: ho tro Plugin (push/slide/zoom/luma dung san va plug-in nguoi dung). Plugin thieu/loi shader -> roi ve Fade.
    void renderTransition(const RenderTarget& a, const RenderTarget& b, const TransitionSpec& spec, float t, RenderTarget& dst);
    // Ve mot nguon (co alpha) de len toan bo dst: dung cho Stinger.
    void drawOverlay(Source& src, RenderTarget& dst);

    // Ngu canh cho Effect: kho dang ky, ho FBO tam, dong ho (uTime). Engine dat truoc moi khung hinh.
    void setEffectContext(EffectRegistry* reg, TargetPool* pool) { registry_ = reg; pool_ = pool; }
    void setTime(double seconds) { time_ = seconds; }
    // Ve texture Channel vao framebuffer DANG BUOC (cua so hoac FBO) kich thuoc winW x winH.
    void drawToCurrent(GLuint tex, int texW, int texH, const OutputConfig& cfg, int winW, int winH);
    // Thu nho texture (toi da maxW rong) va doc ve CPU cho monitor UI. Chay tren render thread.
    QImage thumbnail(TargetPool& pool, const RenderTarget& src, int maxW);
    // Phu den len dst voi do mo `amount` (0..1): dung cho Fade-To-Black.
    void fadeToBlack(RenderTarget& dst, float amount);
    void shutdown();

private:
    void drawQuad(QOpenGLShaderProgram& prog, QVector4D clipRect, QVector4D uvRect, float rotDeg, float quadRotDeg = 0, float aspect = 1);
    QOpenGLShaderProgram* effectProgram(const EffectDef& d);          // bien dich lan dau, nho theo (id, revision); nullptr neu loi
    void setParamUniforms(QOpenGLShaderProgram& prog, const EffectDef& d, const QVariantMap& values);
    RenderTarget* applyLayerEffects(const Layer& l, const TexRef& tex);   // nullptr neu khong co Effect nao dang chay

    GL gl_;
    GLuint vao_ = 0;
    std::unique_ptr<QOpenGLShaderProgram> blit_, out_, trans_, solid_;
    EffectRegistry* registry_ = nullptr;
    TargetPool* pool_ = nullptr;
    double time_ = 0;
    std::map<QString, std::unique_ptr<QOpenGLShaderProgram>> fxCache_;
};

} // namespace lc
