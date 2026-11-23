#include "Compositor.h"
#include <QMatrix2x2>
#include <QVector3D>
#include <QVector4D>
#include <QtMath>
#include <algorithm>

namespace lc {

// Quad 4 dinh sinh bang gl_VertexID (khong can VBO).
// uRect = (tam x, tam y, nua rong, nua cao) trong clip space; uUvRect = (u@day, v@day, u@dinh, v@dinh).
static const char* kVert = R"(#version 330 core
uniform vec4 uRect;
uniform vec4 uUvRect;
uniform mat2 uUvRot;
out vec2 vUv;
void main() {
    vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    gl_Position = vec4(uRect.xy + (p * 2.0 - 1.0) * uRect.zw, 0.0, 1.0);
    vec2 q = uUvRot * (p - 0.5) + 0.5;
    vUv = mix(uUvRect.xy, uUvRect.zw, q);
})";

static const char* kFragBlit = R"(#version 330 core
uniform sampler2D uTex;
uniform float uOpacity;
in vec2 vUv;
out vec4 o;
void main() {
    vec4 c = texture(uTex, vUv);
    o = vec4(c.rgb, c.a * uOpacity);
})";

// Shader cuoi cung cua Output: hieu chinh tuong phan/do sang/gain/gamma.
static const char* kFragOut = R"(#version 330 core
uniform sampler2D uTex;
uniform float uBrightness;
uniform float uContrast;
uniform float uGamma;
uniform vec3 uGain;
in vec2 vUv;
out vec4 o;
void main() {
    vec3 c = texture(uTex, vUv).rgb;
    c = (c - 0.5) * uContrast + 0.5;
    c *= uBrightness * uGain;
    c = pow(clamp(c, 0.0, 1.0), vec3(1.0 / uGamma));
    o = vec4(c, 1.0);
})";

static const char* kFragSolid = R"(#version 330 core
uniform vec4 uColor;
in vec2 vUv;
out vec4 o;
void main() { o = uColor; })";

// Mode: 1 fade, 2 dip-to-black, 3 wipe-left, 4 wipe-right, 5 wipe-up, 6 wipe-down.
static const char* kFragTrans = R"(#version 330 core
uniform sampler2D uA;
uniform sampler2D uB;
uniform float uT;
uniform int uMode;
in vec2 vUv;
out vec4 o;
void main() {
    vec4 a = texture(uA, vUv);
    vec4 b = texture(uB, vUv);
    vec4 r;
    if (uMode == 1)      r = mix(a, b, uT);
    else if (uMode == 2) r = uT < 0.5 ? mix(a, vec4(0.0, 0.0, 0.0, 1.0), uT * 2.0)
                                      : mix(vec4(0.0, 0.0, 0.0, 1.0), b, (uT - 0.5) * 2.0);
    else if (uMode == 3) r = vUv.x > 1.0 - uT ? b : a;
    else if (uMode == 4) r = vUv.x < uT ? b : a;
    else if (uMode == 5) r = vUv.y < uT ? b : a;
    else                 r = vUv.y > 1.0 - uT ? b : a;
    o = vec4(r.rgb, 1.0);
})";

static std::unique_ptr<QOpenGLShaderProgram> makeProgram(const char* frag) {
    auto p = std::make_unique<QOpenGLShaderProgram>();
    if (!p->addShaderFromSourceCode(QOpenGLShader::Vertex, kVert) ||
        !p->addShaderFromSourceCode(QOpenGLShader::Fragment, frag) || !p->link()) {
        qCritical().noquote() << "Shader loi:" << p->log();
        return nullptr;
    }
    return p;
}

bool Compositor::init() {
    if (!gl_.initializeOpenGLFunctions()) return false;
    gl_.glGenVertexArrays(1, &vao_);
    blit_ = makeProgram(kFragBlit);
    out_ = makeProgram(kFragOut);
    trans_ = makeProgram(kFragTrans);
    solid_ = makeProgram(kFragSolid);
    return blit_ && out_ && trans_ && solid_;
}

void Compositor::shutdown() {
    blit_.reset();
    out_.reset();
    trans_.reset();
    solid_.reset();
    if (vao_) gl_.glDeleteVertexArrays(1, &vao_);
    vao_ = 0;
}

