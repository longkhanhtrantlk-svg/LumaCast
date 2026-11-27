// Test SRC-08 (can GL): lop phu chu / chu chay / dem nguoc / logo — trang thai theo thoi gian, dung chu tieng Viet, ghep len Program
// (doc lai diem anh voi `now` tu dat), luu/mo project, va giao dien + engine that (Hoan tac, Khoa van hanh).
#include "InspectorPanel.h"
#include "MainWindow.h"
#include "DragList.h"
#include "OverlayPanel.h"
#include "core/Channel.h"
#include "core/Project.h"
#include "media/StaticSources.h"
#include "render/Compositor.h"
#include "render/OverlayRaster.h"
#include "drag.h"
#include <QApplication>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QOffscreenSurface>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QUndoStack>
#include <iostream>

using namespace lc;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << msg << "  (" #cond ")\n"; } else std::cout << "ok:   " << msg << "\n"; } while (0)

static void pump(int ms) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { QApplication::processEvents(); QThread::msleep(5); }
}
template <class F> static bool waitFor(F cond, int ms) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { pump(10); if (cond()) return true; }
    return cond();
}
static bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }
static bool nearC(QColor c, int r, int g, int b, int tol = 4) {
    return std::abs(c.red() - r) <= tol && std::abs(c.green() - g) <= tol && std::abs(c.blue() - b) <= tol;
}
static bool isWhite(QRgb p) { return qRed(p) > 200 && qGreen(p) > 200 && qBlue(p) > 200; }
static int countWhite(const QImage& img, const QRect& r) {
    int n = 0;
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x) n += isWhite(img.pixel(x, y));
    return n;
}
static int leftmostWhite(const QImage& img, const QRect& r) {
    for (int x = r.left(); x <= r.right(); ++x)
        for (int y = r.top(); y <= r.bottom(); ++y)
            if (isWhite(img.pixel(x, y))) return x;
    return -1;
}

// ---------------- trang thai theo thoi gian ----------------
static void testState() {
    CHECK(formatCountdown(65, false) == "01:05" && formatCountdown(3725, true) == "1:02:05" && formatCountdown(-3, false) == "00:00" &&
              formatCountdown(6000, false) == "100:00", "Dinh dang dong ho mm:ss va h:mm:ss");

    OverlayStack s;
    OverlayDef t = defaultOverlay(OverlayType::Title);
    t.id = 1; t.fade = 0.5f;
    OverlayDef c = defaultOverlay(OverlayType::Countdown);
    c.id = 2; c.seconds = 10; c.text = "Còn";
    s.setDefs({t, c});
    OverlayFrame f;
    CHECK(s.nextId() == 3 && !s.isOn(1) && s.frame(1, 5.0, f) && f.level == 0.0f, "Them moi: trang thai TAT, muc 0");

    s.setOn(1, true, 10.0);
    s.frame(1, 10.25, f);
    CHECK(f.on && near(f.level, 0.5, 1e-5), "Hien mo dan: nua thoi gian = nua muc");
    s.frame(1, 11.0, f);
    CHECK(near(f.level, 1.0, 1e-6) && near(f.onSeconds, 1.0, 1e-9), "Het mo dan = hien han; dem thoi gian da hien");
    s.setOn(1, false, 20.0);
    s.frame(1, 20.125, f);
    CHECK(!f.on && near(f.level, 0.75, 1e-5), "An mo dan");
    s.setOn(1, true, 20.125);
    s.frame(1, 20.125, f);
    CHECK(near(f.level, 0.75, 1e-5), "Bat lai giua chung: tiep tu muc dang co, khong nhay");

    OverlayDef cut = t;
    cut.fade = 0;
    s.setDefs({cut, c});
    s.setOn(1, false, 30.0);
    s.frame(1, 30.0, f);
    CHECK(f.level == 0.0f, "Mo dan = 0: cat ngay");

    s.frame(2, 100.0, f);
    CHECK(f.secondsLeft == 10 && !f.running && overlayDisplayText(f.def, f.secondsLeft) == "Còn 00:10", "Dem nguoc: chua chay = thoi luong day du");
    s.countdownStart(2, 100.0);
    s.frame(2, 102.5, f);
    CHECK(f.running && f.secondsLeft == 8, "Dem nguoc: con 7,5 s hien 00:08 (lam tron len)");
    s.countdownPause(2, 103.0);
    s.frame(2, 200.0, f);
    CHECK(!f.running && f.secondsLeft == 7, "Tam dung: giu nguyen");
    s.countdownStart(2, 200.0);
    s.frame(2, 202.0, f);
    CHECK(f.secondsLeft == 5, "Chay tiep tu cho tam dung");
    s.frame(2, 400.0, f);
    CHECK(!f.running && f.secondsLeft == 0, "Het gio: dung o 00:00");
    s.countdownStart(2, 401.0);
    s.frame(2, 402.0, f);
    CHECK(!f.running && f.secondsLeft == 0, "Da het gio: Bat dau khong lam gi cho den khi Dat lai");
    s.countdownReset(2);
    s.frame(2, 403.0, f);
    CHECK(f.secondsLeft == 10 && !f.running, "Dat lai");

    s.countdownStart(2, 500.0);
    c.seconds = 60;
    s.setDefs({cut, c});
    s.frame(2, 505.0, f);
    CHECK(f.secondsLeft == 60 && !f.running, "Doi thoi luong: dong ho ve dau");

    s.setOn(1, true, 600.0);
    OverlayDef edited = cut;
    edited.text = "khác";
    s.setDefs({edited, c});
    CHECK(s.isOn(1), "Sua noi dung: van dang hien");
    s.setDefs({c});
    s.setDefs({edited, c});
    CHECK(!s.isOn(1), "Xoa roi khoi phuc (Hoan tac): KHONG tu len hinh lai");
}

