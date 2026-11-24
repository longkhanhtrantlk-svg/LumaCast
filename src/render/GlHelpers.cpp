#include "GlHelpers.h"
#include <QDebug>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QVector2D>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>

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

// ---------------- Do thoi gian + tuy chon upload ----------------

int64_t& GlProfile::uploadNs() {
    thread_local int64_t v = 0;
    return v;
}
int64_t GlProfile::nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static std::atomic<bool> gSync{false};
void GlProfile::setSync(bool on) { gSync = on; }
bool GlProfile::sync() { return gSync; }

static std::atomic<int> gUsePbo{-1};      // -1: chua doc bien moi truong
bool UploadConfig::usePbo() {
    int v = gUsePbo;
    if (v < 0) {
        v = qEnvironmentVariableIntValue("LUMACAST_NO_PBO") ? 0 : 1;
        gUsePbo = v;
    }
    return v != 0;
}
void UploadConfig::setUsePbo(bool on) { gUsePbo = on ? 1 : 0; }

namespace {
struct UploadTimer {
    GL& gl;
    int64_t t0 = GlProfile::nowNs();
    explicit UploadTimer(GL& g) : gl(g) {}
    ~UploadTimer() {
        if (GlProfile::sync()) gl.glFinish();
        GlProfile::uploadNs() += GlProfile::nowNs() - t0;
    }
};
} // namespace

// ---------------- PBO ----------------

PboRing::~PboRing() {
    if (QOpenGLContext* c = QOpenGLContext::currentContext())
        if (pbo_[0] || pbo_[1]) c->functions()->glDeleteBuffers(2, pbo_);
}

void* PboRing::begin(GL& gl, size_t bytes) {
    if (!pbo_[0]) gl.glGenBuffers(2, pbo_);
    cur_ ^= 1;
    gl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_[cur_]);
    // orphan: luon cap storage moi de DMA cua khung truoc (dang doc buffer cu) khong bi chan va khong bi ghi de
    gl.glBufferData(GL_PIXEL_UNPACK_BUFFER, GLsizeiptr(bytes), nullptr, GL_STREAM_DRAW);
    cap_[cur_] = bytes;
    void* p = gl.glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, GLsizeiptr(bytes), GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    if (!p) gl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    return p;
}

void PboRing::end(GL& gl) { gl.glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); }

void PboRing::release(GL& gl) { gl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0); }

// Dua `bytes` byte (hang day dac) vao texture dang bind: qua PBO neu bat va map duoc, nguoc lai tu bo nho CPU.
// `fresh`: glTexImage2D (cap storage moi), nguoc lai glTexSubImage2D.
static void texUpload(GL& gl, PboRing& ring, bool fresh, GLint internal, GLenum fmt, GLenum type, int w, int h, const uint8_t* data,
                      size_t bytes) {
    gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    void* dst = UploadConfig::usePbo() ? ring.begin(gl, bytes) : nullptr;
    const void* src = data;
    if (dst) {
        std::memcpy(dst, data, bytes);
        ring.end(gl);
        src = nullptr;                                  // offset 0 trong PBO
    }
    if (fresh) gl.glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, fmt, type, src);
    else gl.glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, type, src);
    if (dst) ring.release(gl);
}

AsyncReadback::~AsyncReadback() {
    if (QOpenGLContext* c = QOpenGLContext::currentContext())
        if (pbo_[0] || pbo_[1]) c->functions()->glDeleteBuffers(2, pbo_);
}

void AsyncReadback::release(GL& gl) {
    if (pbo_[0] || pbo_[1]) gl.glDeleteBuffers(2, pbo_);
    pbo_[0] = pbo_[1] = 0;
    w_[0] = w_[1] = h_[0] = h_[1] = 0;
}

