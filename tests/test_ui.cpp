// Test UI: dieu khien MainWindow nhu nguoi dung (bam nut, double-click, doi gia tri) va kiem tra
// ket qua o render thread / file da luu.
#include "InspectorPanel.h"
#include "MainWindow.h"
#include "MonitorWidget.h"
#include "media/StaticSources.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <iostream>

using namespace lc;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << msg << "  (" #cond ")\n"; } else std::cout << "ok:   " << msg << "\n"; } while (0)

static void pump(int ms) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { QApplication::processEvents(); QThread::msleep(5); }
}
static QPushButton* button(QWidget* root, const QString& text) {
    for (auto* b : root->findChildren<QPushButton*>()) if (b->text() == text) return b;
    return nullptr;
}
static QDoubleSpinBox* byPrefix(QWidget* root, const QString& prefix) {
    for (auto* s : root->findChildren<QDoubleSpinBox*>()) if (s->prefix() == prefix) return s;
    return nullptr;
}

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc, argv);
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);                    // khong lam ban cau hinh that cua may
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.filePath("cfg"));

    Project p;
    SourceDef a; a.type = "color"; a.color = Qt::red; a.label = "do";
    SourceDef b; b.type = "color"; b.color = Qt::blue; b.label = "xanh";
    auto sa = p.addSource(a), sb = p.addSource(b);
    for (auto src : {sa, sb}) {
        auto s = std::make_shared<Scene>();
        s->name = src->name(); s->size = {64, 36};
        s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        p.scenes.push_back(s);
    }
    auto ch = std::make_shared<Channel>();
    ch->name = "K"; ch->size = {64, 36}; ch->scenes = p.scenes; ch->program = 0; ch->preview = 1;
    p.channels = {ch};
    OutputConfig o1; o1.name = "O1"; o1.size = {160, 90};
    OutputConfig o2; o2.name = "O2"; o2.size = {90, 160};
    p.outputs = {o1, o2};
    p.filePath = dir.filePath("ui.json");

    RenderEngine engine(p);
    engine.startEngine();
    MainWindow w(p, engine);
    w.setAskOnClose(false);
    w.show();
    pump(1400);                                   // fps cap nhat moi 1 giay, monitor ~15Hz
    CHECK(engine.error().isEmpty() && engine.fps() > 0 && engine.output(0)->presented > 0, "Engine + MainWindow chay, output dang ve");
    CHECK(!engine.gpuName().isEmpty(), "Doc duoc ten GPU");

    // ---- monitor ----
    const auto mf = engine.monitor(0);
    CHECK(!mf.program.isNull() && !mf.preview.isNull() && mf.program.pixelColor(5, 5) == QColor(255, 0, 0) &&
              mf.preview.pixelColor(5, 5) == QColor(0, 0, 255), "Monitor: Program do, Preview xanh");
    bool anyImage = false;
    for (auto* m : w.findChildren<MonitorWidget*>()) anyImage = anyImage || m->hasImage();
    CHECK(anyImage, "MonitorWidget hien hinh");

    auto* scenes = w.findChild<QListWidget*>("sceneList");
    auto* media = w.findChild<QListWidget*>("mediaList");
    CHECK(scenes && scenes->count() == 2 && media && media->count() == 2, "Scene list va Media Pool co du muc");

    // ---- CUT / chon scene ----
    button(&w, "CUT")->click();
    pump(250);
    CHECK(ch->program == 1 && ch->preview == 0, "Bam CUT: Program <-> Preview hoan doi");
    emit scenes->itemDoubleClicked(scenes->item(0));
    pump(250);
    CHECK(ch->program == 0, "Double-click scene -> len Program");
    emit scenes->itemClicked(scenes->item(1));
    pump(250);
    CHECK(ch->preview == 1 && ch->program == 0, "Click scene -> chi vao Preview, Program khong doi");

    // ---- TAKE (Fade) ----
    button(&w, "TAKE")->click();
    pump(300);
    CHECK(ch->inTransition(), "Bam TAKE: dang chuyen");
    pump(1300);
    CHECK(!ch->inTransition() && ch->program == 1, "TAKE xong: Program doi sang scene Preview");

    // ---- BLACK ALL / Freeze / FTB ----
    auto* blackAll = button(&w, "BLACK ALL");
    blackAll->click();
    pump(200);
    CHECK(engine.output(0)->black && engine.output(1)->black, "BLACK ALL: ca hai output den");
    blackAll->click();
    pump(200);
    CHECK(!engine.output(0)->black && !engine.output(1)->black, "BLACK ALL lan 2: tra hinh");

    QToolButton* freeze = nullptr;
    for (auto* t : w.findChildren<QToolButton*>()) if (t->text() == "Dung") { freeze = t; break; }
    freeze->click();
    pump(200);
    const uint64_t p0 = engine.output(0)->presented, p1 = engine.output(1)->presented;
    pump(400);
    CHECK(engine.output(0)->freeze != engine.output(1)->freeze, "Freeze chi tac dong dung output duoc chon");
    const bool frozenIsFirst = engine.output(0)->freeze;
    const uint64_t frozenNow = frozenIsFirst ? engine.output(0)->presented : engine.output(1)->presented;
    const uint64_t frozenThen = frozenIsFirst ? p0 : p1;
    const uint64_t liveNow = frozenIsFirst ? engine.output(1)->presented : engine.output(0)->presented;
    const uint64_t liveThen = frozenIsFirst ? p1 : p0;
    CHECK(frozenNow == frozenThen && liveNow > liveThen + 5, "Output dung hinh khong ve them khung, output con lai van chay");
    freeze->click();

    button(&w, "FTB")->click();
    pump(1500);
    CHECK(ch->ftbTarget && ch->ftbAmount == 1.0f, "FTB: Program ve den hoan toan");
    CHECK(engine.monitor(0).program.pixelColor(5, 5) == QColor(0, 0, 0), "Monitor Program cung den khi FTB");
    button(&w, "FTB")->click();
    pump(1500);
    CHECK(!ch->ftbTarget && ch->ftbAmount == 0.0f, "FTB lan 2: tro lai hinh");

    // ---- Inspector: Layer + Output ----
    auto* insp = w.inspector();
    auto* alpha = byPrefix(insp, "alpha ");
    alpha->setValue(0.5);
    pump(200);
    CHECK(std::abs(p.scenes[0]->layers[0].opacity - 0.5f) < 1e-4, "Inspector: doi alpha -> Scene.layers[0].opacity = 0.5");
    button(insp, "Them layer")->click();
    pump(200);
    CHECK(p.scenes[0]->layers.size() == 2, "Them layer");
    QPushButton* del = nullptr;
    for (auto* bt : insp->findChildren<QPushButton*>()) if (bt->text() == "Xoa") del = bt;
    del->click();
    pump(200);
    CHECK(p.scenes[0]->layers.size() == 1, "Xoa layer vua them");
    byPrefix(insp, "sang ")->setValue(0.5);
    pump(200);
    CHECK(std::abs(engine.output(0)->config().cal.brightness - 0.5f) < 1e-4, "Inspector: doi 'sang' -> Output.cal.brightness = 0.5");

    // ---- Nhap media -> Scene moi ----
    QImage img(32, 18, QImage::Format_RGBA8888); img.fill(Qt::green);
    const QString png = dir.filePath("g.png");
    img.save(png);
    CHECK(w.addMediaFile(png), "Nhap anh");
    CHECK(!w.addMediaFile(dir.filePath("khong_co.mp4")), "Nhap file khong ton tai -> bao loi, khong crash");
    pump(500);
    CHECK(p.scenes.size() == 3 && scenes->count() == 3 && media->count() == 3 && ch->scenes.size() == 3,
          "Nhap media: them nguon + Scene, cap nhat Scene list / Media Pool");

    // ---- Luu / autosave ----
    w.saveProject(false);
    pump(600);
    CHECK(!QFileInfo::exists(p.filePath + ".autosave"), "Luu thanh cong -> xoa ban autosave cu");
    w.autosaveNow();
    pump(600);
    Project q;
    QString err;
    CHECK(loadProject(q, p.filePath, &err), "Project luu tu UI mo lai duoc: " + err.toStdString());
    CHECK(q.scenes.size() == 3 && q.sources.size() == 3 && std::abs(q.outputs[0].cal.brightness - 0.5f) < 1e-4 &&
              std::abs(q.scenes[0]->layers[0].opacity - 0.5f) < 1e-4, "File luu chua scene moi + hieu chinh mau + alpha MOI");
    Project r;
    CHECK(loadProject(r, p.filePath + ".autosave", &err) && r.scenes.size() == 3, "Autosave: file duoc tao va mo lai duoc");

    engine.stopEngine();
    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