void Compositor::drawQuad(QOpenGLShaderProgram& prog, QVector4D clipRect, QVector4D uvRect, float rotDeg) {
    const float a = qDegreesToRadians(rotDeg), c = std::cos(a), s = std::sin(a);
    const float m[4] = {c, -s, s, c};   // hang truoc; quay nguoc chieu kim dong ho goc `a` tren toa do uv
    prog.setUniformValue("uRect", clipRect);
    prog.setUniformValue("uUvRect", uvRect);
    prog.setUniformValue("uUvRot", QMatrix2x2(m));
    gl_.glBindVertexArray(vao_);
    gl_.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Compositor::clear(RenderTarget& dst, const QColor& c) {
    gl_.glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
    gl_.glViewport(0, 0, dst.w, dst.h);
    gl_.glClearColor(c.redF(), c.greenF(), c.blueF(), c.alphaF());
    gl_.glClear(GL_COLOR_BUFFER_BIT);
}

void Compositor::renderScene(const Scene& scene, RenderTarget& dst) {
    clear(dst, scene.background);
    gl_.glEnable(GL_BLEND);
    gl_.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    blit_->bind();
    blit_->setUniformValue("uTex", 0);
    gl_.glActiveTexture(GL_TEXTURE0);

    const double W = dst.w, H = dst.h;
    for (const Layer& l : scene.layers) {
        if (!l.visible || !l.source) continue;
        const TexRef tex = l.source->acquire(gl_);
        if (!tex.id || tex.w <= 0 || tex.h <= 0) continue;

        double px = l.rect.x() * W, py = l.rect.y() * H, pw = l.rect.width() * W, ph = l.rect.height() * H;
        // Video dien thoai: texture luu ngang nhung hien thi xoay 90/270 -> ti le hieu dung bi hoan doi.
        const bool sideways = (tex.rotation / 90) % 2 != 0;
        const double sa = sideways ? double(tex.h) / tex.w : double(tex.w) / tex.h, ra = pw / ph;
        // uv: day anh (v=1) o day quad, dinh anh (v=0) o dinh quad.
        double u0 = 0, u1 = 1, vTop = 0, vBot = 1;
        if (l.fit == LayerFit::Fit) {
            if (sa > ra) { const double nh = pw / sa; py += (ph - nh) / 2; ph = nh; }
            else         { const double nw = ph * sa; px += (pw - nw) / 2; pw = nw; }
        } else if (l.fit == LayerFit::Fill) {
            // phan cat tinh trong khong gian DA XOAY (fu, fv); doi ve khong gian texture goc truoc khi xoay
            double fu = 1, fv = 1;
            if (sa > ra) fu = ra / sa; else fv = sa / ra;
            const double cu = sideways ? fv : fu, cv = sideways ? fu : fv;
            u0 = (1 - cu) / 2; u1 = u0 + cu;
            vTop = (1 - cv) / 2; vBot = vTop + cv;
        }
        const QVector4D clip(float((px + pw / 2) / W * 2 - 1), float(1 - (py + ph / 2) / H * 2),
                             float(pw / W), float(ph / H));
        gl_.glBindTexture(GL_TEXTURE_2D, tex.id);
        blit_->setUniformValue("uOpacity", l.opacity);
        drawQuad(*blit_, clip, QVector4D(float(u0), float(vBot), float(u1), float(vTop)), float(tex.rotation));
    }
    gl_.glDisable(GL_BLEND);
}

void Compositor::renderTransition(const RenderTarget& a, const RenderTarget& b, TransitionType type, float t,
                                  RenderTarget& dst) {
    gl_.glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
    gl_.glViewport(0, 0, dst.w, dst.h);
    gl_.glDisable(GL_BLEND);
    trans_->bind();
    gl_.glActiveTexture(GL_TEXTURE0); gl_.glBindTexture(GL_TEXTURE_2D, a.tex);
    gl_.glActiveTexture(GL_TEXTURE1); gl_.glBindTexture(GL_TEXTURE_2D, b.tex);
    trans_->setUniformValue("uA", 0);
    trans_->setUniformValue("uB", 1);
    trans_->setUniformValue("uT", t);
    int mode = 1;
    switch (type) {
    case TransitionType::Fade: case TransitionType::Cut: mode = 1; break;
    case TransitionType::DipToBlack: mode = 2; break;
    case TransitionType::WipeLeft: mode = 3; break;
    case TransitionType::WipeRight: mode = 4; break;
    case TransitionType::WipeUp: mode = 5; break;
    case TransitionType::WipeDown: mode = 6; break;
    }
    trans_->setUniformValue("uMode", mode);
    drawQuad(*trans_, QVector4D(0, 0, 1, 1), QVector4D(0, 0, 1, 1), 0);
    gl_.glActiveTexture(GL_TEXTURE0);
}

QImage Compositor::thumbnail(TargetPool& pool, const RenderTarget& src, int maxW) {
    const int tw = std::max(1, std::min(maxW, src.w));
    const int th = std::max(1, int(std::lround(double(tw) * src.h / src.w)));
    RenderTarget* t = pool.acquire(tw, th);
    gl_.glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    OutputConfig cfg;
    cfg.scale = ScaleMode::Stretch;
    drawToCurrent(src.tex, src.w, src.h, cfg, tw, th);
    QImage img(tw, th, QImage::Format_RGBA8888);
    gl_.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl_.glReadPixels(0, 0, tw, th, GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
    return img.mirrored(false, true);                 // GL goc duoi-trai -> anh goc tren-trai
}

void Compositor::fadeToBlack(RenderTarget& dst, float amount) {
    gl_.glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
    gl_.glViewport(0, 0, dst.w, dst.h);
    gl_.glEnable(GL_BLEND);
    gl_.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    solid_->bind();
    solid_->setUniformValue("uColor", QVector4D(0, 0, 0, std::clamp(amount, 0.0f, 1.0f)));
    drawQuad(*solid_, QVector4D(0, 0, 1, 1), QVector4D(0, 0, 1, 1), 0);
    gl_.glDisable(GL_BLEND);
}

void Compositor::drawToCurrent(GLuint tex, int texW, int texH, const OutputConfig& cfg, int winW, int winH) {
    gl_.glViewport(0, 0, winW, winH);
    gl_.glClearColor(0, 0, 0, 1);
    gl_.glClear(GL_COLOR_BUFFER_BIT);
    gl_.glDisable(GL_BLEND);

    const QRectF crop = cfg.crop;
    const double cw = crop.width() * texW, ch = crop.height() * texH;
    if (cw <= 0 || ch <= 0) return;
    const bool sideways = (cfg.rotation / 90) % 2 != 0;
    const double rw = sideways ? ch : cw, rh = sideways ? cw : ch;     // kich thuoc sau khi xoay

    double sx = 1, sy = 1;
    switch (cfg.scale) {
    case ScaleMode::Fit:     sx = sy = std::min(winW / rw, winH / rh); break;
    case ScaleMode::Fill:    sx = sy = std::max(winW / rw, winH / rh); break;
    case ScaleMode::Stretch: sx = winW / rw; sy = winH / rh; break;
    case ScaleMode::Pixel:   sx = sy = 1; break;
    }
    const QVector4D clip(0, 0, float(rw * sx / winW), float(rh * sy / winH));
    // crop toa do tren-trai -> uv goc duoi-trai
    const QVector4D uv(float(crop.left()), float(1 - crop.bottom()), float(crop.right()), float(1 - crop.top()));

    const Calibration& k = cfg.cal;
    out_->bind();
    out_->setUniformValue("uTex", 0);
    out_->setUniformValue("uBrightness", k.brightness);
    out_->setUniformValue("uContrast", k.contrast);
    out_->setUniformValue("uGamma", std::max(0.1f, k.gamma));
    out_->setUniformValue("uGain", QVector3D(k.gainR, k.gainG, k.gainB));
    gl_.glActiveTexture(GL_TEXTURE0);
    gl_.glBindTexture(GL_TEXTURE_2D, tex);
    drawQuad(*out_, clip, uv, float(cfg.rotation));
}

} // namespace lc
