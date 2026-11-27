#include "Compositor.h"
#include <QMatrix2x2>
#include <QDebug>
#include <QVector2D>
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
uniform vec2 uRotSC;        // (sin, cos - 1) cua goc xoay HINH HOC cua quad; 0 = khong xoay
uniform float uAspectM1;    // (rong/cao cua dich) - 1, de xoay dung trong khong gian diem anh
out vec2 vUv;
void main() {
    vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vec2 o = (p * 2.0 - 1.0) * uRect.zw;
    float s = uRotSC.x, c = 1.0 + uRotSC.y, A = 1.0 + uAspectM1;
    o = vec2(c * o.x - s * o.y / A, s * o.x * A + c * o.y);
    gl_Position = vec4(uRect.xy + o, 0.0, 1.0);
    vec2 q = uUvRot * (p - 0.5) + 0.5;
    vUv = mix(uUvRect.xy, uUvRect.zw, q);
})";

static const char* kFragBlit = R"(#version 330 core
uniform sampler2D uTex;
uniform float uOpacity;
uniform int uBlendMode;      // 0 thuong | 1 add/screen (nhan alpha truoc) | 2 multiply | 3 nguon nhan san alpha
in vec2 vUv;
out vec4 o;
void main() {
    vec4 c = texture(uTex, vUv);
    float a = c.a * uOpacity;
    if (uBlendMode == 1) o = vec4(c.rgb * a, a);
    else if (uBlendMode == 2) o = vec4(mix(vec3(1.0), c.rgb, a), a);
    else if (uBlendMode == 3) o = c * uOpacity;      // texture da nhan san alpha (lop phu)
    else o = vec4(c.rgb, a);
})";

// Shader cuoi cung cua Output: hieu chinh tuong phan/do sang/gain/gamma.
static const char* kFragOut = R"(#version 330 core
uniform sampler2D uTex;
uniform float uBrightness;
uniform float uContrast;
uniform float uGamma;
uniform vec3 uGain;
uniform int uIdentity;      // 1: khong hieu chinh (mac dinh) -> chi chep, bo qua contrast/gain/pow (hot spot khi nhieu Output 4K)
in vec2 vUv;
out vec4 o;
void main() {
    vec3 c = texture(uTex, vUv).rgb;
    if (uIdentity == 1) { o = vec4(c, 1.0); return; }
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
    if (QOpenGLContext* c = QOpenGLContext::currentContext())
        HapSupport::set(c->hasExtension("GL_EXT_texture_compression_s3tc") || c->hasExtension("GL_EXT_texture_compression_dxt1"));
    gl_.glGenVertexArrays(1, &vao_);
    blit_ = makeProgram(kFragBlit);
    out_ = makeProgram(kFragOut);
    trans_ = makeProgram(kFragTrans);
    solid_ = makeProgram(kFragSolid);
    return blit_ && out_ && trans_ && solid_;
}

void Compositor::shutdown() {
    for (auto& r : readers_) r.second.release(gl_);
    readers_.clear();
    overlays_.release();
    fxCache_.clear();
    blit_.reset();
    out_.reset();
    trans_.reset();
    solid_.reset();
    if (vao_) gl_.glDeleteVertexArrays(1, &vao_);
    vao_ = 0;
}