// ---------------- dung chu ----------------
static OverlayBitmap rasterText(const QString& text, OverlayType type = OverlayType::Title, QSize box = {600, 160}, double outline = 0) {
    OverlayDef d = defaultOverlay(type);
    d.text = text;
    d.fontSize = 64;
    d.bold = false;
    d.font = "Arial";
    d.outlineWidth = float(outline);
    d.outlineColor = Qt::red;
    d.rect = QRectF(0, 0, box.width() / 1920.0, box.height() / 1080.0);
    return rasterizeOverlay(overlayRasterKey(d, 0, {1920, 1080}, {1920, 1080}));
}
static QRect inkRect(const QImage& img) {
    int l = img.width(), t = img.height(), r = -1, b = -1;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (img.pixelColor(x, y).alpha() > 60) { l = std::min(l, x); r = std::max(r, x); t = std::min(t, y); b = std::max(b, y); }
    return r < 0 ? QRect() : QRect(QPoint(l, t), QPoint(r, b));
}
static void testRaster() {
    const OverlayBitmap a = rasterText("A"), ac = rasterText(QStringLiteral("Ấ")), acd = rasterText(QStringLiteral("Ậ"));
    CHECK(a.tiles.size() == 1 && a.size == QSize(600, 160), "Tieu de: anh dung bang hop");
    const QRect ra = inkRect(a.tiles[0]), rac = inkRect(ac.tiles[0]), racd = inkRect(acd.tiles[0]);
    CHECK(!ra.isEmpty() && rac.height() > ra.height() + 8, "Tieng Viet: dau chong (Ấ) cao hon chu A tran");
    CHECK(racd.height() > rac.height() && racd.top() > 0 && racd.bottom() < 159, "Tieng Viet: ca dau tren lan dau nang (Ậ), khong bi cat");
    const QString nfc = QStringLiteral("Tiếng Việt: đường phố Huế").normalized(QString::NormalizationForm_C);
    const OverlayBitmap c1 = rasterText(nfc, OverlayType::Title, {1200, 160});
    const OverlayBitmap c2 = rasterText(nfc.normalized(QString::NormalizationForm_D), OverlayType::Title, {1200, 160});
    CHECK(!inkRect(c1.tiles[0]).isEmpty() && c1.tiles[0] == c2.tiles[0], "Chu go dung san va to hop (NFC/NFD) cho cung mot hinh");
    CHECK(rasterText(QStringLiteral("Ệ")).tiles[0] != rasterText(QStringLiteral("Ọ")).tiles[0], "Cac chu co dau khac nhau cho hinh khac nhau (khong phai o vuong)");

    const OverlayBitmap o = rasterText("O", OverlayType::Title, {200, 160}, 6);
    int red = 0, white = 0;
    for (int y = 0; y < 160; ++y)
        for (int x = 0; x < 200; ++x) {
            const QColor c = o.tiles[0].pixelColor(x, y);
            if (c.alpha() > 250) { red += c.red() > 200 && c.green() < 60; white += c.green() > 200; }
        }
    CHECK(red > 100 && white > 100, "Vien chu: co ca mau vien va mau chu");

    const OverlayBitmap wrap = rasterText("mot hai ba bon nam sau", OverlayType::Title, {300, 600});
    const QRect wr = inkRect(wrap.tiles[0]);                 // mot dong chi cao ~60 px; nhieu dong va khong tran ngang
    CHECK(wr.height() > 140 && wr.left() > 0 && wr.right() < 299, "Tieu de dai tu xuong dong trong hop");

    // Hop vua khit: dau chong khong duoc bi cat; hop qua thap: bao bi cat
    const OverlayBitmap tight = rasterText(QStringLiteral("ỄẬỔ"), OverlayType::Title, {600, 100});
    const QRect tr = inkRect(tight.tiles[0]);
    CHECK(!tight.clipped && tr.top() >= 0 && tr.bottom() <= 99 && tr.height() >= inkRect(rasterText(QStringLiteral("ỄẬỔ")).tiles[0]).height() - 1,
          "Hop sat chu: dau tieng Viet con nguyen");
    CHECK(rasterText(QStringLiteral("Lễ Tổng Kết Nguyễn Thị Ánh Tuyết"), OverlayType::Title, {400, 90}).clipped && !c1.clipped,
          "Chu khong vua hop: danh dau bi cat");
    const QString shots = qEnvironmentVariable("LUMACAST_TEST_SHOTS");
    if (!shots.isEmpty()) {
        const OverlayBitmap vn = rasterText(QStringLiteral("LỄ TỔNG KẾT — Nguyễn Thị Ánh Tuyết\nắằẳẵặ ấầẩẫậ ếềểễệ ốồổỗộ ớờởỡợ ứừửữự đĐ"), OverlayType::Title, {1500, 220}, 3);
        QImage sheet(vn.size, QImage::Format_RGB32);
        sheet.fill(QColor(0, 40, 120));
        QPainter p(&sheet);
        p.drawImage(0, 0, vn.tiles[0]);
        p.end();
        sheet.save(shots + "/vietnamese.png");
    }

    const OverlayBitmap tk = rasterText(QString("Chữ chạy rất dài ").repeated(20), OverlayType::Ticker, {600, 100});
    int w = 0;
    bool tilesOk = true;
    for (const QImage& t : tk.tiles) { w += t.width(); tilesOk = tilesOk && t.width() <= OverlayBitmap::kTileWidth && t.height() == 100; }
    CHECK(tk.tiles.size() > 1 && w == tk.size.width() && tilesOk, "Chu chay dai: cat thanh nhieu manh <= 2048 px");
    CHECK(rasterText("   ").size.isEmpty() && rasterText("", OverlayType::Ticker).size.isEmpty(), "Chu rong: khong co anh");

    CHECK(overlayContentX(OverlayType::Ticker, OverlayAlign::Center, 600, 400, 0, 100) == 600 &&
              overlayContentX(OverlayType::Ticker, OverlayAlign::Center, 600, 400, 2.5, 100) == 350 &&
              overlayContentX(OverlayType::Ticker, OverlayAlign::Center, 600, 400, 10.5, 100) == 550,
          "Chu chay: vi tri theo thoi gian, vao tu mep phai, ra het roi lap");
}

