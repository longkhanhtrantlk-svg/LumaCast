// Test render khong can cua so: ve vao FBO roi doc lai pixel.
#include "core/Channel.h"
#include "core/Project.h"
#include "media/FFmpegSource.h"
#include "media/StaticSources.h"
#include "render/Compositor.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
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


    // --- Hieu chinh mau (Calibration) ---
    {
        auto s = std::make_shared<Scene>(); s->size = {8, 8};
        s->layers.push_back({std::make_shared<ColorSource>(QColor(64, 0, 0), "c"), {0, 0, 1, 1}, 1.0f, LayerFit::Stretch});
        pool.beginFrame();
        auto* src = pool.acquire(8, 8);
        comp.renderScene(*s, *src);
        auto* win = pool.acquire(8, 8);
        OutputConfig c;
        auto draw = [&](const OutputConfig& cfg) {
            gl.glBindFramebuffer(GL_FRAMEBUFFER, win->fbo);
            comp.drawToCurrent(src->tex, 8, 8, cfg, 8, 8);
            return readPx(gl, *win, 4, 4);
        };
        CHECK(near(draw(c), 64, 0, 0, 2), "Calibration mac dinh = khong doi mau");
        c.cal.brightness = 0.5f;
        CHECK(near(draw(c), 32, 0, 0, 2), "Calibration: brightness 0.5 -> 32");
        c.cal = Calibration{}; c.cal.gamma = 2.0f;
        CHECK(near(draw(c), 128, 0, 0, 3), "Calibration: gamma 2.0 -> 64 len ~128");
        c.cal = Calibration{}; c.cal.gainR = 0.0f;
        CHECK(near(draw(c), 0, 0, 0, 1), "Calibration: gain R = 0 -> mat kenh do");
        c.cal = Calibration{}; c.cal.contrast = 2.0f;
        CHECK(near(draw(c), 1, 0, 0, 3) || draw(c).r < 4, "Calibration: tuong phan 2 day toi xuong den");
    }

    // --- Video wall ---
    {
        auto wall = makeWall(0, 4, 1, QSize(1920, 1080), 0, false);
        CHECK(wall.size() == 4 && wall[2].crop == QRectF(0.5, 0, 0.25, 1) && wall[2].screenIndex == 2,
              "makeWall 4x1: o thu 3 cat 50%..75%, man hinh so 2");
        auto wall2 = makeWall(0, 2, 2, QSize(960, 540));
        CHECK(wall2.size() == 4 && wall2[3].crop == QRectF(0.5, 0.5, 0.5, 0.5), "makeWall 2x2: o cuoi la goc duoi-phai");

        // Canvas 8x2: nua trai do, nua phai xanh la, chia 2 output -> output 1 do het, output 2 xanh het
        QImage img(8, 2, QImage::Format_RGBA8888); img.fill(Qt::green);
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x) img.setPixelColor(x, y, Qt::red);
        auto s = std::make_shared<Scene>(); s->size = {8, 2};
        s->layers.push_back({std::make_shared<ImageSource>(img, "i"), {0, 0, 1, 1}, 1.0f, LayerFit::Stretch});
        pool.beginFrame();
        auto* canvas = pool.acquire(8, 2);
        comp.renderScene(*s, *canvas);
        auto* win = pool.acquire(4, 2);
        auto w2 = makeWall(0, 2, 1, QSize(4, 2));
        gl.glBindFramebuffer(GL_FRAMEBUFFER, win->fbo);
        comp.drawToCurrent(canvas->tex, 8, 2, w2[0], 4, 2);
        const Px a = readPx(gl, *win, 0, 0), b = readPx(gl, *win, 3, 1);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, win->fbo);
        comp.drawToCurrent(canvas->tex, 8, 2, w2[1], 4, 2);
        const Px c2 = readPx(gl, *win, 0, 0), d = readPx(gl, *win, 3, 1);
        CHECK(near(a, 255, 0, 0) && near(b, 255, 0, 0) && near(c2, 0, 255, 0) && near(d, 0, 255, 0),
              "Wall: canvas chia dung - output trai toan do, output phai toan xanh");
    }

    // --- Luu / mo project ---
    {
        QTemporaryDir dir;
        CHECK(dir.isValid(), "Tao thu muc tam");
        QImage red(10, 10, QImage::Format_RGBA8888); red.fill(Qt::red);
        const QString imgPath = dir.filePath("img.png");
        red.save(imgPath);

        Project p;
        p.fps = 50;
        SourceDef di; di.type = "image"; di.path = imgPath;
        SourceDef dc; dc.type = "color"; dc.color = QColor(10, 20, 30); dc.label = "nen";
        SourceDef dt; dt.type = "testpattern"; dt.size = QSize(320, 180); dt.label = "T";
        auto sImg = p.addSource(di), sCol = p.addSource(dc), sTest = p.addSource(dt);
        CHECK(sImg && sCol && sTest && p.sources.size() == 3, "Project: them 3 nguon");

        auto sc1 = std::make_shared<Scene>();
        sc1->name = "Mot"; sc1->size = {640, 360}; sc1->background = QColor(1, 2, 3);
        sc1->layers.push_back({sCol, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        sc1->layers.push_back({sImg, {0.25, 0.1, 0.5, 0.4}, 0.6f, LayerFit::Fill, false});
        auto sc2 = std::make_shared<Scene>();
        sc2->name = "Hai"; sc2->size = {1920, 1080};
        sc2->layers.push_back({sTest, {0, 0, 1, 1}, 1.0f, LayerFit::Fit, true});
        p.scenes = {sc1, sc2};
        auto ch = std::make_shared<Channel>();
        ch->name = "K1"; ch->size = {640, 360}; ch->scenes = {sc2, sc1}; ch->program = 1; ch->preview = 0;
        p.channels = {ch};
        OutputConfig oc; oc.name = "LED-A"; oc.channel = 0; oc.screenIndex = 3; oc.fullscreen = true;
        oc.size = {3840, 2160}; oc.scale = ScaleMode::Fill; oc.rotation = 270;
        oc.crop = QRectF(0.25, 0, 0.5, 1);
        oc.cal.brightness = 0.8f; oc.cal.gamma = 2.2f; oc.cal.gainB = 0.9f;
        p.outputs = {oc};

        QDir(dir.path()).mkpath("sub");
        const QString file = dir.filePath("sub/p.json");
        QString err;
        CHECK(saveProject(p, file, &err), "Luu project ra file");

        QFile f(file); f.open(QIODevice::ReadOnly);
        const QString text = QString::fromUtf8(f.readAll());
        CHECK(!text.contains(dir.path()) && text.contains("../img.png"), "Media luu theo duong dan TUONG DOI");

        Project q;
        CHECK(loadProject(q, file, &err), "Mo lai project: " + err.toStdString());
        CHECK(q.fps == 50 && q.sources.size() == 3 && q.scenes.size() == 2 && q.channels.size() == 1 && q.outputs.size() == 1,
              "Mo lai: so luong sources/scenes/channels/outputs dung");
        const auto& r1 = q.scenes[0];
        CHECK(r1->name == "Mot" && r1->size == QSize(640, 360) && r1->background == QColor(1, 2, 3) && r1->layers.size() == 2,
              "Mo lai: Scene (ten, kich thuoc, nen, 2 layer)");
        const Layer& L = r1->layers[1];
        CHECK(L.rect == QRectF(0.25, 0.1, 0.5, 0.4) && std::abs(L.opacity - 0.6f) < 1e-6 && L.fit == LayerFit::Fill && !L.visible,
              "Mo lai: Layer (rect, opacity, fit, visible)");
        CHECK(q.channels[0]->scenes.size() == 2 && q.channels[0]->scenes[0] == q.scenes[1] &&
                  q.channels[0]->program == 1 && q.channels[0]->preview == 0,
              "Mo lai: Channel dung thu tu scene, Program/Preview");
        const OutputConfig& o = q.outputs[0];
        CHECK(o.name == "LED-A" && o.screenIndex == 3 && o.fullscreen && o.size == QSize(3840, 2160) &&
                  o.scale == ScaleMode::Fill && o.rotation == 270 && o.crop == QRectF(0.25, 0, 0.5, 1) &&
                  std::abs(o.cal.brightness - 0.8f) < 1e-6 && std::abs(o.cal.gamma - 2.2f) < 1e-6 &&
                  std::abs(o.cal.gainB - 0.9f) < 1e-6 && o.cal.gainR == 1.0f,
              "Mo lai: Output (man hinh, scale, xoay, crop, hieu chinh mau)");

        Project bad;
        CHECK(!loadProject(bad, dir.filePath("khong_co.json"), &err) && !err.isEmpty(), "Mo file khong ton tai -> bao loi");
        QFile g(dir.filePath("hong.json")); g.open(QIODevice::WriteOnly); g.write("{ khong phai json"); g.close();
        CHECK(!loadProject(bad, dir.filePath("hong.json"), &err), "Mo JSON hong -> bao loi, khong crash");
        QFile h(dir.filePath("sai.json")); h.open(QIODevice::WriteOnly);
        h.write(R"({"app":"LumaCast","version":1,"sources":[],"scenes":[{"name":"x","layers":[{"source":"khong_co"}]}],
                    "channels":[{"scenes":[0]}],"outputs":[{"channel":0}]})"); h.close();
        CHECK(!loadProject(bad, dir.filePath("sai.json"), &err) && err.contains("khong_co"),
              "Layer tham chieu nguon khong ton tai -> bao loi ro rang");
        QFile k(dir.filePath("kenh.json")); k.open(QIODevice::WriteOnly);
        k.write(R"({"app":"LumaCast","version":1,"sources":[],"scenes":[{"name":"x"}],
                    "channels":[{"scenes":[0]}],"outputs":[{"channel":5}]})"); k.close();
        CHECK(!loadProject(bad, dir.filePath("kenh.json"), &err), "Output tro toi kenh khong ton tai -> bao loi");
    }


    // --- Fade-To-Black, thumbnail monitor, snapshot nguon ---
    {
        Channel ch; ch.size = {64, 36};
        ch.scenes = {colorScene("red", Qt::red), colorScene("blue", Qt::blue)};
        ch.program = 0; ch.preview = 1;
        pool.beginFrame();
        ch.render(comp, pool, 100.0);                       // moc thoi gian
        ch.toggleFtb(1.0);
        pool.beginFrame();
        auto* t = ch.render(comp, pool, 100.5);             // nua duong: do -> 50% den
        CHECK(near(readPx(gl, *t, 5, 5), 127, 0, 0, 8) && std::abs(ch.ftbAmount - 0.5f) < 0.02f, "FTB 50%: do giam con ~ nua");
        pool.beginFrame();
        t = ch.render(comp, pool, 101.5);
        CHECK(near(readPx(gl, *t, 5, 5), 0, 0, 0) && ch.ftbAmount == 1.0f, "FTB xong: den hoan toan");
        ch.toggleFtb(1.0);                                   // bam lan nua -> tro lai
        pool.beginFrame();
        ch.render(comp, pool, 103.0);
        CHECK(ch.ftbAmount == 0.0f, "FTB lan 2: tro lai hinh binh thuong");

        // Preview thumbnail: scene xanh duong; Program do
        pool.beginFrame();
        auto* prog = ch.render(comp, pool, 104.0);
        auto* prev = ch.renderPreview(comp, pool);
        QImage tp = comp.thumbnail(pool, *prog, 32), tv = comp.thumbnail(pool, *prev, 32);
        CHECK(tp.width() == 32 && tp.height() == 18 && tp.pixelColor(5, 5) == QColor(255, 0, 0) &&
                  tv.pixelColor(5, 5) == QColor(0, 0, 255), "Thumbnail: dung kich thuoc, Program do, Preview xanh");
    }
    { // thumbnail khong lat nguoc: nua tren do, nua duoi xanh
        QImage img(20, 20, QImage::Format_RGBA8888); img.fill(Qt::blue);
        for (int y = 0; y < 10; ++y) for (int x = 0; x < 20; ++x) img.setPixelColor(x, y, Qt::red);
        auto sc = std::make_shared<Scene>(); sc->size = {20, 20};
        sc->layers.push_back({std::make_shared<ImageSource>(img, "i"), {0, 0, 1, 1}, 1.0f, LayerFit::Stretch});
        pool.beginFrame();
        auto* t = pool.acquire(20, 20);
        comp.renderScene(*sc, *t);
        QImage th = comp.thumbnail(pool, *t, 10);
        CHECK(th.pixelColor(5, 1) == QColor(255, 0, 0) && th.pixelColor(5, 8) == QColor(0, 0, 255),
              "Thumbnail giu dung chieu (tren do, duoi xanh)");
        ImageSource is(img, "i");
        CHECK(is.snapshot(10).width() == 10 && ColorSource(Qt::green, "g").snapshot(16).pixelColor(1, 1) == QColor(0, 255, 0),
              "Source::snapshot cho anh va mau");
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


    // --- Video xoay (display matrix, vd video doc quay bang dien thoai) ---
    {
        QTemporaryDir dir;
        auto sh = [&](const QString& cmd) { return std::system(cmd.toLocal8Bit().constData()); };
        const QString base = dir.filePath("base.mp4");
        // 320x240: nua trai do, nua phai xanh la
        const int rc = sh("ffmpeg -y -loglevel error -f lavfi -i \"color=c=red:s=320x240:d=1:r=25,"
                          "drawbox=x=160:y=0:w=160:h=240:color=green:t=fill\" -pix_fmt yuv420p -c:v mpeg4 \"" + base + "\"");
        if (rc != 0) {
            std::cout << "skip: khong co ffmpeg de tao video xoay\n";
        } else {
            for (int deg : {90, 180, 270}) {
                const QString rot = dir.filePath(QString("rot%1.mp4").arg(deg));
                const QString ref = dir.filePath(QString("ref%1.png").arg(deg));
                sh(QString("ffmpeg -y -loglevel error -display_rotation %1 -i \"%2\" -c copy \"%3\"").arg(deg).arg(base, rot));
                sh(QString("ffmpeg -y -loglevel error -i \"%1\" -frames:v 1 \"%2\"").arg(rot, ref));   // ffmpeg tu xoay dung
                QImage refImg(ref);
                FFmpegSource v(rot);
                TexRef tex;
                for (int i = 0; i < 300 && !tex.id; ++i) { tex = v.acquire(gl); if (!tex.id) QThread::msleep(10); }
                CHECK(!refImg.isNull() && tex.id && tex.rotation != 0, QString("Video xoay %1: doc duoc co xoay").arg(deg).toStdString());
                if (refImg.isNull() || !tex.id) continue;

                pool.beginFrame();
                auto sc = std::make_shared<Scene>(); sc->size = refImg.size();
                sc->layers.push_back({std::shared_ptr<Source>(&v, [](Source*) {}), {0, 0, 1, 1}, 1.0f, LayerFit::Stretch});
                auto* t = pool.acquire(refImg.width(), refImg.height());
                comp.renderScene(*sc, *t);
                bool all = true;
                for (int qy : {1, 3}) for (int qx : {1, 3}) {
                    const int x = refImg.width() * qx / 4, y = refImg.height() * qy / 4;
                    const QColor e = refImg.pixelColor(x, y);
                    all = all && near(readPx(gl, *t, x, y), e.red(), e.green(), e.blue(), 30);
                }
                CHECK(all, QString("Video xoay %1: 4 diem khop voi hinh ffmpeg tu xoay").arg(deg).toStdString());
            }

            // Video doc (xoay 90) trong Scene ngang 160x90 voi Fit: giua la video, hai ben la nen
            FFmpegSource v(dir.filePath("rot90.mp4"));
            TexRef tex;
            for (int i = 0; i < 300 && !tex.id; ++i) { tex = v.acquire(gl); if (!tex.id) QThread::msleep(10); }
            if (tex.id) {
                pool.beginFrame();
                auto sc = std::make_shared<Scene>(); sc->size = {160, 90}; sc->background = Qt::blue;
                sc->layers.push_back({std::shared_ptr<Source>(&v, [](Source*) {}), {0, 0, 1, 1}, 1.0f, LayerFit::Fit});
                auto* t = pool.acquire(160, 90);
                comp.renderScene(*sc, *t);
                CHECK(near(readPx(gl, *t, 5, 45), 0, 0, 255) && near(readPx(gl, *t, 154, 45), 0, 0, 255) &&
                          !near(readPx(gl, *t, 80, 45), 0, 0, 255, 60),
                      "Video doc Fit trong Scene ngang: vien xanh hai ben, video o giua (khong bi ep ngang)");
            }
        }
    }

    pool.clear();
    comp.shutdown();
    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