QImage AsyncReadback::submit(GL& gl, int w, int h) {
    gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    if (!UploadConfig::usePbo()) {                         // dong bo: doc ngay
        QImage img(w, h, QImage::Format_RGBA8888);
        gl.glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
        return img.mirrored(false, true);
    }
    if (!pbo_[0]) gl.glGenBuffers(2, pbo_);
    cur_ ^= 1;
    const size_t bytes = size_t(w) * h * 4;
    gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[cur_]);
    gl.glBufferData(GL_PIXEL_PACK_BUFFER, GLsizeiptr(bytes), nullptr, GL_STREAM_READ);
    gl.glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);           // async: ghi vao PBO
    w_[cur_] = w; h_[cur_] = h;
    QImage out;
    const int prev = cur_ ^ 1;
    if (w_[prev] == w && h_[prev] == h) {
        gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[prev]);
        if (const void* p = gl.glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, GLsizeiptr(bytes), GL_MAP_READ_BIT)) {
            QImage img(w, h, QImage::Format_RGBA8888);
            const size_t row = size_t(w) * 4;
            for (int y = 0; y < h; ++y) std::memcpy(img.scanLine(h - 1 - y), static_cast<const uint8_t*>(p) + row * size_t(y), row);   // GL goc duoi-trai -> tren-trai
            gl.glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            out = std::move(img);
        }
    }
    gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    return out;
}

GlTexture::~GlTexture() {
    if (id_ && QOpenGLContext::currentContext())
        QOpenGLContext::currentContext()->functions()->glDeleteTextures(1, &id_);
}

void GlTexture::upload(GL& gl, const uint8_t* rgba, int w, int h) {
    UploadTimer timer(gl);
    const bool fresh = (id_ == 0);
    if (fresh) gl.glGenTextures(1, &id_);
    const bool realloc = fresh || w != w_ || h != h_;
    gl.glBindTexture(GL_TEXTURE_2D, id_);
    texUpload(gl, pbo_, realloc, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, w, h, rgba, size_t(w) * h * 4);
    if (realloc) {
        setupTex(gl);
        w_ = w; h_ = h;
    }
}

// ---------------- HAP ----------------

static std::atomic<int> gHap{0};
HapSupport::State HapSupport::state() { return State(gHap.load()); }
void HapSupport::set(bool s3tc) { gHap = s3tc ? Yes : No; }

#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

static const char* kHapVert = R"(#version 330 core
out vec2 vUv;
void main() {
    vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";
// Scaled YCoCg-DXT5 (Hap Q): (R,G,B,A) = (Co, Cg, scale, Y)
static const char* kHapFrag = R"(#version 330 core
uniform sampler2D uTex;
in vec2 vUv;
out vec4 o;
void main() {
    vec4 t = texture(uTex, vUv) - vec4(0.50196078431373, 0.50196078431373, 0.0, 0.0);
    float scale = t.z * (255.0 / 8.0) + 1.0;
    float co = t.x / scale, cg = t.y / scale, y = t.w;
    o = vec4(clamp(vec3(y + co - cg, y + cg, y - co - cg), 0.0, 1.0), 1.0);
})";
namespace {
struct HapProg {
    std::unique_ptr<QOpenGLShaderProgram> p;
    GLuint vao = 0;
    QOpenGLContext* ctx = nullptr;
};
HapProg& hapProg(GL& gl) {
    static std::mutex m;
    static auto* progs = new std::map<QOpenGLContext*, HapProg>;      // co y khong huy: luc thoat tien trinh context GL da het
    std::lock_guard lk(m);
    QOpenGLContext* cx = QOpenGLContext::currentContext();
    HapProg& hp = (*progs)[cx];
    if (!hp.p) {
        hp.p = std::make_unique<QOpenGLShaderProgram>();
        if (!hp.p->addShaderFromSourceCode(QOpenGLShader::Vertex, kHapVert) || !hp.p->addShaderFromSourceCode(QOpenGLShader::Fragment, kHapFrag) ||
            !hp.p->link()) {
            qCritical().noquote() << "Shader HAP loi:" << hp.p->log();
            hp.p.reset();
            return hp;
        }
        gl.glGenVertexArrays(1, &hp.vao);
        hp.ctx = cx;
    }
    return hp;
}
} // namespace

HapTexture::~HapTexture() {
    if (QOpenGLContext* c = QOpenGLContext::currentContext()) {
        const GLuint t[2] = {tex_, out_};
        c->functions()->glDeleteTextures(2, t);
        if (fbo_) c->functions()->glDeleteFramebuffers(1, &fbo_);
    }
}

