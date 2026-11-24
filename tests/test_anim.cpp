// Test M4b: keyframe, dong ho Scene, xoay/crop/blend Layer, luu/mo.
#include "core/Channel.h"
#include "core/Project.h"
#include "media/StaticSources.h"
#include "render/Compositor.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QTemporaryDir>
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
static bool near(Px p, int r, int g, int b, int tol = 5) { return std::abs(p.r - r) <= tol && std::abs(p.g - g) <= tol && std::abs(p.b - b) <= tol; }
static bool close(double a, double b, double tol = 1e-9) { return std::abs(a - b) <= tol; }

static QImage solid(int w, int h, QColor c) { QImage i(w, h, QImage::Format_RGBA8888); i.fill(c); return i; }

// 4 goc: TL do, TR xanh la, BL xanh duong, BR vang
static QImage quadrants(int w, int h) {
    QImage i(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
        i.setPixelColor(x, y, y < h / 2 ? (x < w / 2 ? Qt::red : Qt::green) : (x < w / 2 ? Qt::blue : Qt::yellow));
    return i;
}

class RotImage : public ImageSource {            // nguon bao co xoay hien thi (nhu video dien thoai)
public:
    RotImage(QImage img, int rot) : ImageSource(img, "rot"), rot_(rot) {}
    TexRef acquire(GL& gl) override { TexRef t = ImageSource::acquire(gl); t.rotation = rot_; return t; }
private:
    int rot_;
};

static Track track(AnimProp p, std::vector<Keyframe> k) { Track t; t.prop = p; t.keys = std::move(k); return t; }

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);
    QGuiApplication app(argc, argv);

    // ---------- Noi suy keyframe (khong can GL) ----------
    {
        Track lin = track(AnimProp::Opacity, {{0, 0, Interp::Linear}, {1, 1, Interp::Linear}});
        CHECK(close(evalTrack(lin, 0.5), 0.5) && close(evalTrack(lin, -1), 0) && close(evalTrack(lin, 2), 1), "Tuyen tinh: giua = 0.5; truoc key dau / sau key cuoi giu gia tri bien");
        CHECK(close(evalTrack(track(AnimProp::X, {{0, 0, Interp::Hold}, {1, 1}}), 0.99), 0) && close(evalTrack(track(AnimProp::X, {{0, 0, Interp::Hold}, {1, 1}}), 1.0), 1),
              "Hold: giu gia tri key truoc cho den het doan, nhay bac tai key sau");
        auto at = [](Interp i, double t) { return evalTrack(track(AnimProp::X, {{0, 0, i}, {1, 1}}), t); };
        CHECK(close(at(Interp::EaseIn, 0.5), 0.25) && close(at(Interp::EaseOut, 0.5), 0.75) && close(at(Interp::EaseInOut, 0.5), 0.5) && at(Interp::EaseInOut, 0.25) < 0.25,
              "Easing: EaseIn 0.25, EaseOut 0.75, EaseInOut doi xung va cham o dau");
        Track three = track(AnimProp::X, {{0, 0, Interp::Linear}, {1, 10, Interp::Hold}, {2, 20, Interp::Linear}});
        CHECK(close(evalTrack(three, 0.5), 5) && close(evalTrack(three, 1.5), 10) && close(evalTrack(three, 2.5), 20), "Nhieu key: moi doan dung interp cua key truoc no");
        CHECK(close(evalTrack(track(AnimProp::X, {{3, 7}}), 0), 7) && close(evalTrack(track(AnimProp::X, {{3, 7}}), 99), 7) && evalTrack(Track{}, 1) == 0, "Mot key = hang so; track rong = 0");

        Track t; t.prop = AnimProp::Y;
        setKey(t, 2, 20, Interp::Linear); setKey(t, 0, 0, Interp::Linear); setKey(t, 1, 10, Interp::Hold);
        CHECK(t.keys.size() == 3 && t.keys[0].time == 0 && t.keys[1].time == 1 && t.keys[2].time == 2, "setKey chen dung cho, luon sap xep theo thoi gian");
        const int idx = setKey(t, 1.0004, 99, Interp::EaseIn);
        CHECK(t.keys.size() == 3 && idx == 1 && t.keys[1].value == 99 && t.keys[1].interp == Interp::EaseIn, "setKey gan trung thoi gian (<1ms) thi ghi de, khong tao key moi");
        CHECK(findKey(t, 2) == 2 && findKey(t, 1.5) == -1, "findKey");
        removeKey(t, 1); removeKey(t, 9);
        CHECK(t.keys.size() == 2 && t.keys[1].time == 2, "removeKey (chi so sai bi bo qua)");

        Layer l; l.rect = QRectF(0, 0, 0.25, 0.25); l.opacity = 0.8f; l.rotation = 10;
        l.tracks.push_back(track(AnimProp::X, {{0, 0}, {1, 0.75}}));
        l.tracks.push_back(track(AnimProp::Rotation, {{0, 0}, {1, 90}}));
        l.tracks.push_back(track(AnimProp::Opacity, {{0, 0}, {1, 3}}));            // vuot 1 -> bi kep
        LayerState s = evalLayer(l, 0.5);
        CHECK(close(s.rect.x(), 0.375) && close(s.rect.width(), 0.25) && close(s.rotation, 45, 1e-4) && s.opacity == 1.0f && close(s.rect.y(), 0),
              "evalLayer: track ghi de gia tri tinh, thuoc tinh khong co track giu gia tri tinh, opacity bi kep [0,1]");
        CHECK(evalLayer(Layer{}, 5).opacity == 1.0f && evalLayer(l, 1.0).rect.x() > 0.74, "evalLayer: Layer khong co track = gia tri tinh");
    }

    // ---------- Dong ho Scene ----------
    {
        SceneClock c;
        c.tick(1.0, 2.0, true);
        CHECK(!c.playing && c.time == 0, "Dong ho dung: tick khong lam gi");
        c.play(10.0, 2.0);
        c.tick(10.5, 2.0, true);
        CHECK(close(c.time, 0.5), "Play: tien theo thoi gian engine");
        c.tick(11.7, 2.0, true);
        CHECK(close(c.time, 1.7), "Tiep tuc tien");
        c.tick(12.3, 2.0, true);
        CHECK(close(c.time, 0.3) && c.playing, "Het thoi luong + loop: quay ve dau, van phat");
        c.pause();
        c.tick(20.0, 2.0, true);
        c.play(30.0, 2.0);
        c.tick(30.1, 2.0, true);
        CHECK(close(c.time, 0.4), "Pause roi Play: khong bi nhay thoi gian trong luc dung");
        SceneClock n;
        n.play(0, 2.0);
        n.tick(1.5, 2.0, false);
        n.tick(2.6, 2.0, false);
        CHECK(n.time == 2.0 && !n.playing, "Het thoi luong, khong loop: dung o cuoi");
        n.play(5.0, 2.0);
        CHECK(n.time == 0 && n.playing, "Play khi dang o cuoi: bat dau lai tu 0");
        n.seek(1.25);
        n.seek(-3);
        CHECK(n.time == 0, "seek am bi kep ve 0");
        n.seek(1.25);
        CHECK(n.time == 1.25, "seek");
        n.restart(-1);
        n.tick(50.0, 2.0, true);
        CHECK(n.time == 0 && n.playing, "restart(-1): lan tick dau chi dat moc, chua tien");
        n.tick(50.4, 2.0, true);
        CHECK(close(n.time, 0.4), "Sau do tien binh thuong");
        n.stop();
        CHECK(n.time == 0 && !n.playing, "stop");
        SceneClock cp = n;
        CHECK(cp.time == n.time, "SceneClock sao chep duoc");
    }

    // ---------- cropToTexture ----------
    {
        const QRectF d(0.1, 0.2, 0.3, 0.4);
        auto eq = [](QRectF a, QRectF b) { return close(a.x(), b.x(), 1e-9) && close(a.y(), b.y(), 1e-9) && close(a.width(), b.width(), 1e-9) && close(a.height(), b.height(), 1e-9); };
        CHECK(eq(cropToTexture(d, 0), d) && eq(cropToTexture(d, 90), QRectF(0.2, 0.6, 0.4, 0.3)) && eq(cropToTexture(d, 180), QRectF(0.6, 0.4, 0.3, 0.4)) &&
                  eq(cropToTexture(d, 270), QRectF(0.4, 0.1, 0.4, 0.3)) && eq(cropToTexture(d, 450), cropToTexture(d, 90)),
              "cropToTexture: 0/90/180/270 doi vung theo hinh da xoay ra vung tren texture goc");
    }

    // ---------- GL ----------
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
    EffectRegistry reg;
    reg.loadBuiltins();
    comp.setEffectContext(&reg, &pool);

    auto sceneOf = [](QSize sz, QColor bg, std::vector<Layer> layers) {
        auto s = std::make_shared<Scene>();
        s->size = sz; s->background = bg; s->layers = std::move(layers);
        return s;
    };
    auto layerOf = [](std::shared_ptr<Source> src, QRectF rect) {
        Layer l; l.source = src; l.rect = rect; l.fit = LayerFit::Stretch; return l;
    };
    auto render = [&](std::shared_ptr<Scene> s, double time = 0) {
        pool.beginFrame();
        s->clock.time = time;
        auto* t = pool.acquire(s->size.width(), s->size.height());
        comp.renderScene(*s, *t);
        return t;
    };

    // ---------- Keyframe tren hinh that ----------
    {
        auto white = std::make_shared<ImageSource>(solid(8, 8, Qt::white), "w");
        Layer l = layerOf(white, QRectF(0, 0.375, 0.25, 0.25));
        l.tracks.push_back(track(AnimProp::X, {{0, 0}, {1, 0.75}}));
        auto s = sceneOf({64, 64}, Qt::black, {l});
        auto* t = render(s, 0.0);
        CHECK(near(readPx(gl, *t, 8, 32), 255, 255, 255) && near(readPx(gl, *t, 56, 32), 0, 0, 0), "Keyframe X tai t=0: o vuong o ben trai");
        t = render(s, 0.5);
        CHECK(near(readPx(gl, *t, 32, 32), 255, 255, 255) && near(readPx(gl, *t, 8, 32), 0, 0, 0) && near(readPx(gl, *t, 56, 32), 0, 0, 0), "Keyframe X tai t=0.5: o vuong o giua");
        t = render(s, 1.0);
        CHECK(near(readPx(gl, *t, 56, 32), 255, 255, 255) && near(readPx(gl, *t, 8, 32), 0, 0, 0), "Keyframe X tai t=1: o vuong o ben phai");
        t = render(s, 7.0);
        CHECK(near(readPx(gl, *t, 56, 32), 255, 255, 255), "Sau key cuoi: giu vi tri cuoi");

        auto red = std::make_shared<ColorSource>(Qt::red, "r");
        Layer o = layerOf(red, QRectF(0, 0, 1, 1));
        o.tracks.push_back(track(AnimProp::Opacity, {{0, 0}, {1, 1}}));
        auto so = sceneOf({16, 16}, Qt::blue, {o});
        CHECK(near(readPx(gl, *render(so, 0.0), 8, 8), 0, 0, 255) && near(readPx(gl, *render(so, 0.5), 8, 8), 127, 0, 127, 6) && near(readPx(gl, *render(so, 1.0), 8, 8), 255, 0, 0),
              "Keyframe opacity: xanh -> tim -> do theo thoi gian");
        Layer h = layerOf(red, QRectF(0, 0, 1, 1));
        h.tracks.push_back(track(AnimProp::Opacity, {{0, 0, Interp::Hold}, {1, 1}}));
        auto sh = sceneOf({16, 16}, Qt::blue, {h});
        CHECK(near(readPx(gl, *render(sh, 0.99), 8, 8), 0, 0, 255) && near(readPx(gl, *render(sh, 1.0), 8, 8), 255, 0, 0), "Hold: nhay bac, khong pha tron");

        Layer ra = layerOf(std::make_shared<ImageSource>(solid(8, 8, Qt::white), "w"), QRectF(0.375, 0.25, 0.25, 0.5));
        ra.tracks.push_back(track(AnimProp::Rotation, {{0, 0}, {1, 90}}));
        auto sr = sceneOf({128, 64}, Qt::black, {ra});
        Layer st = ra; st.tracks.clear(); st.rotation = 45;
        auto ss = sceneOf({128, 64}, Qt::black, {st});
        auto* a = render(sr, 0.5);
        const bool animIn = near(readPx(gl, *a, 74, 42), 255, 255, 255), animOut = near(readPx(gl, *a, 78, 46), 0, 0, 0);
        auto* b = render(ss, 0);
        CHECK(animIn && animOut && near(readPx(gl, *b, 74, 42), 255, 255, 255) && near(readPx(gl, *b, 78, 46), 0, 0, 0), "Keyframe xoay 0->90 tai t=0.5 giong xoay tinh 45 do");
    }

    // ---------- Xoay Layer ----------
    {
        QImage lr(32, 16, QImage::Format_RGBA8888);
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 32; ++x) lr.setPixelColor(x, y, x < 16 ? Qt::red : Qt::green);
        Layer l = layerOf(std::make_shared<ImageSource>(lr, "lr"), QRectF(0.25, 0.375, 0.5, 0.25));
        auto s0 = sceneOf({64, 64}, Qt::black, {l});
        auto* t = render(s0);
        CHECK(near(readPx(gl, *t, 20, 32), 255, 0, 0) && near(readPx(gl, *t, 44, 32), 0, 255, 0) && near(readPx(gl, *t, 32, 10), 0, 0, 0), "Layer ngang chua xoay: do trai, xanh phai");
        l.rotation = 90;
        auto s1 = sceneOf({64, 64}, Qt::black, {l});
        t = render(s1);
        CHECK(near(readPx(gl, *t, 32, 18), 255, 0, 0) && near(readPx(gl, *t, 32, 46), 0, 255, 0) && near(readPx(gl, *t, 20, 32), 0, 0, 0) && near(readPx(gl, *t, 32, 10), 0, 0, 0),
              "Xoay 90 theo chieu kim dong ho: phan do len tren, xanh xuong duoi, hinh thanh thanh doc");
        l.rotation = 180;
        t = render(sceneOf({64, 64}, Qt::black, {l}));
        CHECK(near(readPx(gl, *t, 44, 32), 255, 0, 0) && near(readPx(gl, *t, 20, 32), 0, 255, 0), "Xoay 180: do sang phai");
        l.rotation = -90;
        t = render(sceneOf({64, 64}, Qt::black, {l}));
        CHECK(near(readPx(gl, *t, 32, 46), 255, 0, 0) && near(readPx(gl, *t, 32, 18), 0, 255, 0), "Xoay -90 (nguoc chieu kim dong ho): do xuong duoi");

        // Scene KHONG vuong: hinh vuong xoay 45 do van la hinh thoi (khong bi meo theo ti le man hinh)
        Layer sq = layerOf(std::make_shared<ImageSource>(solid(8, 8, Qt::white), "w"), QRectF(0.375, 0.25, 0.25, 0.5));
        sq.rotation = 45;
        t = render(sceneOf({128, 64}, Qt::black, {sq}));
        CHECK(near(readPx(gl, *t, 74, 42), 255, 255, 255) && near(readPx(gl, *t, 84, 32), 255, 255, 255) && near(readPx(gl, *t, 64, 52), 255, 255, 255) &&
                  near(readPx(gl, *t, 78, 46), 0, 0, 0) && near(readPx(gl, *t, 49, 17), 0, 0, 0),
              "Xoay 45 tren Scene 128x64: hinh vuong thanh hinh thoi dung (khong meo), goc cu da trong");
    }

    // ---------- Crop ----------
    {
        auto quad = std::make_shared<ImageSource>(quadrants(64, 64), "q");
        auto crop = [&](QRectF c) { Layer l = layerOf(quad, QRectF(0, 0, 1, 1)); l.crop = c; return sceneOf({64, 64}, Qt::black, {l}); };
        CHECK(near(readPx(gl, *render(crop(QRectF(0, 0, 0.5, 0.5))), 32, 32), 255, 0, 0) && near(readPx(gl, *render(crop(QRectF(0.5, 0.5, 0.5, 0.5))), 5, 5), 255, 255, 0),
              "Crop goc tren-trai -> toan do; crop goc duoi-phai -> toan vang");
        auto* t = render(crop(QRectF(0.25, 0.25, 0.5, 0.5)));
        CHECK(near(readPx(gl, *t, 2, 2), 255, 0, 0) && near(readPx(gl, *t, 61, 2), 0, 255, 0) && near(readPx(gl, *t, 2, 61), 0, 0, 255) && near(readPx(gl, *t, 61, 61), 255, 255, 0),
              "Crop o giua: bon goc cua Layer hien du bon phan tu");
        t = render(crop(QRectF(0, 0, 0, 0)));
        CHECK(near(readPx(gl, *t, 2, 2), 255, 0, 0) && near(readPx(gl, *t, 61, 61), 255, 255, 0), "Crop co kich thuoc 0 -> dung toan bo anh (khong crash, khong den)");

        // video dien thoai: nguon luu 64x64 bon phan tu, hien thi xoay 90 -> hien thi: TL xanh duong, TR do, BR xanh la, BL vang
        auto r90 = std::make_shared<RotImage>(quadrants(64, 64), 90);
        auto rc = [&](std::shared_ptr<Source> s, QRectF c) { Layer l = layerOf(s, QRectF(0, 0, 1, 1)); l.crop = c; return sceneOf({64, 64}, Qt::black, {l}); };
        t = render(rc(r90, QRectF(0, 0, 1, 1)));
        CHECK(near(readPx(gl, *t, 2, 2), 0, 0, 255) && near(readPx(gl, *t, 61, 2), 255, 0, 0) && near(readPx(gl, *t, 61, 61), 0, 255, 0) && near(readPx(gl, *t, 2, 61), 255, 255, 0),
              "Nguon xoay 90: hien thi dung chieu (TL xanh duong, TR do, BR xanh la, BL vang)");
        CHECK(near(readPx(gl, *render(rc(r90, QRectF(0.5, 0, 0.5, 0.5))), 32, 32), 255, 0, 0) && near(readPx(gl, *render(rc(r90, QRectF(0, 0.5, 0.5, 0.5))), 32, 32), 255, 255, 0) &&
                  near(readPx(gl, *render(rc(r90, QRectF(0.5, 0.5, 0.5, 0.5))), 32, 32), 0, 255, 0) && near(readPx(gl, *render(rc(r90, QRectF(0, 0, 0.5, 0.5))), 32, 32), 0, 0, 255),
              "Crop THEO HINH DA XOAY voi nguon xoay 90: dung phan tu duoc chon");
        auto r270 = std::make_shared<RotImage>(quadrants(64, 64), 270);
        CHECK(near(readPx(gl, *render(rc(r270, QRectF(0.5, 0.5, 0.5, 0.5))), 32, 32), 0, 0, 255) && near(readPx(gl, *render(rc(r270, QRectF(0, 0, 0.5, 0.5))), 32, 32), 0, 255, 0),
              "Nguon xoay 270: crop goc duoi-phai = xanh duong, goc tren-trai = xanh la");
        auto r180 = std::make_shared<RotImage>(quadrants(64, 64), 180);
        CHECK(near(readPx(gl, *render(rc(r180, QRectF(0, 0, 0.5, 0.5))), 32, 32), 255, 255, 0) && near(readPx(gl, *render(rc(r180, QRectF(0.5, 0.5, 0.5, 0.5))), 32, 32), 255, 0, 0),
              "Nguon xoay 180: crop goc tren-trai = vang, goc duoi-phai = do");

        // Fill ket hop crop: anh 64x32 (trai do, phai xanh) vao Scene vuong 32x32
        QImage two(64, 32, QImage::Format_RGBA8888);
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 64; ++x) two.setPixelColor(x, y, x < 32 ? Qt::red : Qt::blue);
        Layer f = layerOf(std::make_shared<ImageSource>(two, "two"), QRectF(0, 0, 1, 1));
        f.fit = LayerFit::Fill;
        t = render(sceneOf({32, 32}, Qt::black, {f}));
        const bool fillOk = near(readPx(gl, *t, 4, 16), 255, 0, 0) && near(readPx(gl, *t, 28, 16), 0, 0, 255);
        f.crop = QRectF(0, 0, 0.5, 1);
        t = render(sceneOf({32, 32}, Qt::black, {f}));
        CHECK(fillOk && near(readPx(gl, *t, 4, 16), 255, 0, 0) && near(readPx(gl, *t, 28, 16), 255, 0, 0), "Fill cat o giua; Fill + crop nua trai (ti le 1:1) -> toan do");
        Layer fit = layerOf(std::make_shared<ImageSource>(two, "two"), QRectF(0, 0, 1, 1));
        fit.fit = LayerFit::Fit;
        fit.crop = QRectF(0, 0, 0.5, 1);                        // vung 32x32 -> khung vuong: lap day khong con vien
        t = render(sceneOf({32, 32}, Qt::black, {fit}));
        CHECK(near(readPx(gl, *t, 2, 2), 255, 0, 0) && near(readPx(gl, *t, 29, 29), 255, 0, 0), "Fit + crop: ti le tinh theo vung da crop");
    }

    // ---------- Blend mode ----------
    {
        auto layerWith = [&](QColor c, BlendMode m, float op) { Layer l = layerOf(std::make_shared<ImageSource>(solid(4, 4, c), "c"), QRectF(0, 0, 1, 1)); l.blend = m; l.opacity = op; return l; };
        auto px = [&](QColor bg, QColor c, BlendMode m, float op) {
            auto* t = render(sceneOf({8, 8}, bg, {layerWith(c, m, op)}));
            return readPx(gl, *t, 4, 4);
        };
        CHECK(near(px(QColor(128, 128, 128), Qt::white, BlendMode::Normal, 0.5f), 191, 191, 191, 3), "Normal alpha 0.5: trung binh");
        CHECK(near(px(QColor(128, 128, 128), QColor(64, 64, 64), BlendMode::Add, 1.0f), 192, 192, 192, 3), "Add: 128 + 64 = 192");
        CHECK(near(px(QColor(128, 128, 128), QColor(128, 128, 128), BlendMode::Add, 0.5f), 192, 192, 192, 3), "Add co opacity 0.5: cong 64");
        CHECK(near(px(QColor(255, 255, 255), QColor(128, 128, 128), BlendMode::Add, 1.0f), 255, 255, 255, 1), "Add bi kep o 255");
        CHECK(near(px(QColor(200, 200, 200), QColor(128, 128, 128), BlendMode::Multiply, 1.0f), 100, 100, 100, 3), "Multiply: 200 x 0.5 = 100");
        CHECK(near(px(QColor(200, 200, 200), QColor(128, 128, 128), BlendMode::Multiply, 0.5f), 150, 150, 150, 3), "Multiply co opacity 0.5: pha giua goc (200) va ket qua (100)");
        CHECK(near(px(QColor(200, 200, 200), Qt::white, BlendMode::Multiply, 1.0f), 200, 200, 200, 2), "Multiply voi trang = khong doi");
        CHECK(near(px(QColor(128, 128, 128), QColor(128, 128, 128), BlendMode::Screen, 1.0f), 191, 191, 191, 3), "Screen: 0.5 va 0.5 -> 0.75");
        CHECK(near(px(QColor(128, 128, 128), Qt::black, BlendMode::Screen, 1.0f), 128, 128, 128, 2), "Screen voi den = khong doi");
        // blend khong 'ro' sang layer sau: layer Add roi layer Normal
        Layer a = layerWith(QColor(64, 64, 64), BlendMode::Add, 1.0f), n = layerWith(Qt::red, BlendMode::Normal, 1.0f);
        n.rect = QRectF(0.5, 0, 0.5, 1);
        auto* t = render(sceneOf({8, 8}, QColor(128, 128, 128), {a, n}));
        CHECK(near(readPx(gl, *t, 1, 4), 192, 192, 192, 3) && near(readPx(gl, *t, 6, 4), 255, 0, 0, 2), "Layer Add roi Layer Normal: moi layer dung che do cua minh");
    }

    // ---------- Channel: Scene tu phat lai khi len song ----------
    {
        auto mk = [&](bool restart) {
            auto s = sceneOf({8, 8}, Qt::black, {layerOf(std::make_shared<ColorSource>(Qt::red, "r"), QRectF(0, 0, 1, 1))});
            s->restartOnAir = restart;
            s->clock.time = 3.0;
            return s;
        };
        Channel ch; ch.size = {8, 8};
        auto a = mk(true), b = mk(true), c = mk(false);
        ch.scenes = {a, b, c};
        ch.program = 0; ch.preview = 1;
        ch.cut(5.0);
        CHECK(b->clock.playing && b->clock.time == 0 && !a->clock.playing && a->clock.time == 3.0, "CUT: Scene moi len Program tu phat lai tu dau, Scene cu khong bi dong vao");
        ch.setPreview(2);
        ch.cut(6.0);
        CHECK(!c->clock.playing && c->clock.time == 3.0, "Scene tat 'phat lai khi len song' khong bi dong vao");
        ch.setPreview(0);
        ch.startTransition(TransitionType::Fade, 1.0, 10.0);
        CHECK(a->clock.playing && a->clock.time == 0 && ch.inTransition(), "TAKE: Scene vao bat dau chay hoat hoa ngay khi bat dau chuyen");
        a->clock.stop(); a->clock.time = 2.0;
        ch.cut(10.5);                                                // hoan tat giua chung (cut trong luc chuyen)
        Channel ch2; ch2.size = {8, 8};
        auto p = mk(true), q = mk(true);
        ch2.scenes = {p, q}; ch2.program = 0; ch2.preview = 1;
        ch2.setManual(TransitionType::Fade, 0.3f);
        CHECK(q->clock.playing && q->clock.time == 0, "Keo T-bar: Scene vao bat dau chay khi vua keo");
    }

    // ---------- Luu / mo ----------
    {
        QTemporaryDir dir;
        Project p;
        SourceDef c; c.type = "color"; c.color = Qt::red; c.label = "do";
        auto src = p.addSource(c);
        auto s = std::make_shared<Scene>();
        s->name = "A"; s->size = {64, 36}; s->duration = 7.5; s->loop = false; s->restartOnAir = false;
        Layer l; l.source = src; l.rotation = 30; l.crop = QRectF(0.1, 0.2, 0.5, 0.6); l.blend = BlendMode::Multiply;
        l.tracks.push_back(track(AnimProp::X, {{0, 0.1, Interp::Hold}, {2, 0.5, Interp::EaseInOut}, {4, 0.9, Interp::Linear}}));
        l.tracks.push_back(track(AnimProp::Opacity, {{1, 0.25}}));
        s->layers.push_back(l);
        p.scenes = {s};
        auto ch = std::make_shared<Channel>();
        ch->scenes = p.scenes; ch->program = 0; ch->preview = -1;
        p.channels = {ch};
        OutputConfig o; o.name = "O";
        p.outputs = {o};
        const QString file = dir.filePath("anim.json");
        QString err;
        CHECK(saveProject(p, file, &err), "Luu project co keyframe");
        Project q;
        CHECK(loadProject(q, file, &err), "Mo lai project co keyframe: " + err.toStdString());
        const auto& sc = *q.scenes[0];
        const Layer& r = sc.layers[0];
        CHECK(close(sc.duration, 7.5) && !sc.loop && !sc.restartOnAir && close(r.rotation, 30) && r.crop == QRectF(0.1, 0.2, 0.5, 0.6) && r.blend == BlendMode::Multiply,
              "Round-trip: thoi luong, loop, restartOnAir, xoay, crop, blend");
        const Track* tx = r.track(AnimProp::X);
        CHECK(tx && tx->keys.size() == 3 && tx->keys[0].interp == Interp::Hold && tx->keys[1].interp == Interp::EaseInOut && close(tx->keys[2].value, 0.9) &&
                  r.track(AnimProp::Opacity) && r.track(AnimProp::Opacity)->keys.size() == 1 && !r.track(AnimProp::Rotation),
              "Round-trip: keyframe (thoi gian, gia tri, kieu noi)");

        // file viet tay co du lieu xau: phai doc duoc, khong crash, du lieu hong bi bo/sua
        const QString bad = R"({"app":"LumaCast","version":1,"sources":[{"id":"s","type":"color","color":"#ffff0000"}],
            "scenes":[{"name":"X","duration":0,"layers":[{"source":"s","blend":"khong_biet","crop":[0,0,0,0],
                "tracks":[{"prop":"x","keys":[{"t":3,"v":3},{"t":-5,"v":1},{"t":1,"v":2,"i":"loai_la"}]},
                          {"prop":"x","keys":[{"t":0,"v":9}]},
                          {"prop":"thuoc_tinh_la","keys":[{"t":0,"v":1}]},
                          {"prop":"y","keys":[]}]}]}],
            "channels":[{"scenes":[0]}],"outputs":[{"channel":0}]})";
        QFile f(dir.filePath("bad.json")); f.open(QIODevice::WriteOnly); f.write(bad.toUtf8()); f.close();
        Project b;
        CHECK(loadProject(b, dir.filePath("bad.json"), &err), "Mo file keyframe viet tay co du lieu xau: " + err.toStdString());
        const Layer& bl = b.scenes[0]->layers[0];
        const Track* bx = bl.track(AnimProp::X);
        CHECK(close(b.scenes[0]->duration, 10.0) && bl.blend == BlendMode::Normal && bl.crop == QRectF(0, 0, 1, 1), "Thoi luong 0, blend la, crop rong -> ve gia tri an toan");
        CHECK(bx && bx->keys.size() == 3 && bx->keys[0].time == 0 && bx->keys[1].time == 1 && bx->keys[2].time == 3 && bx->keys[1].interp == Interp::Linear &&
                  bl.tracks.size() == 1, "Key sap xep lai, thoi gian am -> 0, kieu noi la -> Linear, track trung/la/rong bi bo");
    }

    pool.clear();
    comp.shutdown();
    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
