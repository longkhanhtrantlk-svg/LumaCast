// Test bu khe video wall (OUT-04): luoi crop, 1:1 tung diem anh, luu/mo, giao dien (Hoan tac, Khoa van hanh), dong lenh.
// Phan giao dien dung Output gia nen khong mo cua so; chi phan dong lenh chay lumacast that (vai cua so nho trong choc lat).
#include "InspectorPanel.h"
#include "media/StaticSources.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QOffscreenSurface>
#include <QProcess>
#include <QSpinBox>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QThread>
#include <QUndoStack>
#include <cmath>
#include <iostream>

using namespace lc;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << msg << "  (" #cond ")\n"; } else std::cout << "ok:   " << msg << "\n"; } while (0)

// Moi diem anh mot mau rieng theo toa do: lech mot diem anh la phat hien duoc.
static QImage coded(QSize s) {
    QImage img(s, QImage::Format_RGBA8888);
    for (int y = 0; y < s.height(); ++y) {
        uchar* p = img.scanLine(y);
        for (int x = 0; x < s.width(); ++x, p += 4) {
            p[0] = uchar(x & 255); p[1] = uchar(y & 255); p[2] = uchar(((x >> 8) & 15) | (((y >> 8) & 15) << 4)); p[3] = 255;
        }
    }
    return img;
}

// Ve tung Output cua wall tu canvas roi so TUNG diem anh voi vi tri (cot*(o+khe), hang*(o+khe)). Tra so diem anh sai.
static long long wrongPixels(Compositor& comp, int cols, int rows, QSize cell, QSize gap) {
    GL& gl = comp.gl();
    TargetPool pool(gl);
    pool.beginFrame();
    const QSize canvas = wallCanvas(cols, rows, cell, gap);
    const QImage src = coded(canvas);
    Scene scene;
    scene.size = canvas;
    scene.layers.push_back({std::make_shared<ImageSource>(src, "c"), {0, 0, 1, 1}, 1.0f, LayerFit::Stretch});
    auto* program = pool.acquire(canvas.width(), canvas.height());
    comp.renderScene(scene, *program);
    auto* win = pool.acquire(cell.width(), cell.height());
    const auto outs = makeWall(0, cols, rows, cell, 0, false, gap);
    long long wrong = 0;
    std::vector<uint8_t> buf(size_t(cell.width()) * size_t(cell.height()) * 4);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            gl.glBindFramebuffer(GL_FRAMEBUFFER, win->fbo);
            comp.drawToCurrent(program->tex, canvas.width(), canvas.height(), outs[size_t(r * cols + c)], cell.width(), cell.height());
            gl.glBindFramebuffer(GL_FRAMEBUFFER, win->fbo);
            gl.glReadPixels(0, 0, cell.width(), cell.height(), GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
            const int ox = c * (cell.width() + gap.width()), oy = r * (cell.height() + gap.height());
            for (int y = 0; y < cell.height(); ++y) {
                const uint8_t* got = buf.data() + size_t(cell.height() - 1 - y) * size_t(cell.width()) * 4;
                const uint8_t* want = src.constScanLine(oy + y) + size_t(ox) * 4;
                for (int x = 0; x < cell.width(); ++x)
                    if (got[4 * x] != want[4 * x] || got[4 * x + 1] != want[4 * x + 1] || got[4 * x + 2] != want[4 * x + 2]) ++wrong;
            }
        }
    pool.clear();
    return wrong;
}

static Project wallProject(int cols, int rows, QSize cell, QSize gap) {
    Project p;
    const QSize canvas = wallCanvas(cols, rows, cell, gap);
    SourceDef t; t.type = "testpattern"; t.size = canvas; t.label = "Test";
    auto src = p.addSource(t);
    for (const char* n : {"A", "B"}) {
        auto s = std::make_shared<Scene>();
        s->name = n; s->size = canvas;
        s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        p.scenes.push_back(s);
    }
    auto own = std::make_shared<Scene>();                       // Scene co do phan giai rieng: khong duoc doi co
    own->name = "rieng"; own->size = {100, 50};
    own->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
    p.scenes.push_back(own);
    auto ch = std::make_shared<Channel>();
    ch->name = "K"; ch->size = canvas; ch->scenes = p.scenes; ch->program = 0; ch->preview = 1;
    p.channels = {ch};
    p.outputs = makeWall(0, cols, rows, cell, 0, false, gap);
    return p;
}

// Output gia: moi khung kiem tra canvas dang render va crop khop nhau (o lay dung cell.width diem anh).
struct Probe : OutputSink {
    OutputConfig cfg;
    int cellW = 0;
    std::atomic<int> bad{0}, canvasW{0};
    const OutputConfig& config() const override { return cfg; }
    OutputConfig& mutableConfig() override { return cfg; }
    void present(QOpenGLContext&, Compositor&, RenderTarget* src) override {
        if (src) {
            canvasW = src->w;
            if (std::abs(cfg.crop.width() * src->w - cellW) > 0.01) ++bad;
        }
        ++presented;
    }
};