bool HapTexture::upload(GL& gl, HapGlFormat f, int w, int h, const uint8_t* data, size_t bytes) {
    if (w <= 0 || h <= 0 || w % 4 || h % 4) return false;
    const size_t blk = f == HapGlFormat::Dxt1 ? 8 : 16;
    if (bytes != size_t(w / 4) * size_t(h / 4) * blk) return false;
    UploadTimer timer(gl);
    const bool fresh = !tex_ || w != w_ || h != h_ || f != fmt_;
    if (!tex_) gl.glGenTextures(1, &tex_);
    const GLenum internal = f == HapGlFormat::Dxt1 ? GL_COMPRESSED_RGB_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    gl.glActiveTexture(GL_TEXTURE0);
    gl.glBindTexture(GL_TEXTURE_2D, tex_);
    void* dst = UploadConfig::usePbo() ? pbo_.begin(gl, bytes) : nullptr;
    const void* src = data;
    if (dst) { std::memcpy(dst, data, bytes); pbo_.end(gl); src = nullptr; }
    if (fresh) {
        gl.glCompressedTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, GLsizei(bytes), src);
        setupTex(gl);
    } else {
        gl.glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, internal, GLsizei(bytes), src);
    }
    if (dst) pbo_.release(gl);
    w_ = w; h_ = h; fmt_ = f;
    ycocg_ = f == HapGlFormat::YCoCgDxt5;
    if (ycocg_) {
        HapProg& hp = hapProg(gl);
        if (!hp.p) return false;
        if (!out_) {
            gl.glGenTextures(1, &out_);
            gl.glGenFramebuffers(1, &fbo_);
        }
        if (fresh) {
            gl.glBindTexture(GL_TEXTURE_2D, out_);
            gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            setupTex(gl);
            gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
            gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out_, 0);
        }
        gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        gl.glViewport(0, 0, w, h);
        gl.glDisable(GL_BLEND);
        hp.p->bind();
        hp.p->setUniformValue("uTex", 0);
        gl.glBindTexture(GL_TEXTURE_2D, tex_);
        gl.glBindVertexArray(hp.vao);
        gl.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    return true;
}

// ---------------- YUV ----------------

size_t YuvImage::planeBytes(int i) const {
    const size_t bps = size_t(fmt.bytesPerSample());
    if (i == 0) return size_t(w) * h * bps;
    if (fmt.layout == YuvLayout::SemiPlanar) return i == 1 ? size_t(chromaW()) * chromaH() * 2 * bps : 0;
    return i <= 2 ? size_t(chromaW()) * chromaH() * bps : 0;
}

bool YuvImage::valid() const {
    if (w <= 0 || h <= 0) return false;
    const int n = fmt.layout == YuvLayout::SemiPlanar ? 2 : 3;
    for (int i = 0; i < n; ++i)
        if (plane[i].size() != planeBytes(i)) return false;
    return true;
}

static const char* kYuvVert = R"(#version 330 core
out vec2 vUv;
void main() {
    vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vUv = p;                                   // hang 0 cua FBO (y=-1) <- hang 0 cua nguon: giu nguyen "hang dau = tren cung"
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

static const char* kYuvFrag = R"(#version 330 core
uniform sampler2D uY;
uniform sampler2D uU;       // Planar: U | SemiPlanar: UV xen ke (rg)
uniform sampler2D uV;
uniform int uSemi;
uniform float uScale;       // chuan hoa gia tri chua (0..1 theo kieu du lieu) -> 0..1 theo do sau mau that
uniform vec2 uYR;           // (bu, he so) cua Y
uniform vec2 uCR;           // (bu, he so) cua Cb/Cr
uniform vec2 uK;            // (Kr, Kb)
uniform vec2 uCShift;       // dich toa do lay mau Cb/Cr (don vi uv): +0,25 diem sac cho chroma "lech trai"
in vec2 vUv;
out vec4 o;
void main() {
    float y = texture(uY, vUv).r * uScale;
    vec2 cuv = vUv + uCShift;
    vec2 c = uSemi == 1 ? texture(uU, cuv).rg : vec2(texture(uU, cuv).r, texture(uV, cuv).r);
    c *= uScale;
    y = (y - uYR.x) * uYR.y;
    c = (c - uCR.x) * uCR.y;
    float kr = uK.x, kb = uK.y, kg = 1.0 - kr - kb;
    float r = y + 2.0 * (1.0 - kr) * c.y;
    float b = y + 2.0 * (1.0 - kb) * c.x;
    float g = y - 2.0 * (1.0 - kb) * kb / kg * c.x - 2.0 * (1.0 - kr) * kr / kg * c.y;
    o = vec4(clamp(vec3(r, g, b), 0.0, 1.0), 1.0);
})";

