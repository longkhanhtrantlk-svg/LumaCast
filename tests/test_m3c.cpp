// Test M3c: do tin cay (log, thong ke khung, watchdog, nhan dien man hinh/mat man hinh, mau thu theo Output).
#include "core/Log.h"
#include "core/Project.h"
#include "media/StaticSources.h"
#include "render/Engine.h"
#include "render/FrameStats.h"
#include "render/OutputHealth.h"
#include "render/OutputSupervisor.h"
#include "render/OutputWindow.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QScreen>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

using namespace lc;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << msg << "  (" #cond ")\n"; } else std::cout << "ok:   " << msg << "\n"; } while (0)

static void pump(int ms) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { QApplication::processEvents(); QThread::msleep(5); }
}

static void buildProject(Project& p, std::vector<OutputConfig> outs) {
    SourceDef a; a.type = "color"; a.color = Qt::red; a.label = "do";
    auto src = p.addSource(a);
    auto s = std::make_shared<Scene>();
    s->name = "do"; s->size = {64, 36};
    s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
    p.scenes = {s};
    auto ch = std::make_shared<Channel>();
    ch->name = "K"; ch->size = {64, 36}; ch->scenes = p.scenes; ch->program = 0; ch->preview = -1;
    p.channels = {ch};
    p.outputs = std::move(outs);
}

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc, argv);
    QTemporaryDir dir;

    // ---- FrameStats ----
    {
        FrameStats st(100);
        for (int i = 1; i <= 100; ++i) st.add(i);                 // 1..100 ms
        CHECK(st.size() == 100 && st.percentile(0.5) >= 50 && st.percentile(0.5) <= 52 && st.percentile(0.99) >= 99 && st.max() == 100,
              "FrameStats: p50 ~50, p99 ~99, max 100");
        for (int i = 0; i < 100; ++i) st.add(1.0);                // ghi de het (vong dem)
        CHECK(st.max() == 1.0 && st.percentile(0.99) == 1.0, "FrameStats: vong dem ghi de mau cu");
        FrameStats empty;
        CHECK(empty.percentile(0.5) == 0 && empty.max() == 0, "FrameStats rong tra 0");
    }

    // ---- Log xoay vong ----
    {
        const QString ld = dir.filePath("logs");
        initLogging(ld, 400, 2);
        for (int i = 0; i < 60; ++i) qInfo().noquote() << "dong log so" << i << "xxxxxxxxxxxxxxxxxxxx";
        shutdownLogging();
        QFile f(ld + "/lumacast.log");
        f.open(QIODevice::ReadOnly);
        const QString cur = QString::fromUtf8(f.readAll());
        CHECK(QFile::exists(ld + "/lumacast.log.1") && QFile::exists(ld + "/lumacast.log.2") && !QFile::exists(ld + "/lumacast.log.3"),
              "Log: xoay vong, giu dung 2 file cu");
        CHECK(f.size() <= 400 && cur.contains("dong log so 59") && cur.contains("[I]") && cur.contains("20"), "Log: file hien tai nho, co dong moi nhat, co nhan muc va thoi gian");
    }

    // ---- Nhan dien man hinh ----
    {
        const QStringList names = {"A", "B", "C"};
        OutputConfig c; c.fullscreen = true;
        c.screenIndex = 1;
        CHECK(resolveScreenIndex(c, names) == 1, "Man hinh theo chi so");
        c.screenIndex = 5;
        CHECK(resolveScreenIndex(c, names) == -1, "Chi so vuot so man hinh -> -1 (khong dung man cuoi nua)");
        c.screenName = "C"; c.screenIndex = 0;
        CHECK(resolveScreenIndex(c, names) == 2, "Ten man hinh uu tien hon chi so");
        CHECK(resolveScreenIndex(c, {"A", "C"}) == 1, "Rut man B: man C doi chi so nhung van tim dung theo ten");
        CHECK(resolveScreenIndex(c, {"A", "B"}) == -1, "Man C bi rut -> khong con");
        CHECK(evaluateOutput(c, names, 2, true) == OutputHealth::Ok && evaluateOutput(c, names, 2, false) == OutputHealth::Hidden &&
                  evaluateOutput(c, names, 0, true) == OutputHealth::WrongScreen && evaluateOutput(c, {"A"}, 0, true) == OutputHealth::NoScreen,
              "evaluateOutput: Ok / Hidden / WrongScreen / NoScreen");
        OutputConfig w; w.fullscreen = false;
        CHECK(evaluateOutput(w, names, 0, true) == OutputHealth::Ok && evaluateOutput(w, names, 0, false) == OutputHealth::Hidden,
              "Output cua so thuong: chi xet co hien khong");
        CHECK(healthText(OutputHealth::NoScreen, c).contains("MẤT MÀN HÌNH") && healthText(OutputHealth::NoScreen, c).contains("C"), "Thong bao mat man hinh co ten man");
    }

    // ---- screenName trong project JSON ----
    {
        Project p;
        OutputConfig o; o.name = "L"; o.fullscreen = true; o.screenIndex = 1; o.screenName = "\\\\.\\DISPLAY2";
        buildProject(p, {o});
        const QString file = dir.filePath("p.json");
        QString err;
        CHECK(saveProject(p, file, &err), "Luu project co screenName");
        Project q;
        CHECK(loadProject(q, file, &err) && q.outputs[0].screenName == "\\\\.\\DISPLAY2" && q.outputs[0].screenIndex == 1,
              "screenName luu va doc lai dung");
    }

    // ---- Mau thu theo Output ----
    {
        const QImage img = makeOutputTestPattern(640, 360, "Output X");
        CHECK(img.size() == QSize(640, 360) && img.pixelColor(2, 180) == QColor(255, 0, 0) && img.pixelColor(637, 180) == QColor(255, 0, 0),
              "Mau thu: dung do phan giai, vien do o hai mep (de thay mep man hinh)");
        bool hasYellow = false;
        for (int x = 0; x < 640 && !hasYellow; ++x)
            for (int y = 150; y < 210; ++y) if (img.pixelColor(x, y) == QColor(255, 220, 0)) { hasYellow = true; break; }
        CHECK(hasYellow, "Mau thu co thuoc do/toa do mau vang");
    }

    // ---- Engine + Output that: watchdog, thong ke khung, mat man hinh, mau thu ----
    {
        const QString realScreen = QGuiApplication::screens()[0]->name();
        OutputConfig win; win.name = "Cua so"; win.size = {320, 180};
        OutputConfig gone; gone.name = "Man mat"; gone.fullscreen = true; gone.screenName = "MAN-HINH-KHONG-TON-TAI"; gone.size = {640, 360};
        OutputConfig real; real.name = "Man that"; real.fullscreen = true; real.screenName = realScreen;
        Project p;
        buildProject(p, {win, gone, real});
        RenderEngine engine(p);
        engine.setStallThresholdMs(300);
        engine.startEngine();
        OutputSupervisor sup(engine);
        pump(1500);
        sup.check();

        CHECK(engine.error().isEmpty() && engine.frameP50() > 0 && engine.frameP99() >= engine.frameP50(),
              "Thong ke khung: p50 > 0, p99 >= p50");
        CHECK(!engine.stalled(), "Watchdog: khong bao treo khi chay binh thuong");

        auto* gw = dynamic_cast<OutputWindow*>(engine.output(1));
        CHECK(gw && !gw->isVisible() && sup.health(1) == OutputHealth::NoScreen,
              "Man khong ton tai: cua so KHONG duoc hien (khong nhay sang man khac), bao NoScreen");
        CHECK(sup.text(1).contains("MẤT MÀN HÌNH"), "Thong bao nhan biet Output bi mat man");
        CHECK(sup.health(2) == OutputHealth::Ok && dynamic_cast<OutputWindow*>(engine.output(2))->isVisible(), "Man co that: Ok va dang hien");
        CHECK(sup.badCount() == 1, "Dung 1 Output co van de");

        // watchdog: chan render thread 800 ms
        engine.post([](double) { QThread::msleep(800); });
        pump(500);
        CHECK(engine.stalled(), "Watchdog: render thread bi chan -> stalled()");
        pump(900);
        CHECK(!engine.stalled(), "Watchdog: render thread chay lai -> het stalled");

        // mau thu tren cua so that
        OutputSink* s0 = engine.output(0);
        const uint64_t before = s0->presented;
        s0->testPattern = true;
        pump(500);
        CHECK(s0->presented > before + 5, "Mau thu: Output van ve lien tuc");
        auto* w0 = dynamic_cast<OutputWindow*>(s0);
        const QImage grab = w0->screen()->grabWindow(w0->winId()).toImage();
        if (grab.isNull() || grab.width() < 100) {
            std::cout << "skip: khong chup duoc noi dung cua so GL tren nen tang nay\n";
        } else {
            // vien do 4px cua mau thu -> diem (2, giua) phai do
            const int cy = grab.height() / 2;
            const QColor edge = grab.pixelColor(2, cy);
            CHECK(edge.red() > 200 && edge.green() < 60 && edge.blue() < 60, "Mau thu hien tren man hinh that: vien do o mep trai");
        }
        s0->black = true;
        pump(300);
        s0->black = false;
        s0->testPattern = false;

        // dung hinh: presented khong tang
        s0->freeze = true;
        pump(200);
        const uint64_t f0 = s0->presented;
        pump(400);
        CHECK(s0->presented == f0, "Dung hinh: Output khong ve them khung");
        s0->freeze = false;
        engine.stopEngine();
    }

    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
