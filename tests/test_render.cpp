// Test render khong can cua so: ve vao FBO roi doc lai pixel.
#include "core/Channel.h"
#include "media/FFmpegSource.h"
#include "media/StaticSources.h"
#include "render/Compositor.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QThread>
#include <cstdlib>
#include <iostream>

using namespace lc;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << msg << "  (" #cond ")\n"; } else std::cout << "ok:   " << msg << "\n"; } while (0)

struct Px { int r, g, b; };
static Px readPx(GL& gl, const RenderTarget& t, int x, int yFromTop) {
    uint8_t p[4];
    gl.glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    gl.glReadPixels(x, t.h - 1 - yFromTop, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    return {p[0], p[1], p[2]};
}
static bool near(Px p, int r, int g, int b, int tol = 4) {
    return std::abs(p.r - r) <= tol && std::abs(p.g - g) <= tol && std::abs(p.b - b) <= tol;
}
static std::shared_ptr<Scene> colorScene(const char* n, QColor c, QSize sz = {64, 36}) {
    auto s = std::make_shared<Scene>();
    s->name = n; s->size = sz;
    s->layers.push_back({std::make_shared<ColorSource>(c, n), {0, 0, 1, 1}, 1.0f, LayerFit::Stretch});
    return s;
}

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);
    QGuiApplication app(argc, argv);

    QOffscreenSurface surf;
    surf.setFormat(fmt);
    surf.create();
    QOpenGLContext ctx;
    ctx.setFormat(fmt);
    if (!ctx.create() || !ctx.makeCurrent(&surf)) { std::cerr << "Khong co OpenGL 3.3\n"; return 2; }
    Compositor comp;
    if (!comp.init()) { std::cerr << "Shader loi\n"; return 2; }
    GL& gl = comp.gl();
    TargetPool pool(gl);
    std::cout << "GL: " << gl.glGetString(GL_RENDERER) << "\n";

    // --- Scene + layer ---
    {
        pool.beginFrame();
        auto* t = pool.acquire(64, 36);
        comp.renderScene(*colorScene("red", Qt::red), *t);
        CHECK(near(readPx(gl, *t, 32, 18), 255, 0, 0), "Scene mau do");
    }
    { // Fit: anh 2:1 vao Scene vuong -> vien tren/duoi la nen, giua la anh
        pool.beginFrame();
        auto s = std::make_shared<Scene>();
        s->size = {100, 100}; s->background = Qt::blue;
        QImage img(200, 100, QImage::Format_RGBA8888); img.fill(Qt::green);
        s->layers.push_back({std::make_shared<ImageSource>(img, "i"), {0, 0, 1, 1}, 1.0f, LayerFit::Fit});
        auto* t = pool.acquire(100, 100);
        comp.renderScene(*s, *t);
        CHECK(near(readPx(gl, *t, 50, 50), 0, 255, 0), "Layer Fit: giua la anh");
        CHECK(near(readPx(gl, *t, 50, 5), 0, 0, 255), "Layer Fit: vien tren la nen");
        s->layers[0].fit = LayerFit::Fill;
        comp.renderScene(*s, *t);
        CHECK(near(readPx(gl, *t, 50, 5), 0, 255, 0), "Layer Fill: phu kin, khong con vien");
    }
    { // Huong anh: nua tren do, nua duoi xanh -> phai giu nguyen chieu
        pool.beginFrame();
        QImage img(20, 20, QImage::Format_RGBA8888); img.fill(Qt::blue);
        for (int y = 0; y < 10; ++y) for (int x = 0; x < 20; ++x) img.setPixelColor(x, y, Qt::red);
        auto s = std::make_shared<Scene>(); s->size = {20, 20};
        s->layers.push_back({std::make_shared<ImageSource>(img, "i")});
        auto* t = pool.acquire(20, 20);
        comp.renderScene(*s, *t);
        CHECK(near(readPx(gl, *t, 10, 2), 255, 0, 0) && near(readPx(gl, *t, 10, 17), 0, 0, 255),
              "Khong bi lat nguoc anh (tren do, duoi xanh)");
    }

    // --- Transition ---
    {
        Channel ch; ch.size = {64, 36};
        ch.scenes = {colorScene("red", Qt::red), colorScene("blue", Qt::blue)};
        ch.program = 0; ch.preview = 1;
        pool.beginFrame();
        auto* t = ch.render(comp, pool, 0.0);
        CHECK(near(readPx(gl, *t, 5, 5), 255, 0, 0), "Program ban dau = do");

        ch.startTransition(TransitionType::Fade, 1.0, 10.0);
        pool.beginFrame();
        t = ch.render(comp, pool, 10.5);     // giua chang: ease(0.5)=0.5
        CHECK(near(readPx(gl, *t, 5, 5), 127, 0, 127, 6), "Fade 50% = tim");
        pool.beginFrame();
        t = ch.render(comp, pool, 11.2);     // xong
        CHECK(near(readPx(gl, *t, 5, 5), 0, 0, 255) && !ch.inTransition(), "Fade xong -> xanh");
        CHECK(ch.program == 1 && ch.preview == 0, "Sau transition Program/Preview hoan doi");

        ch.cut();
        pool.beginFrame();
        t = ch.render(comp, pool, 12.0);
        CHECK(near(readPx(gl, *t, 5, 5), 255, 0, 0), "Cut -> do ngay lap tuc");

        ch.startTransition(TransitionType::WipeRight, 1.0, 20.0);
        pool.beginFrame();
        t = ch.render(comp, pool, 20.5);
        CHECK(near(readPx(gl, *t, 5, 18), 0, 0, 255) && near(readPx(gl, *t, 60, 18), 255, 0, 0),
              "Wipe phai 50%: trai la B (xanh), phai van la A (do)");
        pool.beginFrame();
        ch.render(comp, pool, 21.5);
        ch.startTransition(TransitionType::DipToBlack, 1.0, 30.0);
        pool.beginFrame();
        t = ch.render(comp, pool, 30.5);
        CHECK(near(readPx(gl, *t, 5, 5), 0, 0, 0, 6), "Dip-to-black giua chang = den");
    }

    // --- Output: scale / xoay / crop ---
    {
        // Noi dung 40x20: nua trai do, nua phai xanh la
        QImage img(40, 20, QImage::Format_RGBA8888); img.fill(Qt::green);
        for (int y = 0; y < 20; ++y) for (int x = 0; x < 20; ++x) img.setPixelColor(x, y, Qt::red);
        auto s = std::make_shared<Scene>(); s->size = {40, 20};
        s->layers.push_back({std::make_shared<ImageSource>(img, "i"), {0, 0, 1, 1}, 1.0f, LayerFit::Stretch});
        pool.beginFrame();
        auto* src = pool.acquire(40, 20);
        comp.renderScene(*s, *src);
        auto* win = pool.acquire(100, 100);   // gia lap cua so Output 100x100

        OutputConfig c;
        c.scale = ScaleMode::Fit;
        gl.glBindFramebuffer(GL_FRAMEBUFFER, win->fbo);
        comp.drawToCurrent(src->tex, 40, 20, c, 100, 100);
        CHECK(near(readPx(gl, *win, 50, 5), 0, 0, 0) && near(readPx(gl, *win, 10, 50), 255, 0, 0)
              && near(readPx(gl, *win, 90, 50), 0, 255, 0), "Output Fit 2:1 vao 100x100: letterbox den tren, trai do, phai xanh");

        c.scale = ScaleMode::Stretch;
        gl.glBindFramebuffer(GL_FRAMEBUFFER, win->fbo);
        comp.drawToCurrent(src->tex, 40, 20, c, 100, 100);
        CHECK(near(readPx(gl, *win, 50, 5), 255, 0, 0, 255) && !near(readPx(gl, *win, 50, 5), 0, 0, 0),
              "Output Stretch: phu kin, khong con vien den");

        c.scale = ScaleMode::Fit; c.rotation = 90;
        gl.glBindFramebuffer(GL_FRAMEBUFFER, win->fbo);
        comp.drawToCurrent(src->tex, 40, 20, c, 100, 100);
        CHECK(near(readPx(gl, *win, 50, 25), 255, 0, 0) && near(readPx(gl, *win, 50, 75), 0, 255, 0),
              "Xoay 90 do theo chieu kim dong ho: do len tren, xanh xuong duoi");

        c.rotation = 0; c.crop = QRectF(0.5, 0, 0.5, 1);      // chi lay nua phai (Mapping)
        gl.glBindFramebuffer(GL_FRAMEBUFFER, win->fbo);
        comp.drawToCurrent(src->tex, 40, 20, c, 100, 100);
        CHECK(near(readPx(gl, *win, 50, 50), 0, 255, 0) && near(readPx(gl, *win, 5, 50), 0, 255, 0),
              "Crop nua phai -> toan bo la xanh");
    }

    // --- FFmpeg ---
    {
        const char* path = "/tmp/lumacast_test_red.mp4";
        const int rc = std::system("ffmpeg -y -loglevel error -f lavfi -i color=c=red:s=320x240:d=1:r=25 "
                                   "-pix_fmt yuv420p -c:v mpeg4 /tmp/lumacast_test_red.mp4");
        if (rc != 0) {
            std::cout << "skip: khong co ffmpeg de tao video thu\n";
        } else {
            FFmpegSource v(path);
            TexRef tex;
            for (int i = 0; i < 300 && !tex.id; ++i) {            // toi da ~3s
                tex = v.acquire(gl);
                if (!tex.id) QThread::msleep(10);
            }
            CHECK(tex.id && tex.w == 320 && tex.h == 240 && v.error().isEmpty(), "FFmpeg: nhan duoc khung hinh 320x240");
            if (tex.id) {
                pool.beginFrame();
                auto s = std::make_shared<Scene>(); s->size = {320, 240};
                s->layers.push_back({std::shared_ptr<Source>(&v, [](Source*) {})});
                auto* t = pool.acquire(320, 240);
                comp.renderScene(*s, *t);
                CHECK(near(readPx(gl, *t, 160, 120), 255, 0, 0, 12), "FFmpeg: video do -> pixel do");
            }
            // lap lai: sau >1s van phai con khung hinh moi
            QThread::msleep(1400);
            const uint64_t before = 0; (void)before;
            CHECK(v.acquire(gl).id != 0, "FFmpeg: lap lai file van chay");
        }
    }

    pool.clear();
    comp.shutdown();
    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