struct YuvTexture::Prog {
    std::unique_ptr<QOpenGLShaderProgram> p;
    GLuint vao = 0;
    QOpenGLContext* ctx = nullptr;
    ~Prog() {
        QOpenGLContext* cur = QOpenGLContext::currentContext();
        if (vao && cur == ctx) cur->extraFunctions()->glDeleteVertexArrays(1, &vao);
    }
};

static std::mutex gProgMu;
static std::map<QOpenGLContext*, std::weak_ptr<YuvTexture::Prog>> gProgs;

YuvTexture::YuvTexture() = default;

YuvTexture::~YuvTexture() {
    if (QOpenGLContext* c = QOpenGLContext::currentContext()) {
        const GLuint t[4] = {planeTex_[0], planeTex_[1], planeTex_[2], out_};
        c->functions()->glDeleteTextures(4, t);
        if (fbo_) c->functions()->glDeleteFramebuffers(1, &fbo_);
    }
}

static void coeffs(YuvMatrix m, double& kr, double& kb) {
    switch (m) {
    case YuvMatrix::BT601: kr = 0.299; kb = 0.114; break;
    case YuvMatrix::BT2020: kr = 0.2627; kb = 0.0593; break;
    default: kr = 0.2126; kb = 0.0722; break;
    }
}

void YuvTexture::convertPixel(const YuvFormat& f, double y, double u, double v, double rgb[3]) {
    // y,u,v: gia tri nguyen theo do sau mau that (0..2^bits-1)
    const double mx = double((1 << f.bits) - 1), sh = double(1 << (f.bits - 8));
    double kr, kb;
    coeffs(f.matrix, kr, kb);
    const double kg = 1 - kr - kb;
    const double yn = f.fullRange ? y / mx : (y - 16 * sh) / (219 * sh);
    const double cb = f.fullRange ? (u - 128 * sh) / mx : (u - 128 * sh) / (224 * sh);
    const double cr = f.fullRange ? (v - 128 * sh) / mx : (v - 128 * sh) / (224 * sh);
    const double r = yn + 2 * (1 - kr) * cr, b = yn + 2 * (1 - kb) * cb;
    const double g = yn - 2 * (1 - kb) * kb / kg * cb - 2 * (1 - kr) * kr / kg * cr;
    rgb[0] = std::clamp(r, 0.0, 1.0); rgb[1] = std::clamp(g, 0.0, 1.0); rgb[2] = std::clamp(b, 0.0, 1.0);
}

void YuvTexture::ensureTargets(GL& gl, const YuvImage& img) {
    const bool semi = img.fmt.layout == YuvLayout::SemiPlanar;
    const int n = semi ? 2 : 3;
    const bool deep = img.fmt.bits > 8;
    const bool sameSize = haveFmt_ && w_ == img.w && h_ == img.h;
    if (haveFmt_ && fmt_ == img.fmt && sameSize) return;
    const int want[3][2] = {{img.w, img.h}, {img.chromaW(), img.chromaH()}, {img.chromaW(), img.chromaH()}};
    for (int i = 0; i < 3; ++i) {
        if (i >= n) {
            if (planeTex_[i]) { gl.glDeleteTextures(1, &planeTex_[i]); planeTex_[i] = 0; }
            planeW_[i] = planeH_[i] = 0;
            continue;
        }
        if (!planeTex_[i]) gl.glGenTextures(1, &planeTex_[i]);
        planeW_[i] = want[i][0]; planeH_[i] = want[i][1];
        gl.glBindTexture(GL_TEXTURE_2D, planeTex_[i]);
        const bool two = semi && i == 1;
        const GLint internal = two ? (deep ? GL_RG16 : GL_RG8) : (deep ? GL_R16 : GL_R8);
        gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl.glTexImage2D(GL_TEXTURE_2D, 0, internal, planeW_[i], planeH_[i], 0, two ? GL_RG : GL_RED,
                        deep ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE, nullptr);
        setupTex(gl);
    }
    if (!out_) gl.glGenTextures(1, &out_);
    if (!fbo_) gl.glGenFramebuffers(1, &fbo_);
    if (!sameSize) {
        gl.glBindTexture(GL_TEXTURE_2D, out_);
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        setupTex(gl);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out_, 0);
    }
    w_ = img.w; h_ = img.h;
    fmt_ = img.fmt;
    haveFmt_ = true;
}