void Compositor::drawQuad(QOpenGLShaderProgram& prog, QVector4D clipRect, QVector4D uvRect, float rotDeg, float quadRotDeg, float aspect) {
    const float a = qDegreesToRadians(rotDeg), c = std::cos(a), s = std::sin(a);
    const float m[4] = {c, -s, s, c};   // hang truoc; quay nguoc chieu kim dong ho goc `a` tren toa do uv
    prog.setUniformValue("uRect", clipRect);
    prog.setUniformValue("uUvRect", uvRect);
    prog.setUniformValue("uUvRot", QMatrix2x2(m));
    // xoay hinh hoc cua quad (Layer.rotation): duong = theo chieu kim dong ho tren man hinh (y len) -> goc am
    const float qa = qDegreesToRadians(-quadRotDeg);
    prog.setUniformValue("uRotSC", QVector2D(std::sin(qa), std::cos(qa) - 1.0f));
    prog.setUniformValue("uAspectM1", aspect - 1.0f);
    gl_.glBindVertexArray(vao_);
    gl_.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Compositor::clear(RenderTarget& dst, const QColor& c) {
    gl_.glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
    gl_.glViewport(0, 0, dst.w, dst.h);
    gl_.glClearColor(c.redF(), c.greenF(), c.blueF(), c.alphaF());
    gl_.glClear(GL_COLOR_BUFFER_BIT);
}

QRectF cropToTexture(const QRectF& d, int rotation) {
    switch (((rotation % 360) + 360) % 360) {
    case 90: return QRectF(d.y(), 1 - d.x() - d.width(), d.height(), d.width());
    case 180: return QRectF(1 - d.x() - d.width(), 1 - d.y() - d.height(), d.width(), d.height());
    case 270: return QRectF(1 - d.y() - d.height(), d.x(), d.height(), d.width());
    default: return d;
    }
}

void Compositor::renderScene(const Scene& scene, RenderTarget& dst) {
    const int64_t t0 = GlProfile::nowNs(), up0 = GlProfile::uploadNs();
    struct Done {
        Compositor& c; int64_t t0, up0;
        ~Done() {
            if (GlProfile::sync()) c.gl_.glFinish();
            c.timing_.sceneNs += (GlProfile::nowNs() - t0) - (GlProfile::uploadNs() - up0);     // upload do rieng
        }
    } done{*this, t0, up0};
    clear(dst, scene.background);

    const double W = dst.w, H = dst.h;
    const double sceneTime = scene.clock.time;
    for (const Layer& l : scene.layers) {
        if (!l.visible || !l.source) continue;
        const LayerState st = evalLayer(l, sceneTime);               // gia tri tinh + keyframe tai thoi diem Scene
        if (st.opacity <= 0.0f || st.rect.width() <= 0 || st.rect.height() <= 0) continue;
        TexRef tex = l.source->acquire(gl_);
        if (!tex.id || tex.w <= 0 || tex.h <= 0) continue;

        // Effect cua Layer chay TRUOC (tren texture nguon), ket qua nam trong FBO (goc duoi-trai)
        bool bottomUp = false;
        if (!l.effects.empty() && registry_ && pool_) {
            if (RenderTarget* fx = applyLayerEffects(l, tex)) {
                tex = TexRef{fx->tex, fx->w, fx->h, tex.rotation};
                bottomUp = true;
            }
        }
        // (dat lai trang thai ve vao Scene vi Effect da doi FBO/program/blend)
        gl_.glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
        gl_.glViewport(0, 0, dst.w, dst.h);
        gl_.glEnable(GL_BLEND);
        switch (l.blend) {
        case BlendMode::Normal: gl_.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
        case BlendMode::Add: gl_.glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE); break;
        case BlendMode::Multiply: gl_.glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE); break;
        case BlendMode::Screen: gl_.glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE); break;
        }
        blit_->bind();
        blit_->setUniformValue("uTex", 0);
        blit_->setUniformValue("uBlendMode", l.blend == BlendMode::Normal ? 0 : l.blend == BlendMode::Multiply ? 2 : 1);
        gl_.glActiveTexture(GL_TEXTURE0);

        double px = st.rect.x() * W, py = st.rect.y() * H, pw = st.rect.width() * W, ph = st.rect.height() * H;
        // Vung nguon duoc dung (crop) tinh theo HINH DA XOAY; video dien thoai luu ngang nhung hien thi xoay 90/270.
        const bool sideways = (tex.rotation / 90) % 2 != 0;
        QRectF cr = l.crop.intersected(QRectF(0, 0, 1, 1));
        if (cr.width() < 1e-3 || cr.height() < 1e-3) cr = QRectF(0, 0, 1, 1);
        const double dw = (sideways ? tex.h : tex.w) * cr.width(), dh = (sideways ? tex.w : tex.h) * cr.height();
        const double sa = dw / dh, ra = pw / ph;
        QRectF d = cr;
        if (l.fit == LayerFit::Fit) {
            if (sa > ra) { const double nh = pw / sa; py += (ph - nh) / 2; ph = nh; }
            else         { const double nw = ph * sa; px += (pw - nw) / 2; pw = nw; }
        } else if (l.fit == LayerFit::Fill) {
            // cat them o giua phan da crop cho vua khung
            if (sa > ra) { const double f = ra / sa; d = QRectF(cr.x() + cr.width() * (1 - f) / 2, cr.y(), cr.width() * f, cr.height()); }
            else         { const double f = sa / ra; d = QRectF(cr.x(), cr.y() + cr.height() * (1 - f) / 2, cr.width(), cr.height() * f); }
        }
        const QRectF t = cropToTexture(d, tex.rotation);                    // vung tren texture goc (v tinh tu tren xuong)
        double vTop = t.top(), vBot = t.bottom();
        if (bottomUp) { vTop = 1 - vTop; vBot = 1 - vBot; }                  // FBO goc duoi-trai: khong lat doc
        const QVector4D clip(float((px + pw / 2) / W * 2 - 1), float(1 - (py + ph / 2) / H * 2),
                             float(pw / W), float(ph / H));
        gl_.glBindTexture(GL_TEXTURE_2D, tex.id);
        blit_->setUniformValue("uOpacity", st.opacity);
        drawQuad(*blit_, clip, QVector4D(float(t.left()), float(vBot), float(t.right()), float(vTop)), float(tex.rotation),
                 st.rotation, float(W / H));
    }
    gl_.glDisable(GL_BLEND);
}