static bool waitFrames(const Probe& p, uint64_t n) {
    const uint64_t from = p.presented;
    QElapsedTimer t; t.start();
    while (p.presented < from + n && t.elapsed() < 5000) QThread::msleep(5);
    return p.presented >= from + n;
}

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc, argv);
    QTemporaryDir dir;

    // ---------------- luoi crop ----------------
    {
        const QSize cell(1920, 1080), gap(40, 24);
        CHECK(wallCanvas(4, 1, cell, QSize(40, 0)) == QSize(7800, 1080) && wallCanvas(2, 3, cell, gap) == QSize(3880, 3288),
              "Canvas = o + khe: 4x1 khe 40 -> 7800x1080; 2x3 khe 40x24 -> 3880x3288");
        const auto w = makeWall(0, 4, 1, cell, 0, false, QSize(40, 0));
        CHECK(std::abs(w[2].crop.x() * 7800 - 3920) < 1e-6 && std::abs(w[2].crop.width() * 7800 - 1920) < 1e-6 && w[2].crop.height() == 1,
              "makeWall co khe: o thu 3 bat dau o diem anh 3920, rong 1920");
        CHECK(makeWall(0, 4, 1, cell)[2].crop == QRectF(0.5, 0, 0.25, 1), "Khe 0 giong wall cu");

        WallLayout d;
        auto outs = makeWall(0, 3, 2, cell, 0, false, gap);
        const bool ok = detectWall(outs, 0, wallCanvas(3, 2, cell, gap), d);
        CHECK(ok && d.cols == 3 && d.rows == 2 && d.cell == cell && d.gap == gap && d.cells.size() == 6 && d.cells[5].col == 2 && d.cells[5].row == 1,
              "Nhan lai wall 3x2 co khe tu crop");
        CHECK(detectWall(makeWall(0, 4, 1, cell), 0, QSize(7680, 1080), d) && d.gap == QSize(0, 0) && d.cols == 4 && d.rows == 1,
              "Wall cu khong khe cung duoc nhan ra");
        CHECK(d.maxGap() == QSize(kMaxWallGap < (16384 - 7680) / 3 ? kMaxWallGap : (16384 - 7680) / 3, 0), "Khe toi da gioi han theo canvas 16384; 1 hang -> khe doc 0");
        CHECK(detectWall(makeWall(0, 1, 3, cell, 0, false, QSize(0, 30)), 0, QSize(1920, 3300), d) && d.rows == 3 && d.gap.height() == 30,
              "Wall doc 1x3");

        auto extra = makeWall(0, 2, 1, cell, 0, false, QSize(40, 0));
        OutputConfig monitor; monitor.size = {640, 360};                    // man giam sat lay ca canvas
        OutputConfig ndi; ndi.kind = OutputKind::Ndi; ndi.crop = QRectF(0, 0, 0.5, 1);
        extra.insert(extra.begin(), monitor);
        extra.push_back(ndi);
        CHECK(detectWall(extra, 0, QSize(3880, 1080), d) && d.cells.size() == 2 && d.cells[0].output == 1 && d.cells[1].output == 2,
              "Bo qua Output lay ca canvas va Output NDI");
        CHECK(!detectWall(extra, 1, QSize(3880, 1080), d) && !detectWall(extra, 0, QSize(3840, 1080), d), "Kenh khac / canvas khong khop: khong phai wall");
        auto broken = makeWall(0, 3, 1, cell, 0, false, QSize(40, 0));
        broken[1].crop.moveLeft(broken[1].crop.x() + 10.0 / 5840);
        CHECK(!detectWall(broken, 0, QSize(5840, 1080), d), "Crop bi xe dich 10 diem anh: khong con la wall");
        broken = makeWall(0, 2, 2, cell);
        broken.pop_back();
        CHECK(!detectWall(broken, 0, QSize(3840, 2160), d), "Thieu mot o: khong phai wall");
        broken = makeWall(0, 2, 1, cell);
        broken[1] = broken[0];
        CHECK(!detectWall(broken, 0, QSize(3840, 1080), d), "Hai Output trung o: khong phai wall");
    }

    // ---------------- 1:1 tung diem anh ----------------
    {
        QOffscreenSurface surf;
        surf.setFormat(fmt);
        surf.create();
        QOpenGLContext ctx;
        ctx.setFormat(fmt);
        if (!ctx.create() || !ctx.makeCurrent(&surf)) { std::cerr << "Khong co OpenGL 3.3\n"; return 2; }
        Compositor comp;
        if (!comp.init()) { std::cerr << "Shader loi\n"; return 2; }
        CHECK(wrongPixels(comp, 4, 1, {1920, 1080}, {40, 0}) == 0, "4x1 1920x1080 khe 40: moi diem anh dung vi tri, phan sau khe khong hien");
        CHECK(wrongPixels(comp, 2, 2, {960, 540}, {37, 23}) == 0, "2x2 khe le 37x23: moi diem anh dung vi tri");
        CHECK(wrongPixels(comp, 3, 3, {333, 217}, {1, 5}) == 0, "3x3 o le 333x217 khe 1x5: moi diem anh dung vi tri");

        TargetPool pool(comp.gl());                                // canvas doi co khong duoc de lai bo dem GPU cu mai mai
        pool.beginFrame();
        for (int w = 100; w < 110; ++w) pool.acquire(w, 10);
        for (uint64_t f = 0; f <= TargetPool::kKeepFrames; ++f) { pool.beginFrame(); pool.acquire(100, 10); }
        CHECK(pool.size() == 1, "TargetPool giai phong bo dem khong con dung, giu bo dem dang dung");
        pool.clear();
    }

    // ---------------- luu / mo ----------------
    {
        const QString file = dir.filePath("wall.json");
        Project p = wallProject(4, 1, {1920, 1080}, {40, 0});
        QString err;
        CHECK(saveProject(p, file, &err), "Luu project wall co khe");
        Project q;
        CHECK(loadProject(q, file, &err), "Mo lai");
        WallLayout d;
        CHECK(q.channels[0]->size == QSize(7800, 1080) && q.scenes[0]->size == QSize(7800, 1080) &&
                  detectWall(q.outputs, 0, q.channels[0]->size, d) && d.gap == QSize(40, 0) && d.cell == QSize(1920, 1080),
              "Khe con nguyen sau khi mo lai");
        QFile f(file); f.open(QIODevice::ReadOnly);
        const QByteArray json = f.readAll();
        CHECK(QJsonDocument::fromJson(json).object()["version"].toInt() == 1 && !json.contains("\"gap\"") && !json.contains("\"wall\""),
              "Khong doi dinh dang: version 1, khong them truong");
    }

    // ---------------- giao dien ----------------
    {
        const QSize cell(160, 90);
        Project p = wallProject(3, 2, cell, {0, 0});              // "project cu": khong khe
        const auto outputs = p.outputs;
        p.outputs.clear();                                        // khong cho engine mo cua so; dung Output gia
        RenderEngine engine(p);
        engine.startEngine();
        std::vector<Probe*> probes;
        for (const OutputConfig& c : outputs) {
            auto probe = std::make_unique<Probe>();
            probe->cfg = c; probe->cellW = cell.width();
            probes.push_back(probe.get());
            engine.addSink(std::move(probe));
        }
        engine.postAndWait([&](double) { p.outputs = outputs; });
        CHECK(waitFrames(*probes[0], 3) && probes[0]->canvasW == 480, "Engine chay, canvas ban dau 480");

        QUndoStack undo;
        InspectorPanel insp(p, engine, &undo);
        auto* gx = insp.findChild<QSpinBox*>("wallGapX");
        auto* gy = insp.findChild<QSpinBox*>("wallGapY");
        auto* info = insp.findChild<QLabel*>("wallInfo");
        CHECK(gx && gy && info && gx->isEnabled() && gy->isEnabled() && gx->value() == 0 && info->text().contains("480×180"),
              "Tab Output co o khe ngang/doc cho wall 3x2");
        auto& ch = *p.channels[0];
        auto state = [&](QSize canvas, QSize gap) {
            WallLayout d;
            std::vector<OutputConfig> now;
            engine.postAndWait([&](double) { for (Probe* pr : probes) now.push_back(pr->cfg); });
            return ch.size == canvas && p.scenes[0]->size == canvas && p.scenes[1]->size == canvas && p.scenes[2]->size == QSize(100, 50) &&
                   detectWall(now, 0, canvas, d) && d.gap == gap && d.cell == cell;
        };

        gx->setValue(8);
        gy->setValue(6);
        CHECK(state({496, 186}, {8, 6}), "Dat khe 8x6: canvas + Scene cung co thanh 496x186, Scene co rieng giu nguyen, crop theo khe");
        CHECK(waitFrames(*probes[0], 3) && probes[0]->canvasW == 496, "Render thread ve canvas moi");
        CHECK(ch.program == 0 && ch.preview == 1, "Program/Preview khong doi");
        CHECK(undo.count() == 2, "Moi chieu mot buoc Hoan tac");
        gy->setValue(10);
        CHECK(undo.count() == 2 && state({496, 190}, {8, 10}), "Chinh tiep cung chieu gop vao mot buoc");

        undo.undo();
        CHECK(state({496, 180}, {8, 0}) && gy->value() == 0, "Hoan tac khe doc");
        undo.undo();
        CHECK(state({480, 180}, {0, 0}) && gx->value() == 0, "Hoan tac khe ngang: ve nhu ban dau");
        undo.redo(); undo.redo();
        CHECK(state({496, 190}, {8, 10}) && gx->value() == 8 && gy->value() == 10, "Lam lai");

        insp.setLocked(true);
        const int steps = undo.count();
        insp.setWallGap(0, {20, 20}, 0);
        gx->setValue(30);
        CHECK(!gx->isEnabled() && undo.count() == steps && state({496, 190}, {8, 10}), "Khoa van hanh: khong doi duoc khe");
        insp.setLocked(false);
        gx->setValue(8);                                          // tra o nhap ve gia tri dang ap dung

        insp.setWallGap(0, {kMaxWallGap * 10, 10}, 0);
        CHECK(state({480 + 2 * kMaxWallGap, 190}, {kMaxWallGap, 10}), "Khe qua lon bi ep ve gioi han");
        insp.setWallGap(0, {8, 10}, 0);
        CHECK(state({496, 190}, {8, 10}) && undo.count() == steps, "Tra ve khe cu: khong de lai buoc Hoan tac rong");

        // Sua crop tay lam lech luoi -> khoa o khe, giai thich; hoan tac thi mo lai
        QDoubleSpinBox* cropX = nullptr;
        for (auto* s : insp.findChildren<QDoubleSpinBox*>()) if (s->prefix() == "crop x ") { cropX = s; break; }
        cropX->setValue(0.05);
        CHECK(!gx->isEnabled() && info->text().contains("không còn khớp"), "Crop lech luoi: o khe bi khoa va co giai thich");
        insp.setWallGap(0, {2, 2}, 0);
        CHECK(ch.size == QSize(496, 190), "Khi khong phai wall: khong doi canvas");
        undo.undo();
        CHECK(gx->isEnabled() && gx->value() == 8 && state({496, 190}, {8, 10}), "Hoan tac sua crop: o khe dung lai duoc");

        CHECK(waitFrames(*probes[0], 3), "Engine van chay");
        int bad = 0;
        for (Probe* pr : probes) bad += pr->bad;
        CHECK(bad == 0, "Khong khung hinh nao thay canvas va crop lech nhau (doi cung mot khung)");

        engine.postAndWait([&](double) { engine.syncOutputsToProject(); });
        const QString file = dir.filePath("ui.json");
        QString err;
        Project q;
        WallLayout d;
        CHECK(saveProject(p, file, &err) && loadProject(q, file, &err) && detectWall(q.outputs, 0, q.channels[0]->size, d) && d.gap == QSize(8, 10),
              "Luu sau khi dat khe tu giao dien roi mo lai: khe dung");
        engine.stopEngine();
    }

    // ---------------- dong lenh ----------------
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("LUMACAST_CONFIG_DIR", dir.filePath("config"));
        auto exitCode = [&](const QStringList& args) {
            QProcess pr;
            pr.setProcessEnvironment(env);
            pr.start(LUMACAST_EXE, args);
            if (!pr.waitForFinished(15000)) { pr.kill(); pr.waitForFinished(5000); return -1; }
            return pr.exitCode();
        };
        CHECK(exitCode({"--gap", "8"}) == 2 && exitCode({"--wall", "2x1", "--gap", "-3"}) == 2 && exitCode({"--wall", "2x1", "--gap", "5000"}) == 2 &&
                  exitCode({"--wall", "8x1", "--cell", "1920x1080", "--gap", "400"}) == 2,
              "--gap sai (thieu --wall, am, qua lon, canvas > 16384) bi tu choi");

        const QString file = dir.filePath("cli.json");
        QProcess pr;
        pr.setProcessEnvironment(env);
        pr.start(LUMACAST_EXE, {"--wall", "2x2", "--cell", "160x90", "--gap", "12x6", "--no-audio", "--save", file});
        QElapsedTimer t; t.start();
        while (!QFile::exists(file) && t.elapsed() < 20000 && pr.state() != QProcess::NotRunning) QThread::msleep(50);
        pr.kill();
        pr.waitForFinished(5000);
        Project q;
        QString err;
        WallLayout d;
        CHECK(loadProject(q, file, &err) && q.channels[0]->size == QSize(332, 186) && q.scenes[0]->size == QSize(332, 186) &&
                  detectWall(q.outputs, 0, q.channels[0]->size, d) && d.gap == QSize(12, 6) && d.cell == QSize(160, 90) && d.cols == 2 && d.rows == 2,
              "--wall 2x2 --cell 160x90 --gap 12x6 luu dung canvas va khe");
    }

    std::cout << (g_fail ? "CO LOI: " : "TAT CA DAT: ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