bool YuvTexture::upload(GL& gl, const YuvImage& img) {
    if (!img.valid()) return false;
    UploadTimer timer(gl);
    if (!prog_) {
        QOpenGLContext* cx = QOpenGLContext::currentContext();
        std::lock_guard lk(gProgMu);
        prog_ = gProgs[cx].lock();
        if (!prog_) {
            auto pr = std::make_shared<Prog>();
            pr->p = std::make_unique<QOpenGLShaderProgram>();
            if (!pr->p->addShaderFromSourceCode(QOpenGLShader::Vertex, kYuvVert) ||
                !pr->p->addShaderFromSourceCode(QOpenGLShader::Fragment, kYuvFrag) || !pr->p->link()) {
                qCritical().noquote() << "Shader YUV loi:" << pr->p->log();
                return false;
            }
            pr->ctx = cx;
            gl.glGenVertexArrays(1, &pr->vao);
            gProgs[cx] = pr;
            prog_ = pr;
        }
    }
    ensureTargets(gl, img);
    const bool semi = img.fmt.layout == YuvLayout::SemiPlanar;
    const bool deep = img.fmt.bits > 8;
    const int n = semi ? 2 : 3;
    for (int i = 0; i < n; ++i) {
        const bool two = semi && i == 1;
        gl.glActiveTexture(GL_TEXTURE0 + i);
        gl.glBindTexture(GL_TEXTURE_2D, planeTex_[i]);
        texUpload(gl, pbo_, false, 0, two ? GL_RG : GL_RED, deep ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE, planeW_[i], planeH_[i],
                  img.plane[i].data(), img.plane[i].size());
    }
    // bu + he so khoang gia tri (cung cong thuc voi convertPixel)
    const double mx = double((1 << img.fmt.bits) - 1), sh = double(1 << (img.fmt.bits - 8));
    const double scale = deep ? 65535.0 / (img.fmt.msb ? mx * double(1 << (16 - img.fmt.bits)) : mx) : 1.0;
    double kr, kb;
    coeffs(img.fmt.matrix, kr, kb);
    QOpenGLShaderProgram& p = *prog_->p;
    gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    gl.glViewport(0, 0, w_, h_);
    gl.glDisable(GL_BLEND);
    p.bind();
    p.setUniformValue("uY", 0);
    p.setUniformValue("uU", 1);
    p.setUniformValue("uV", semi ? 1 : 2);
    p.setUniformValue("uSemi", semi ? 1 : 0);
    p.setUniformValue("uScale", float(scale));
    if (img.fmt.fullRange) {
        p.setUniformValue("uYR", QVector2D(0.0f, 1.0f));
        p.setUniformValue("uCR", QVector2D(float(128 * sh / mx), 1.0f));
    } else {
        p.setUniformValue("uYR", QVector2D(float(16 * sh / mx), float(mx / (219 * sh))));
        p.setUniformValue("uCR", QVector2D(float(128 * sh / mx), float(mx / (224 * sh))));
    }
    p.setUniformValue("uK", QVector2D(float(kr), float(kb)));
    // Chroma "lech trai" (MPEG-2/H.264): mau sac i nam tai tam diem anh 2i -> doc o toa do + 1/4 diem sac. Chi theo chieu ngang (doc: nam giua, nhu mac dinh).
    const float shiftX = (img.fmt.chromaShiftX > 0 && !img.fmt.chromaCenter) ? 0.25f / float(planeW_[1]) : 0.0f;
    p.setUniformValue("uCShift", QVector2D(shiftX, 0.0f));
    gl.glBindVertexArray(prog_->vao);
    gl.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl.glActiveTexture(GL_TEXTURE0);
    return true;
}

} // namespace lc