// ---------------- ghep tren GPU ----------------
static void testRender(const QString& tmp) {
    QOffscreenSurface surf;
    surf.setFormat(QSurfaceFormat::defaultFormat());
    surf.create();
    QOpenGLContext ctx;
    ctx.setFormat(QSurfaceFormat::defaultFormat());
    if (!ctx.create() || !ctx.makeCurrent(&surf)) { CHECK(false, "Khong co OpenGL 3.3"); return; }
    Compositor comp;
    if (!comp.init()) { CHECK(false, "Shader loi"); return; }
    {
        GL& gl = comp.gl();
        TargetPool pool(gl);
        auto scene = [](QColor c, QSize sz) {
            auto s = std::make_shared<Scene>();
            s->size = sz;
            s->layers.push_back({std::make_shared<ColorSource>(c, "c"), {0, 0, 1, 1}, 1.0f, LayerFit::Stretch});
            return s;
        };
        Channel ch;
        ch.size = {640, 360};
        ch.scenes = {scene(Qt::red, ch.size), scene(Qt::green, ch.size)};
        ch.program = 0; ch.preview = 1;

        auto grab = [&](RenderTarget* t) {
            QImage img(t->w, t->h, QImage::Format_RGBA8888);
            gl.glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
            gl.glReadPixels(0, 0, t->w, t->h, GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
            return img.mirrored(false, true);
        };
        // ve, cho thread dung chu xong, ve lai cung thoi diem
        auto render = [&](double now) {
            pool.beginFrame();
            ch.render(comp, pool, now);
            comp.overlayCache().waitIdle(10000);
            pool.beginFrame();
            return grab(ch.render(comp, pool, now));
        };

        OverlayDef title = defaultOverlay(OverlayType::Title);
        title.id = 1;
        title.rect = QRectF(0.1, 0.6, 0.8, 0.3);              // px: x 64..575, y 216..323
        title.fontSize = 48;
        title.background = QColor(0, 0, 255);
        title.text = QStringLiteral("Xin chào Việt Nam");
        title.fade = 1.0f;
        const QRect box(64, 216, 512, 108);
        ch.overlays.setDefs({title});

        QImage img = render(1.0);
        CHECK(nearC(img.pixelColor(70, 220), 255, 0, 0) && countWhite(img, box) == 0, "Lop phu chua bat: khong co gi tren Program");
        ch.overlays.setOn(1, true, 10.0);
        img = render(10.5);
        CHECK(nearC(img.pixelColor(70, 220), 128, 0, 128, 6), "Dang mo dan 50%: nen tron nua voi Scene");
        img = render(12.0);
        CHECK(nearC(img.pixelColor(70, 220), 0, 0, 255) && countWhite(img, box) > 300, "Hien han: nen + chu trong hop");
        CHECK(countWhite(img, QRect(0, 0, 640, 215)) == 0 && nearC(img.pixelColor(30, 220), 255, 0, 0) && nearC(img.pixelColor(70, 340), 255, 0, 0),
              "Ngoai hop: Scene nguyen ven");

        const uint64_t before = comp.overlayCache().rasterCount();
        for (int i = 0; i < 100; ++i) { pool.beginFrame(); ch.render(comp, pool, 12.0 + i / 60.0); }
        comp.overlayCache().waitIdle(10000);
        CHECK(comp.overlayCache().rasterCount() == before, "100 khung noi dung khong doi: khong dung lai anh chu");
        title.rect.moveTop(0.55);
        title.opacity = 0.9f;
        title.background = QColor(0, 0, 200);
        ch.overlays.setDefs({title});
        render(14.0);
        CHECK(comp.overlayCache().rasterCount() == before, "Doi vi tri / do dam / mau nen: khong phai dung lai anh");
        title.text = QStringLiteral("Nội dung khác");
        title.rect.moveTop(0.6);
        title.opacity = 1.0f;
        title.background = QColor(0, 0, 255);
        ch.overlays.setDefs({title});
        render(14.5);
        CHECK(comp.overlayCache().rasterCount() == before + 1, "Doi noi dung: dung lai dung mot lan");

        ch.cut(15.0);
        img = render(15.0);
        CHECK(nearC(img.pixelColor(30, 30), 0, 255, 0) && nearC(img.pixelColor(70, 220), 0, 0, 255), "Doi Scene: lop phu giu nguyen");
        pool.beginFrame();
        const QImage pv = grab(ch.renderPreview(comp, pool));
        CHECK(nearC(pv.pixelColor(70, 220), 255, 0, 0), "Preview khong co lop phu");

        ch.startTransition(TransitionType::Fade, 2.0, 16.0);
        img = render(17.0);
        CHECK(ch.inTransition() && nearC(img.pixelColor(70, 220), 0, 0, 255), "Giua transition: lop phu van nam tren cung");
        render(19.0);

        ch.toggleFtb(0.5);
        render(20.0);
        img = render(21.0);
        CHECK(nearC(img.pixelColor(70, 220), 0, 0, 0) && countWhite(img, box) == 0, "FTB che ca lop phu");
        ch.toggleFtb(0.5);
        render(22.0);

        // Scene co do phan giai khac canvas kenh: hinh hoc theo ti le
        ch.scenes[size_t(ch.program.load())]->size = {320, 180};
        img = render(25.0);
        CHECK(img.size() == QSize(320, 180) && nearC(img.pixelColor(35, 110), 0, 0, 255) && countWhite(img, QRect(32, 108, 256, 54)) > 50,
              "Scene 320x180 tren kenh 640x360: lop phu thu theo cung ti le");
        ch.scenes[size_t(ch.program.load())]->size = {640, 360};

        // ---- chu chay ----
        OverlayDef tk = defaultOverlay(OverlayType::Ticker);
        tk.id = 2;
        tk.rect = QRectF(0.25, 0.0, 0.5, 0.2);                // px: x 160..479, y 0..71
        tk.fontSize = 40;
        tk.speed = 80;
        tk.fade = 0;
        tk.background = QColor(0, 0, 0, 0);
        tk.text = QStringLiteral("HẾT GIỜ");
        ch.overlays.setDefs({tk});
        render(30.0);
        ch.overlays.setOn(2, true, 31.0);
        const QRect band(160, 0, 320, 72);
        img = render(31.0);
        CHECK(countWhite(img, QRect(0, 0, 640, 72)) == 0, "Chu chay vua bat: con nam ngoai mep phai cua hop");
        const int x1 = leftmostWhite(render(32.5), band), x2 = leftmostWhite(render(33.5), band);
        CHECK(x1 > 0 && x2 > 0 && near(x1 - x2, 80, 2), "Chu chay: 1 giay dich dung 80 px (theo thoi gian)");
        for (int i = 0; i < 30; ++i) { pool.beginFrame(); ch.render(comp, pool, 33.5); }
        CHECK(leftmostWhite(render(33.5), band) == x2, "Chu chay: khong dich theo so khung da ve");
        bool inside = true;
        for (double t = 31.5; t < 38; t += 0.5) {
            img = render(t);
            inside = inside && countWhite(img, QRect(0, 0, 160, 72)) == 0 && countWhite(img, QRect(480, 0, 160, 72)) == 0;
        }
        CHECK(inside, "Chu chay bi cat gon trong hop o moi thoi diem");

        // ---- dem nguoc: dung lai anh moi giay mot lan ----
        OverlayDef cd = defaultOverlay(OverlayType::Countdown);
        cd.id = 3;
        cd.seconds = 90;
        cd.fade = 0;
        ch.overlays.setDefs({cd});
        ch.overlays.setOn(3, true, 40.0);
        render(40.0);
        const uint64_t r0 = comp.overlayCache().rasterCount();
        ch.overlays.countdownStart(3, 40.0);
        for (int i = 1; i <= 180; ++i) {                        // 3 giay o 60 fps
            pool.beginFrame();
            ch.render(comp, pool, 40.0 + i / 60.0);
            if (i % 60 == 1) comp.overlayCache().waitIdle(10000);
        }
        comp.overlayCache().waitIdle(10000);
        const uint64_t made = comp.overlayCache().rasterCount() - r0;
        CHECK(made >= 3 && made <= 4, "Dem nguoc 3 s: chi dung anh khi so giay doi");

        // ---- logo co kenh trong suot ----
        QImage logo(100, 100, QImage::Format_RGBA8888);
        logo.fill(Qt::transparent);
        {
            QPainter p(&logo);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.fillRect(33, 0, 33, 100, QColor(0, 255, 0, 128));
            p.fillRect(66, 0, 34, 100, QColor(0, 255, 0, 255));
        }
        const QString png = tmp + "/logo.png";
        logo.save(png);
        ch.scenes = {scene(Qt::red, ch.size)};
        ch.program = 0; ch.preview = -1;
        OverlayDef lg = defaultOverlay(OverlayType::Logo);
        lg.id = 4;
        lg.rect = QRectF(0, 0, 100.0 / 640, 100.0 / 360);
        lg.image = png;
        lg.fade = 0;
        ch.overlays.setDefs({lg});
        ch.overlays.setOn(4, true, 50.0);
        img = render(50.0);
        CHECK(nearC(img.pixelColor(10, 50), 255, 0, 0), "Logo: vung trong suot thay Scene phia duoi");
        CHECK(nearC(img.pixelColor(50, 50), 127, 128, 0, 6), "Logo: nua trong suot tron dung");
        CHECK(nearC(img.pixelColor(85, 50), 0, 255, 0), "Logo: vung dac");
        lg.opacity = 0.5f;
        ch.overlays.setDefs({lg});
        img = render(50.5);
        CHECK(nearC(img.pixelColor(85, 50), 127, 128, 0, 6), "Do dam lop phu ap len logo");
        lg.image = tmp + "/khong-co.png";
        ch.overlays.setDefs({lg});
        img = render(51.0);
        CHECK(nearC(img.pixelColor(85, 50), 255, 0, 0), "Thieu file logo: khong ve gi, khong loi");
        CHECK(gl.glGetError() == GL_NO_ERROR, "Khong co loi GL");
        pool.clear();
    }
    comp.shutdown();
    ctx.doneCurrent();
}

// ---------------- project ----------------
static void testProject(const QString& tmp) {
    QImage logo(8, 8, QImage::Format_RGBA8888);
    logo.fill(Qt::green);
    QDir(tmp).mkpath("proj/media");
    logo.save(tmp + "/proj/media/logo.png");

    Project p;
    SourceDef sd; sd.type = "color"; sd.color = Qt::red; sd.label = "do";
    auto src = p.addSource(sd);
    auto s = std::make_shared<Scene>();
    s->layers.push_back({src, {0, 0, 1, 1}});
    p.scenes = {s};
    auto ch = std::make_shared<Channel>();
    ch->scenes = p.scenes; ch->program = 0;
    p.channels = {ch};
    p.outputs = {OutputConfig{}};

    const QString path = tmp + "/proj/a.json";
    CHECK(saveProject(p, path), "Luu project khong co lop phu");
    QFile f0(path); f0.open(QIODevice::ReadOnly);
    const QByteArray plain = f0.readAll();
    f0.close();
    CHECK(!plain.contains("overlays"), "Khong dung lop phu: file khong them truong nao (project cu khong doi)");
    Project old;
    CHECK(loadProject(old, path) && old.channels[0]->overlays.empty(), "Mo project cu (khong co 'overlays')");

    OverlayDef t = defaultOverlay(OverlayType::Title);
    t.id = 1; t.text = QStringLiteral("Chương trình “Đêm Huế”\ndòng hai"); t.font = "Arial"; t.fontSize = 88; t.italic = true;
    t.color = QColor(10, 20, 30); t.outlineColor = QColor(200, 0, 0, 128); t.outlineWidth = 3.5f; t.background = QColor(1, 2, 3, 4);
    t.align = OverlayAlign::Left; t.rect = QRectF(0.125, 0.25, 0.5, 0.25); t.opacity = 0.75f; t.fade = 0.0f;
    OverlayDef tk = defaultOverlay(OverlayType::Ticker); tk.id = 2; tk.speed = 220;
    OverlayDef cd = defaultOverlay(OverlayType::Countdown); cd.id = 3; cd.seconds = 5400; cd.text = "Còn";
    OverlayDef lg = defaultOverlay(OverlayType::Logo); lg.id = 4; lg.image = QDir::cleanPath(tmp + "/proj/media/logo.png");
    ch->overlays.setDefs({t, tk, cd, lg});
    ch->overlays.setOn(1, true, 1.0);
    ch->overlays.countdownStart(3, 1.0);
    CHECK(saveProject(p, path), "Luu project co lop phu");
    QFile f1(path); f1.open(QIODevice::ReadOnly);
    const QJsonObject root = QJsonDocument::fromJson(f1.readAll()).object();
    f1.close();
    const QJsonArray ov = root["channels"].toArray()[0].toObject()["overlays"].toArray();
    CHECK(root["version"].toInt() == 1 && ov.size() == 4 && ov[3].toObject()["image"].toString() == "media/logo.png",
          "Khong tang version; duong dan logo tuong doi so voi file project");

    Project q;
    QString err;
    CHECK(loadProject(q, path, &err), "Mo lai");
    const std::vector<OverlayDef> d = q.channels[0]->overlays.defs();
    // ten/rect... cua cac loai khong dung font duoc giu mac dinh nen so sanh nguyen ven duoc
    CHECK(d.size() == 4 && d[0] == t && d[1] == tk && d[2] == cd && d[3] == lg, "Moi thuoc tinh giu nguyen (ca chu tieng Viet, thu tu)");
    OverlayFrame fr;
    q.channels[0]->overlays.frame(1, 100.0, fr);
    CHECK(fr.on && fr.level == 1.0f, "Lop dang hien luc luu: mo ra van hien, khong mo dan");
    q.channels[0]->overlays.frame(3, 100.0, fr);
    CHECK(!fr.on && !fr.running && fr.secondsLeft == 5400, "Dong ho dem nguoc mo ra o trang thai dung, day du");

    QFile::remove(tmp + "/proj/media/logo.png");
    Project m;
    CHECK(loadProject(m, path) && m.channels[0]->overlays.defs().size() == 4, "Mat file logo: project van mo duoc");

    QJsonObject bad = root;
    QJsonObject c0 = bad["channels"].toArray()[0].toObject();
    c0["overlays"] = QJsonArray{
        QJsonObject{{"type", "title"}, {"id", 7}, {"w", -5}, {"opacity", 99}, {"fontSize", 1e9}, {"color", "zzz"}},
        QJsonObject{{"type", "title"}, {"id", 7}},
        QJsonObject{{"type", "hologram"}},
        QJsonObject{{"type", "countdown"}, {"seconds", -1}},
        42};
    bad["channels"] = QJsonArray{c0};
    QFile fb(tmp + "/proj/bad.json"); fb.open(QIODevice::WriteOnly); fb.write(QJsonDocument(bad).toJson()); fb.close();
    Project b;
    CHECK(loadProject(b, tmp + "/proj/bad.json"), "Du lieu lop phu xau: van mo duoc");
    const auto bd = b.channels[0]->overlays.defs();
    CHECK(bd.size() == 3 && bd[0].rect.width() > 0 && bd[0].opacity == 1.0f && bd[0].fontSize == 2000 && bd[0].color == QColor(Qt::white) &&
              bd[1].id != bd[0].id && bd[2].seconds == 1 && bd[2].id != bd[0].id && bd[2].id != bd[1].id,
          "Gia tri bi kep, loai la bi bo, id trung duoc danh lai");
}

// ---------------- giao dien + engine ----------------
static void testUi(const QString& tmp) {
    Project p;
    auto scene = [&](const char* name, QColor c) {
        SourceDef sd; sd.type = "color"; sd.color = c; sd.label = name;
        auto src = p.addSource(sd);
        auto s = std::make_shared<Scene>();
        s->name = name; s->size = {320, 180};
        s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        p.scenes.push_back(s);
    };
    scene("do", Qt::red);
    scene("xanh", Qt::green);
    for (const char* n : {"K1", "K2"}) {
        auto ch = std::make_shared<Channel>();
        ch->name = n; ch->size = {320, 180}; ch->scenes = p.scenes; ch->program = 0; ch->preview = 1;
        p.channels.push_back(ch);
    }
    OutputConfig o; o.name = "O1"; o.size = {160, 90};
    p.outputs = {o};
    p.filePath = tmp + "/ui.json";

    RenderEngine engine(p);
    engine.startEngine();
    MainWindow w(p, engine, {});
    w.setAskOnClose(false);
    w.show();
    OverlayPanel* ov = w.overlayPanel();
    QUndoStack* undo = w.undoStack();
    auto& stack = p.channels[0]->overlays;
    auto btn = [&](const char* n) { return ov->findChild<QPushButton*>(n); };
    auto list = ov->findChild<QListWidget*>("ovList");
    auto monitorAt = [&](int ch, int xPct, int yPct) {
        const QImage img = engine.monitor(ch).program;
        return img.isNull() ? QColor(1, 2, 3) : img.pixelColor(img.width() * xPct / 100, img.height() * yPct / 100);
    };
    CHECK(waitFor([&] { return nearC(monitorAt(0, 50, 50), 255, 0, 0); }, 5000), "Engine chay, Program = Scene do");

    // nut / dong ho cua tung dong trong danh sach
    auto part = [&](int id, const char* name) { QWidget* r = ov->row(id); return r ? r->findChild<QPushButton*>(name) : nullptr; };
    auto clockOf = [&](int id) { QWidget* r = ov->row(id); return r ? r->findChild<QLabel*>("ovClock") : nullptr; };
    auto isRed = [&](int id) { QWidget* r = ov->row(id); return r && r->property("on").toBool(); };
    btn("ovAddTitle")->click();
    const int id = ov->currentId();
    CHECK(id > 0 && stack.defs().size() == 1 && !stack.isOn(id) && list->count() == 1, "Them tieu de: vao danh sach, trang thai TAT");
    ov->editCurrent([](OverlayDef& d) { d.rect = QRectF(0, 0, 1, 1); d.background = QColor(0, 0, 255); d.fade = 0; }, "dat phu kin");
    pump(300);
    CHECK(nearC(monitorAt(0, 5, 5), 255, 0, 0), "Them/sua lop phu khong dua gi len Program");

    CHECK(!btn("ovToggle") && !btn("ovUp") && !btn("ovDown") && !btn("ovDelete"), "khong con nut Len / Xuong / Xoa / HIEN to");
    CHECK(part(id, "ovEye") && part(id, "ovDel") && !isRed(id), "moi dong co mat hien/an va thung rac; chua hien thi khong do");
    part(id, "ovEye")->click();
    CHECK(stack.isOn(id) && isRed(id) && ov->findChild<QLabel*>("ovLiveWarn")->isVisibleTo(ov), "mat hien lop phu: dong nen do, canh bao sua la len man");
    CHECK(waitFor([&] { return nearC(monitorAt(0, 5, 5), 0, 0, 255); }, 3000), "Program co lop phu");
    CHECK(nearC(monitorAt(1, 5, 5), 255, 0, 0), "Kenh 2 khong bi anh huong");
    const int steps = undo->count();
    part(id, "ovEye")->click();
    CHECK(!stack.isOn(id) && undo->count() == steps && !isRed(id), "bam mat lan nua = an; hien/an khong vao ngan xep Hoan tac");

    auto* text = ov->findChild<QPlainTextEdit*>("ovText");
    const int base = undo->index();
    for (const QString& s : {QStringLiteral("Đ"), QStringLiteral("Đà"), QStringLiteral("Đà Nẵng")}) text->setPlainText(s);
    CHECK(stack.defs()[0].text == QStringLiteral("Đà Nẵng") && undo->index() == base + 1, "Go chu lien tuc = mot buoc Hoan tac");
    undo->undo();
    CHECK(stack.defs()[0].text == "Tiêu đề" && text->toPlainText() == "Tiêu đề", "Hoan tac sua chu (ca o nhap)");
    undo->redo();
    ov->findChild<QDoubleSpinBox*>("ovX")->setValue(0.25);
    CHECK(near(stack.defs()[0].rect.x(), 0.25, 1e-9), "Sua vi tri qua o nhap");
    undo->undo();

    while (undo->canUndo()) undo->undo();
    CHECK(stack.empty() && list->count() == 0, "Hoan tac het: lop phu bien mat");
    undo->redo();
    CHECK(stack.defs().size() == 1 && !stack.isOn(id), "Lam lai buoc Them: o trang thai TAT");
    while (undo->canRedo()) undo->redo();

    ov->setOn(id, true);
    ov->selectId(id);
    part(id, "ovDel")->click();
    pump(50);
    CHECK(stack.empty(), "Xoa lop phu dang hien (thung rac)");
    undo->undo();
    CHECK(stack.defs().size() == 1 && !stack.isOn(id), "Hoan tac xoa: khong tu len hinh lai");

    btn("ovAddCountdown")->click();
    const int cd = ov->currentId();
    CHECK(cd != id && clockOf(cd) && clockOf(cd)->text() == "05:00" && stack.defs().back().id == cd && list->item(0)->data(Qt::UserRole).toInt() == cd,
          "Them dem nguoc 05:00, nam tren cung");
    for (QDockWidget* d : w.findChildren<QDockWidget*>("dockOverlay")) { d->show(); d->raise(); }
    pump(300);
    testdrag::dragRow(list, 0, 2);
    CHECK(stack.defs().front().id == cd, "keo dong xuong duoi: doi thu tu");
    undo->undo();
    CHECK(stack.defs().back().id == cd, "hoan tac doi thu tu");
    // keo ra ngoai danh sach = huy, KHONG xoa
    testdrag::drag(list->viewport(), list->visualItemRect(list->item(0)).center(), QPoint(-400, -300));
    CHECK(stack.defs().size() == 2 && stack.defs().back().id == cd, "keo ra ngoai khong xoa, khong doi thu tu");
    // phim Delete xoa muc dang chon, hoan tac duoc
    ov->selectId(cd);
    QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QApplication::sendEvent(list, &del);
    pump(50);
    CHECK(stack.defs().size() == 1 && stack.defs()[0].id == id, "phim Delete xoa lop phu dang chon");
    undo->undo();
    CHECK(stack.defs().size() == 2 && !stack.isOn(cd), "Hoan tac phim Delete");
    ov->selectId(cd);
    QKeyEvent delElsewhere(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QApplication::sendEvent(ov->findChild<QWidget*>("ovBold"), &delElsewhere);
    pump(50);
    CHECK(stack.defs().size() == 2, "phim Delete o o tick / cho khac trong dock KHONG xoa lop phu");
    ov->selectId(cd);

    // ---- phim tat: Enter/Space trong o nhap cua dock khong duoc thanh CUT/TAKE ----
    for (const char* name : {"ovName", "ovX", "ovFontSize", "ovFont", "ovText"}) {
        QKeyEvent ev(QEvent::ShortcutOverride, Qt::Key_Return, Qt::NoModifier);
        ev.ignore();
        QApplication::sendEvent(ov->findChild<QWidget*>(name), &ev);
        CHECK(ev.isAccepted(), std::string("Enter trong ") + name + " duoc o nhap giu lai (khong CUT)");
    }

    // ---- khoa van hanh ----
    w.setLocked(true);
    const auto lockedDefs = stack.defs();
    CHECK(!btn("ovAddTitle")->isEnabled() && !part(cd, "ovDel")->isEnabled() && !ov->findChild<QWidget*>("ovProps")->isEnabled() &&
              part(cd, "ovEye")->isEnabled() && part(cd, "ovPlay")->isEnabled() && list->isEnabled() && !ov->list()->reorderEnabled(),
          "Khoa: vo hieu them/xoa/keo/sua; giu hien/an va dong ho");
    testdrag::dragRow(list, 0, 2);
    QApplication::sendEvent(list, &del);
    CHECK(stack.defs() == lockedDefs, "Khoa: keo va phim Delete bi chan");
    CHECK(ov->addOverlay(OverlayType::Ticker) == -1, "Khoa: khong them duoc ke ca goi lenh");
    ov->editCurrent([](OverlayDef& d) { d.seconds = 5; }, "x");
    ov->removeCurrent();
    ov->moveCurrent(-1);
    CHECK(stack.defs() == lockedDefs, "Khoa: lenh sua bi chan");
    part(cd, "ovEye")->click();
    CHECK(stack.isOn(cd), "Khoa: van hien duoc lop phu");
    part(cd, "ovPlay")->click();
    CHECK(waitFor([&] { return clockOf(cd)->text() != "05:00"; }, 4000), "Khoa: dong ho chay duoc");
    part(cd, "ovPlay")->click();
    const QString paused = clockOf(cd)->text();
    pump(1200);
    CHECK(clockOf(cd)->text() == paused, "Khoa: tam dung duoc");
    part(cd, "ovReset")->click();
    CHECK(clockOf(cd)->text() == "05:00", "Khoa: dat lai duoc");
    w.setLocked(false);
    CHECK(btn("ovAddTitle")->isEnabled() && ov->findChild<QWidget*>("ovProps")->isEnabled(), "Mo khoa: sua lai duoc");

    ov->selectId(id);
    ov->editCurrent([](OverlayDef& d) { d.text = QStringLiteral("Dòng rất dài ").repeated(30); }, "x");
    CHECK(ov->findChild<QLabel*>("ovWarn")->text().contains("không vừa hộp"), "Chu tran hop: canh bao tren bang");
    undo->undo();

    // ---- logo + canh bao + kenh ----
    QImage logo(8, 8, QImage::Format_RGBA8888);
    logo.fill(Qt::green);
    logo.save(tmp + "/l.png");
    const int lg = ov->addLogo(tmp + "/l.png");
    CHECK(lg > 0 && stack.defs().back().image.endsWith("l.png") && !stack.isOn(lg) && ov->findChild<QLabel*>("ovWarn")->text().isEmpty(),
          "Them logo: tat, co duong dan");
    ov->editCurrent([&](OverlayDef& d) { d.image = tmp + "/mat.png"; }, "x");
    CHECK(ov->findChild<QLabel*>("ovWarn")->text().contains("Không tìm thấy"), "Thieu file logo: canh bao");
    undo->undo();

    ov->setChannel(1);
    CHECK(list->count() == 0, "Kenh 2 co danh sach rieng");
    const int other = ov->addOverlay(OverlayType::Ticker);
    ov->setChannel(0);
    CHECK(other > 0 && p.channels[1]->overlays.defs().size() == 1 && list->count() == 3, "Lop phu theo tung Kenh");
    undo->undo();
    CHECK(p.channels[1]->overlays.empty() && list->count() == 3, "Hoan tac tac dong dung kenh du dang xem kenh khac");

    // Anh chup de kiem tra bang mat (chi khi dat bien moi truong)
    const QString shots = qEnvironmentVariable("LUMACAST_TEST_SHOTS");
    if (!shots.isEmpty()) {
        while (!stack.empty()) { ov->selectId(stack.defs().back().id); ov->removeCurrent(); }
        w.applyLayout("big");
        const int a = ov->addOverlay(OverlayType::Title);
        ov->editCurrent([](OverlayDef& d) { d.text = QStringLiteral("Lễ Tổng Kết: Nguyễn Thị Ánh Tuyết"); d.fontSize = 15; d.outlineWidth = 1; d.rect = QRectF(0.02, 0.62, 0.96, 0.2); }, "x");
        const int b = ov->addOverlay(OverlayType::Ticker);
        ov->editCurrent([](OverlayDef& d) { d.text = QStringLiteral("Kính mời quý đại biểu ổn định chỗ ngồi • Đề nghị tắt chuông điện thoại"); d.fontSize = 13; d.speed = 40; }, "x");
        const int c = ov->addOverlay(OverlayType::Countdown);
        ov->editCurrent([](OverlayDef& d) { d.text = QStringLiteral("Bắt đầu sau"); d.fontSize = 16; d.rect = QRectF(0.2, 0.05, 0.6, 0.18); }, "x");
        for (int i : {a, b, c}) ov->setOn(i, true);
        ov->editCurrent([](OverlayDef& d) { d.alert = true; d.seconds = 9; }, "x");
        ov->toggleClock(c);
        w.resize(1500, 1000);
        pump(3500);
        w.grab().save(shots + "/window.png");
        engine.monitor(0).program.save(shots + "/program.png");
        while (!stack.empty()) { ov->selectId(stack.defs().back().id); ov->removeCurrent(); }
        ov->addOverlay(OverlayType::Title);
        ov->addOverlay(OverlayType::Ticker);
        ov->addLogo(tmp + "/l.png");
    }

    w.saveProject(false);
    CHECK(waitFor([&] {
              Project q;
              return QFile::exists(p.filePath) && loadProject(q, p.filePath) && q.channels[0]->overlays.defs().size() == 3;
          }, 4000), "Luu project kem lop phu");
    w.close();
    engine.stopEngine();
}

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc, argv);
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.filePath("cfg"));

    testState();
    testRaster();
    testRender(dir.path());
    testProject(dir.path());
    testUi(dir.path());

    std::cout << (g_fail ? "THAT BAI: " : "DAT TAT CA, loi: ") << g_fail << "\n";
    return g_fail ? 1 : 0;
}