namespace {
struct TransTimer {
    int& depth; int64_t& acc; GL& gl; int64_t t0 = GlProfile::nowNs();
    TransTimer(int& d, int64_t& a, GL& g) : depth(d), acc(a), gl(g) { ++depth; }
    ~TransTimer() {
        if (--depth == 0) {                 // chi do lop ngoai cung (overload day du goi overload theo loai)
            if (GlProfile::sync()) gl.glFinish();
            acc += GlProfile::nowNs() - t0;
        }
    }
};
} // namespace

void Compositor::renderTransition(const RenderTarget& a, const RenderTarget& b, TransitionType type, float t,
                                  RenderTarget& dst) {
    TransTimer tt(transDepth_, timing_.transitionNs, gl_);
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

// ---------------- Effect / Transition shader ----------------

QOpenGLShaderProgram* Compositor::effectProgram(const EffectDef& d) {
    const QString key = QString("%1#%2").arg(d.id).arg(d.revision);
    auto it = fxCache_.find(key);
    if (it != fxCache_.end()) return it->second.get();
    for (auto i = fxCache_.begin(); i != fxCache_.end();)                 // bo ban bien dich cu cua cung id (revision khac)
        i = i->first.startsWith(d.id + "#") ? fxCache_.erase(i) : std::next(i);

    auto prog = std::make_unique<QOpenGLShaderProgram>();
    const bool ok = prog->addShaderFromSourceCode(QOpenGLShader::Vertex, kVert) &&
                    prog->addShaderFromSourceCode(QOpenGLShader::Fragment, buildFragmentSource(d).toUtf8()) && prog->link();
    if (!ok) {
        const QString log = prog->log().isEmpty() ? QString("bien dich that bai") : prog->log();
        if (registry_) registry_->setCompileError(d.id, d.revision, log);
        qWarning().noquote() << QString("Shader '%1' loi, tat Effect nay:").arg(d.id) << log;
        fxCache_[key] = nullptr;
        return nullptr;
    }
    if (registry_) registry_->setCompileError(d.id, d.revision, QString());
    return (fxCache_[key] = std::move(prog)).get();
}

void Compositor::setParamUniforms(QOpenGLShaderProgram& prog, const EffectDef& d, const QVariantMap& values) {
    for (const ParamDef& p : d.params) {
        const QVariant v = sanitizeParam(p, values.contains(p.name) ? values.value(p.name) : p.def);
        const QByteArray n = p.name.toUtf8();
        switch (p.type) {
        case ParamDef::Float: prog.setUniformValue(n.constData(), float(v.toDouble())); break;
        case ParamDef::Int: case ParamDef::Choice: prog.setUniformValue(n.constData(), GLint(v.toInt())); break;
        case ParamDef::Bool: prog.setUniformValue(n.constData(), GLint(v.toBool() ? 1 : 0)); break;
        case ParamDef::Color: { const QColor c = v.value<QColor>(); prog.setUniformValue(n.constData(), QVector4D(c.redF(), c.greenF(), c.blueF(), c.alphaF())); break; }
        case ParamDef::Point: { const QPointF q = v.toPointF(); prog.setUniformValue(n.constData(), QVector2D(float(q.x()), float(q.y()))); break; }
        }
    }
}

RenderTarget* Compositor::applyLayerEffects(const Layer& l, const TexRef& tex) {
    GLuint in = tex.id;
    bool topDown = true;                                   // texture nguon luu tu tren xuong; FBO thi goc duoi-trai
    RenderTarget* last = nullptr;
    for (const EffectInstance& inst : l.effects) {
        if (!inst.enabled) continue;
        auto def = registry_->find(inst.effectId);
        if (!def || def->kind != EffectDef::Effect) continue;          // plug-in thieu/da tat: bo qua, giu du lieu
        QOpenGLShaderProgram* prog = effectProgram(*def);
        if (!prog) continue;                                            // shader loi: bo qua Effect nay
        RenderTarget* out = pool_->acquire(tex.w, tex.h);               // khac `in` vi `in` (neu la FBO truoc) dang busy
        gl_.glBindFramebuffer(GL_FRAMEBUFFER, out->fbo);
        gl_.glViewport(0, 0, out->w, out->h);
        gl_.glDisable(GL_BLEND);
        prog->bind();
        gl_.glActiveTexture(GL_TEXTURE0);
        gl_.glBindTexture(GL_TEXTURE_2D, in);
        prog->setUniformValue("uInput", 0);
        prog->setUniformValue("uSize", QVector2D(float(out->w), float(out->h)));
        prog->setUniformValue("uTime", float(time_));
        setParamUniforms(*prog, *def, inst.params);
        drawQuad(*prog, QVector4D(0, 0, 1, 1), topDown ? QVector4D(0, 1, 1, 0) : QVector4D(0, 0, 1, 1), 0);
        in = out->tex;
        topDown = false;
        last = out;
    }
    return last;
}

void Compositor::renderTransition(const RenderTarget& a, const RenderTarget& b, const TransitionSpec& spec, float t,
                                  RenderTarget& dst) {
    TransTimer tt(transDepth_, timing_.transitionNs, gl_);
    if (spec.type == TransitionType::Plugin && registry_) {
        auto def = registry_->find(spec.pluginId);
        QOpenGLShaderProgram* prog = def && def->kind == EffectDef::Transition ? effectProgram(*def) : nullptr;
        if (prog) {
            gl_.glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
            gl_.glViewport(0, 0, dst.w, dst.h);
            gl_.glDisable(GL_BLEND);
            prog->bind();
            gl_.glActiveTexture(GL_TEXTURE0); gl_.glBindTexture(GL_TEXTURE_2D, a.tex);
            gl_.glActiveTexture(GL_TEXTURE1); gl_.glBindTexture(GL_TEXTURE_2D, b.tex);
            prog->setUniformValue("uFrom", 0);
            prog->setUniformValue("uTo", 1);
            prog->setUniformValue("uProgress", t);
            prog->setUniformValue("uSize", QVector2D(float(dst.w), float(dst.h)));
            prog->setUniformValue("uTime", float(time_));
            setParamUniforms(*prog, *def, spec.params);
            drawQuad(*prog, QVector4D(0, 0, 1, 1), QVector4D(0, 0, 1, 1), 0);
            gl_.glActiveTexture(GL_TEXTURE0);
            return;
        }
    }
    const bool special = spec.type == TransitionType::Plugin || spec.type == TransitionType::Stinger;
    renderTransition(a, b, special ? TransitionType::Fade : spec.type, t, dst);
}

void Compositor::drawOverlay(Source& src, RenderTarget& dst) {
    const TexRef tex = src.acquire(gl_);
    if (!tex.id) return;
    gl_.glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
    gl_.glViewport(0, 0, dst.w, dst.h);
    gl_.glEnable(GL_BLEND);
    gl_.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    blit_->bind();
    blit_->setUniformValue("uTex", 0);
    blit_->setUniformValue("uOpacity", 1.0f);
    blit_->setUniformValue("uBlendMode", 0);
    gl_.glActiveTexture(GL_TEXTURE0);
    gl_.glBindTexture(GL_TEXTURE_2D, tex.id);
    drawQuad(*blit_, QVector4D(0, 0, 1, 1), QVector4D(0, 1, 1, 0), float(tex.rotation));
    gl_.glDisable(GL_BLEND);
}

void Compositor::renderOverlays(const OverlayStack& stack, QSize canvas, double now, RenderTarget& dst) {
    const std::vector<OverlayFrame> frames = stack.frames(now);
    if (frames.empty()) return;
    overlays_.beginFrame(gl_);
    const QSize target(dst.w, dst.h);
    const double W = dst.w, H = dst.h;
    const double k = canvas.height() > 0 ? H / canvas.height() : 1.0;       // px canvas -> px dich
    for (const OverlayFrame& f : frames) {
        const OverlayRasterKey key = overlayRasterKey(f.def, f.secondsLeft, canvas, target);
        const OverlayCache::Entry* e = overlays_.request(&stack, f.def.id, target, key);      // ke ca dang tat: dung san
        const float alpha = f.def.opacity * f.level;
        if (alpha <= 0.0f) continue;

        // hop cua lop phu, lam tron ve diem anh de chu dung yen khong bi nhoe
        const double bx = std::round(f.def.rect.x() * W), by = std::round(f.def.rect.y() * H);
        const double bw = std::max(1.0, std::round(f.def.rect.width() * W)), bh = key.box.height();
        gl_.glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
        gl_.glViewport(0, 0, dst.w, dst.h);
        gl_.glEnable(GL_BLEND);
        gl_.glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        gl_.glEnable(GL_SCISSOR_TEST);                                       // chu chay khong tran ra ngoai hop
        gl_.glScissor(int(bx), int(H - by - bh), int(bw), int(bh));
        auto clipRect = [&](double x, double y, double w, double h) {
            return QVector4D(float((x + w / 2) / W * 2 - 1), float(1 - (y + h / 2) / H * 2), float(w / W), float(h / H));
        };
        if (f.def.background.alpha() > 0) {
            const QColor& bg = f.def.background;
            const float a = float(bg.alphaF()) * alpha;
            solid_->bind();
            solid_->setUniformValue("uColor", QVector4D(float(bg.redF()) * a, float(bg.greenF()) * a, float(bg.blueF()) * a, a));
            drawQuad(*solid_, clipRect(bx, by, bw, bh), QVector4D(0, 0, 1, 1), 0);
        }
        if (!e || e->size.isEmpty()) continue;
        const double ew = e->size.width(), eh = e->size.height();
        double sx = 1, sy = 1;                                               // = 1 khi anh dung dung kich thuoc dich
        if (f.def.type == OverlayType::Ticker) sx = sy = bh / eh;
        else if (f.def.type == OverlayType::Logo) sx = sy = std::min(bw / ew, bh / eh);
        else { sx = bw / ew; sy = bh / eh; }
        const double cw = ew * sx, ch = eh * sy;
        double x = bx + overlayContentX(f.def.type, f.def.align, bw, cw, f.onSeconds, f.def.speed * k);
        const double y = by + std::round((bh - ch) / 2);
        if (f.def.type == OverlayType::Logo) x = std::round(x);
        blit_->bind();
        blit_->setUniformValue("uTex", 0);
        blit_->setUniformValue("uBlendMode", 3);
        blit_->setUniformValue("uOpacity", alpha);
        gl_.glActiveTexture(GL_TEXTURE0);
        for (const auto& tile : e->tiles) {
            const double tw = tile->w() * sx;
            if (x < bx + bw && x + tw > bx) {
                gl_.glBindTexture(GL_TEXTURE_2D, tile->id());
                drawQuad(*blit_, clipRect(x, y, tw, ch), QVector4D(0, 1, 1, 0), 0);
            }
            x += tw;
        }
    }
    gl_.glDisable(GL_SCISSOR_TEST);
    gl_.glDisable(GL_BLEND);
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

QImage Compositor::thumbnailAsync(TargetPool& pool, const RenderTarget& src, int maxW, int slot) {
    const int tw = std::max(1, std::min(maxW, src.w));
    const int th = std::max(1, int(std::lround(double(tw) * src.h / src.w)));
    RenderTarget* t = pool.acquire(tw, th);
    gl_.glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    OutputConfig cfg;
    cfg.scale = ScaleMode::Stretch;
    drawToCurrent(src.tex, src.w, src.h, cfg, tw, th);
    return readers_[slot].submit(gl_, tw, th);
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

void Compositor::drawToCurrent(GLuint tex, int texW, int texH, const OutputConfig& cfg, int winW, int winH, const QRect& area) {
    gl_.glViewport(0, 0, winW, winH);
    gl_.glClearColor(0, 0, 0, 1);
    gl_.glClear(GL_COLOR_BUFFER_BIT);                  // ca khung: ngoai vung LED luon den
    gl_.glDisable(GL_BLEND);
    if (!area.isEmpty()) {                             // area goc tren-trai -> viewport goc duoi-trai
        gl_.glViewport(area.x(), winH - area.y() - area.height(), area.width(), area.height());
        winW = area.width();
        winH = area.height();
    }

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
    const bool identity = k.brightness == 1.0f && k.contrast == 1.0f && k.gamma == 1.0f && k.gainR == 1.0f && k.gainG == 1.0f && k.gainB == 1.0f;
    out_->setUniformValue("uIdentity", identity ? 1 : 0);
    gl_.glActiveTexture(GL_TEXTURE0);
    gl_.glBindTexture(GL_TEXTURE_2D, tex);
    drawQuad(*out_, clip, uv, float(cfg.rotation));
}

} // namespace lc
