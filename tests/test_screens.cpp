// Test thiet lap man hinh (UI-08): canh bao co gian / cat / phong to, vung LED trong tin hieu (diem anh that tren cua so),
// them/xoa/doi Output luc dang chay, khung hinh, video wall, cau hinh luu san, project moi, Hoan tac, Khoa van hanh.
// LC_SHOT_DIR=<thu muc> de luu anh chup hop thoai.
#include "InspectorPanel.h"
#include "MainWindow.h"
#include "ScreenDialog.h"
#include "Theme.h"
#include "core/AppSettings.h"
#include "media/StaticSources.h"
#include "render/OutputWindow.h"
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <QUndoStack>
#include <cmath>
#include <iostream>

using namespace lc;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << msg << "  (" #cond ")\n"; } else std::cout << "ok:   " << msg << "\n"; } while (0)

static void pump(int ms) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(5);
    }
}
template <class F> static bool waitFor(F cond, int ms = 4000) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { pump(10); if (cond()) return true; }
    return cond();
}

static OutputConfig out(const QString& name, QSize size) {
    OutputConfig o;
    o.name = name;
    o.size = size;
    return o;
}

// ---------------- 1. canh bao + tinh diem anh ----------------
static void testWarnings() {
    const QSize full(1920, 1080);
    std::vector<OutputConfig> v{out("A", full)};
    auto warn = [&](QSize canvas, int i = 0) { return outputWarnings(v, i, canvas, outputSignalSize(v[size_t(i)])).join(" | "); };
    CHECK(warn(full).isEmpty(), "1920x1080 -> 1920x1080: khong canh bao");
    QString w = warn({1320, 360});
    CHECK(w.contains("phóng to ×1,45") && w.contains("viền đen"), "canvas 1320x360 tren tin hieu 1920x1080 (truong hop nhoe): bao phong to va vien den");
    v[0].scale = ScaleMode::Stretch;
    CHECK(warn({1320, 360}).contains("co giãn méo"), "Stretch khac ti le: bao co gian meo");
    v[0].scale = ScaleMode::Fit;
    CHECK(warn({3840, 2160}).contains("thu nhỏ ×0,50"), "canvas lon hon: bao thu nho");
    v[0].scale = ScaleMode::Pixel;
    CHECK(warn({3840, 2160}).contains("cắt mép") && warn({1280, 720}).contains("viền đen"), "1:1 khac co: bao cat mep / vien den");
    v[0].scale = ScaleMode::Fit;

    v[0].area = QRect(0, 0, 1600, 800);
    CHECK(warn({1600, 800}).isEmpty(), "vung LED 1600x800 + khung hinh 1600x800: 1:1, khong canh bao");
    CHECK(warn(full).contains("thu nhỏ"), "vung LED nho hon khung hinh: bao thu nho");
    v[0].area = QRect(400, 0, 1600, 800);
    CHECK(warn({1600, 800}).contains("vượt ra ngoài"), "vung LED tran ra ngoai tin hieu: bao bi cat");
    v[0].area = QRect();

    v[0].crop = QRectF(0, 0, 0.5, 1);
    CHECK(warn({3840, 1080}).isEmpty() == false && warn({3840, 1080}).contains("một phần"), "mot Output chi lay nua khung: bao hinh bi cat");
    v = makeWall(0, 2, 1, full);
    CHECK(warn({3840, 1080}, 0).isEmpty() && warn({3840, 1080}, 1).isEmpty(), "video wall khop luoi: viec cat la dung y, khong canh bao");

    CHECK(ledPixels(4, 2.5) == 1600 && ledPixels(2, 2.5) == 800 && ledPixels(6, 3.91) == 1535 && ledPixels(0, 2.5) == 0, "tinh diem anh tu met va pitch");
    auto s = std::make_shared<Scene>();
    s->name = "Cu"; s->size = {1320, 360};
    CHECK(sceneSizeWarning({s}, {1320, 360}).isEmpty() && sceneSizeWarning({s}, full).contains("Cu"), "Scene dung khac khung hinh: bao ten");
}

// ---------------- 2. luu / mo, project moi ----------------
static void testProject(const QString& dir) {
    Project p = makeNewProject({1600, 800});
    CHECK(p.channels.size() == 1 && p.channels[0]->size == QSize(1600, 800) && p.scenes.size() == 2 && p.channels[0]->program == 0
              && p.outputs.size() == 1 && p.outputs[0].size == QSize(1920, 1080),
          "project moi: 1 kenh, Program den + mau thu, mot Output 1920x1080");
    p.outputs[0].area = QRect(0, 0, 1600, 800);
    const QString path = dir + "/moi.json";
    QString err;
    Project q;
    CHECK(saveProject(p, path, &err) && loadProject(q, path, &err) && q.outputs[0].area == QRect(0, 0, 1600, 800) && q.channels[0]->size == QSize(1600, 800)
              && q.scenes.size() == 2,
          "luu / mo giu vung LED va khung hinh");
    p.outputs[0].area = QRect();
    saveProject(p, path, &err);
    QFile f(path);
    CHECK(f.open(QIODevice::ReadOnly) && !f.readAll().contains("\"area\""), "khong co vung LED thi khong ghi truong area");
}

// ---------------- 3. cua so chinh ----------------
static bool near(const QColor& a, const QColor& b) {
    return std::abs(a.red() - b.red()) < 60 && std::abs(a.green() - b.green()) < 60 && std::abs(a.blue() - b.blue()) < 60;
}

static void testWindow(const QString& dir) {
    Project p;
    SourceDef d;
    d.type = "color";
    d.color = Qt::green;
    d.label = "la";
    auto src = p.addSource(d);
    auto add = [&](const QString& name, QSize size) {
        auto s = std::make_shared<Scene>();
        s->name = name; s->size = size;
        s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        p.scenes.push_back(s);
    };
    add("A", {400, 300});
    add("B", {400, 300});
    add("Rieng", {200, 100});
    auto ch = std::make_shared<Channel>();
    ch->name = "K"; ch->size = {400, 300}; ch->scenes = p.scenes; ch->program = 0; ch->preview = 1;
    p.channels = {ch};
    p.outputs = {out("O1", {400, 300})};
    p.filePath = dir + "/s.json";

    RenderEngine engine(p);
    engine.startEngine();
    {
        MainWindow w(p, engine, {});
        w.setAskOnClose(false);
        w.resize(1536, 780);
        w.move(0, 0);
        w.show();
        pump(900);
        auto* us = w.undoStack();
        OutputSink* first = engine.output(0);
        auto* firstWin = dynamic_cast<OutputWindow*>(first);
        CHECK(firstWin && firstWin->isVisible(), "(chuan bi) Output cua so dang hien");

        // ---------- them / doi ten / xoa ----------
        ScreenSetup s = w.screenSetup();
        s.outputs.push_back(out("O2", {320, 180}));
        CHECK(w.applyScreenSetup(s), "them Output thu hai");
        pump(400);
        CHECK(engine.outputCount() == 2 && engine.output(0) == first && dynamic_cast<OutputWindow*>(engine.output(1))->isVisible(),
              "Output moi hien ra; cua so cu GIU NGUYEN (khong tao lai)");
        CHECK(p.outputs.size() == 2 && p.outputs[1].name == "O2" && w.findChild<QToolButton*>("reopenOutput1")
                  && w.inspector()->outputCount() == 2 && w.controlApi().state()["outputs"].toArray().size() == 2,
              "danh sach Output cua project, bang Outputs, Inspector va API cung cap nhat");
        CHECK(ch->program == 0 && ch->preview == 1, "them Output khong doi Program/Preview");
        CHECK(waitFor([&] { return engine.output(1)->presented > 10; }), "Output moi duoc ve");

        us->undo();
        pump(400);
        CHECK(engine.outputCount() == 1 && engine.output(0) == first && w.inspector()->outputCount() == 1 && p.outputs.size() == 1, "Hoan tac: go Output vua them");
        us->redo();
        pump(400);
        CHECK(engine.outputCount() == 2 && p.outputs[1].name == "O2", "Lam lai: Output tro lai");

        s = w.screenSetup();
        s.outputs[0].name = "LED chính";
        CHECK(w.applyScreenSetup(s) && engine.output(0) == first && first->config().name == "LED chính", "doi ten: khong tao lai cua so");
        s = w.screenSetup();
        s.outputs.erase(s.outputs.begin());
        OutputSink* second = engine.output(1);
        CHECK(w.applyScreenSetup(s), "xoa Output dau");
        pump(300);
        CHECK(engine.outputCount() == 1 && engine.output(0) == second && !firstWin->isVisible(), "Output con lai giu nguyen cua so; cua so bi xoa duoc an");
        CHECK(engine.error().isEmpty() && !engine.stalled(), "engine van chay sau khi them/bot Output");
        us->undo();
        pump(300);
        CHECK(engine.outputCount() == 2 && engine.output(0)->config().name == "LED chính", "Hoan tac xoa");

        // BLACK ALL dang bat: Output moi them cung den
        QPushButton* blackAll = w.findChild<QPushButton*>("blackAll");
        blackAll->click();
        s = w.screenSetup();
        s.outputs.push_back(out("O3", {320, 180}));
        w.applyScreenSetup(s);
        pump(300);
        CHECK(engine.outputCount() == 3 && engine.output(2)->black, "dang BLACK ALL: Output moi cung den (khong loe hinh)");
        blackAll->click();
        us->undo();
        pump(300);

        // chi mot Output cho phan con lai
        s = w.screenSetup();
        s.outputs.resize(1);
        w.applyScreenSetup(s);
        pump(300);

        // ---------- khung hinh ----------
        s = w.screenSetup();
        s.canvases[0] = {640, 360};
        CHECK(w.applyScreenSetup(s) && ch->size == QSize(640, 360) && p.scenes[0]->size == QSize(640, 360) && p.scenes[1]->size == QSize(640, 360)
                  && p.scenes[2]->size == QSize(200, 100),
              "doi khung hinh: Scene dung theo khung cu doi theo, Scene co kich thuoc rieng giu nguyen");
        CHECK(ch->program == 0 && ch->preview == 1, "doi khung hinh khong doi Program/Preview");
        us->undo();
        CHECK(ch->size == QSize(400, 300) && p.scenes[0]->size == QSize(400, 300) && p.scenes[2]->size == QSize(200, 100), "Hoan tac khung hinh va kich thuoc Scene");
        s = w.screenSetup();
        CHECK(w.applyScreenSetup(s, {1}) && p.scenes[2]->size == QSize(400, 300), "\"dat moi Scene theo khung hinh\": ca Scene rieng cung doi");
        us->undo();
        CHECK(p.scenes[2]->size == QSize(200, 100), "Hoan tac");
        const int stack = us->count();
        CHECK(w.applyScreenSetup(w.screenSetup()) && us->count() == stack, "khong doi gi: khong them buoc Hoan tac");
        ScreenSetup bad = w.screenSetup();
        bad.outputs.clear();
        ScreenSetup bad2 = w.screenSetup();
        bad2.canvases[0] = {4, 4};
        CHECK(!w.applyScreenSetup(bad) && !w.applyScreenSetup(bad2) && us->count() == stack, "tu choi: khong con Output / khung hinh qua nho");

        // ---------- vung LED: diem anh that tren cua so ----------
        s = w.screenSetup();
        s.outputs[0].area = QRect(100, 50, 200, 100);
        s.canvases[0] = {200, 100};
        CHECK(w.applyScreenSetup(s), "dat vung LED 200x100 tai (100,50) trong tin hieu 400x300");
        auto* win = dynamic_cast<OutputWindow*>(engine.output(0));
        win->raise();
        pump(900);
        auto grab = [&] { return win->screen()->grabWindow(win->winId()).toImage(); };
        QImage g = grab();
        if (g.isNull() || g.width() < 100) {
            std::cout << "skip: khong chup duoc noi dung cua so GL\n";
        } else {
            const double kx = g.width() / 400.0, ky = g.height() / 300.0;
            auto px = [&](int x, int y) { return g.pixelColor(int(x * kx), int(y * ky)); };
            CHECK(near(px(200, 100), Qt::green) && near(px(104, 54), Qt::green) && near(px(296, 146), Qt::green), "hinh nam dung trong vung LED");
            CHECK(near(px(50, 100), Qt::black) && near(px(350, 100), Qt::black) && near(px(200, 20), Qt::black) && near(px(200, 200), Qt::black)
                      && near(px(96, 54), Qt::black) && near(px(200, 154), Qt::black),
                  "ngoai vung LED den han");
            engine.output(0)->testPattern = true;
            pump(500);
            g = grab();
            CHECK(near(px(101, 100), Qt::red) && near(px(298, 100), Qt::red) && near(px(50, 100), Qt::black), "mau thu co vien do sat mep vung LED, ngoai vung den");
            // thanh mau o TREN, dai xam o DUOI (mau thu khong duoc lon nguoc: toa do 4 goc phai dung)
            const QColor top = px(100 + 200 * 3 / 16, 58), bottom = px(100 + 200 * 3 / 16, 142);
            CHECK(top.red() > 200 && top.green() > 200 && top.blue() < 80 && std::abs(bottom.red() - bottom.blue()) < 40 && std::abs(bottom.red() - bottom.green()) < 40,
                  "mau thu dung chieu: thanh mau o tren, dai xam o duoi");
            if (qEnvironmentVariableIsSet("LC_SHOT_DIR")) g.save(qEnvironmentVariable("LC_SHOT_DIR") + "/led-area-pattern.png");
            engine.output(0)->testPattern = false;
        }
        CHECK(waitFor([&] { return w.findChild<QToolButton*>("outputWarn0")->isHidden(); }), "bang Outputs: 1:1 thi khong co dau canh bao");
        w.saveProject(false);
        pump(600);
        Project q;
        QString err;
        CHECK(loadProject(q, p.filePath, &err) && q.outputs[0].area == QRect(100, 50, 200, 100) && q.channels[0]->size == QSize(200, 100),
              "luu tu giao dien: giu vung LED va khung hinh");
        us->undo();
        pump(200);
        CHECK(engine.output(0)->config().area.isEmpty() && ch->size == QSize(400, 300), "Hoan tac vung LED");
        CHECK(waitFor([&] { return !w.findChild<QToolButton*>("outputWarn0")->isHidden(); }) && w.findChild<QToolButton*>("outputWarn0")->toolTip().contains("Rieng"),
              "bang Outputs hien canh bao (Scene khac khung hinh)");

        // ---------- hop thoai ----------
        {
            std::unique_ptr<ScreenDialog> dlg(w.createScreenDialog());
            dlg->show();
            pump(200);
            auto spin = [&](const char* name) { return dlg->findChild<QSpinBox*>(name); };
            CHECK(dlg->draft().outputs.size() == 1 && spin("sdCanvasW")->value() == 400 && dlg->warnings().size() == 1 && dlg->warnings()[0].contains("Rieng"),
                  "hop thoai doc trang thai dang chay (chi canh bao Scene co kich thuoc rieng)");
            spin("sdCanvasW")->setValue(1320);
            spin("sdCanvasH")->setValue(360);
            spin("sdW")->setValue(1920);
            spin("sdH")->setValue(1080);
            CHECK(dlg->warnings().join(" ").contains("phóng to") && dlg->findChild<QLabel*>("sdWarnings")->text().contains("phóng to"),
                  "canh bao hien ngay khi dang soan");
            CHECK(ch->size == QSize(400, 300) && engine.output(0)->config().size == QSize(400, 300), "chua bam Ap dung: chua doi gi");

            dlg->findChild<QGroupBox*>("sdAreaBox")->setChecked(true);
            dlg->findChild<QDoubleSpinBox*>("sdMetersW")->setValue(4);
            dlg->findChild<QDoubleSpinBox*>("sdMetersH")->setValue(2);
            CHECK(dlg->findChild<QLabel*>("sdLedPixels")->text().contains("1600 × 800"), "tinh diem anh: 4 m x 2 m P2.5 = 1600 x 800");
            dlg->findChild<QPushButton*>("sdFromMeters")->click();
            CHECK(dlg->draft().outputs[0].area == QRect(0, 0, 1600, 800) && dlg->draft().canvases[0] == QSize(1600, 800) && dlg->warnings().filter("phóng to").isEmpty()
                      && dlg->warnings().filter("viền đen").isEmpty(),
                  "vung LED + khung hinh = dung vung (1:1)");
            dlg->findChild<QPushButton*>("sdCenter")->click();
            CHECK(dlg->draft().outputs[0].area == QRect(160, 140, 1600, 800), "Giua khung");
            dlg->findChild<QPushButton*>("sdTopLeft")->click();
            CHECK(dlg->draft().outputs[0].area.topLeft() == QPoint(0, 0), "Goc tren trai");

            dlg->savePreset("Sự kiện A");
            spin("sdAreaW")->setValue(960);
            spin("sdAreaH")->setValue(540);
            dlg->savePreset("Sự kiện B");
            CHECK(dlg->presetNames().size() == 2 && dlg->loadPreset("Sự kiện A") && dlg->draft().outputs[0].area == QRect(0, 0, 1600, 800)
                      && dlg->draft().canvases[0] == QSize(1600, 800),
                  "cau hinh LED luu theo may: luu va dung lai");
            dlg->deletePreset("Sự kiện B");
            CHECK(dlg->presetNames() == QStringList{"Sự kiện A"} && !dlg->loadPreset("Sự kiện B"), "xoa cau hinh");

            if (qEnvironmentVariableIsSet("LC_SHOT_DIR")) dlg->grab().save(qEnvironmentVariable("LC_SHOT_DIR") + "/screen-dialog.png");

            CHECK(dlg->applyDraft(), "Ap dung");
            pump(300);
            CHECK(ch->size == QSize(1600, 800) && engine.output(0)->config().area == QRect(0, 0, 1600, 800) && engine.output(0)->config().size == QSize(1920, 1080)
                      && ch->program == 0 && ch->preview == 1,
                  "Ap dung: Output va khung hinh doi, Program/Preview khong doi");
            us->undo();
            pump(300);
            CHECK(ch->size == QSize(400, 300) && engine.output(0)->config().size == QSize(400, 300), "mot buoc Hoan tac tra ve nhu truoc");

            // them / xoa Output trong hop thoai
            dlg->reload();
            dlg->findChild<QToolButton*>("sdAdd")->click();
            CHECK(dlg->draft().outputs.size() == 2 && dlg->selectedOutput() == 1 && dlg->draft().outputs[1].size == QSize(1920, 1080), "nut Them Output");
            dlg->findChild<QToolButton*>("sdRemove")->click();
            CHECK(dlg->draft().outputs.size() == 1 && !dlg->findChild<QToolButton*>("sdRemove")->isEnabled(), "nut Xoa; con mot Output thi khong xoa duoc");

            // video wall kem khe
            spin("sdWallCols")->setValue(2);
            spin("sdWallRows")->setValue(1);
            spin("sdCellW")->setValue(320);
            spin("sdCellH")->setValue(180);
            spin("sdGapX")->setValue(10);
            dlg->findChild<QPushButton*>("sdWall")->click();
            WallLayout wall;
            CHECK(dlg->draft().outputs.size() == 2 && dlg->draft().canvases[0] == QSize(650, 180) && detectWall(dlg->draft().outputs, 0, {650, 180}, wall)
                      && wall.gap.width() == 10 && dlg->warnings().filter("một phần").isEmpty(),
                  "tao video wall 2x1 khe 10: 2 Output, khung hinh 650x180, khong canh bao cat");
            CHECK(dlg->applyDraft(), "Ap dung video wall");
            pump(500);
            CHECK(engine.outputCount() == 2 && ch->size == QSize(650, 180) && p.scenes[0]->size == QSize(650, 180), "wall chay that");
            us->undo();
            pump(300);
            CHECK(engine.outputCount() == 1 && ch->size == QSize(400, 300), "Hoan tac video wall");
        }

        // ---------- project moi ----------
        {
            NewProjectDialog nd(&w);
            nd.findChild<QSpinBox*>("npW")->setValue(960);
            nd.findChild<QSpinBox*>("npH")->setValue(540);
            Project np = nd.project();
            CHECK(np.channels[0]->size == QSize(960, 540) && np.outputs.size() == 1 && np.outputs[0].size == QSize(960, 540), "project moi: khung hinh da chon, mot Output");
            nd.findChild<QGroupBox*>("npWall")->setChecked(true);
            nd.findChild<QSpinBox*>("npCols")->setValue(3);
            nd.findChild<QSpinBox*>("npGapX")->setValue(20);
            np = nd.project();
            CHECK(np.outputs.size() == 3 && np.channels[0]->size == QSize(3 * 960 + 40, 540), "project moi kieu video wall kem khe");
            const QString path = dir + "/wall-moi.json";
            Project q;
            QString err;
            CHECK(w.createProject(path, np) && loadProject(q, path, &err) && q.outputs.size() == 3 && q.channels[0]->size == QSize(2920, 540), "ghi file project moi, mo lai duoc");
            CHECK(!w.createProject(p.filePath, np), "khong ghi de project dang mo");
            CHECK(ch->size == QSize(400, 300) && engine.outputCount() == 1, "tao project khong doi buoi chay hien tai");
        }

        // ---------- khoa van hanh ----------
        w.setLocked(true);
        const int cnt = us->count();
        ScreenSetup locked = w.screenSetup();
        locked.canvases[0] = {800, 600};
        CHECK(!w.applyScreenSetup(locked) && ch->size == QSize(400, 300) && us->count() == cnt, "khoa: khong doi duoc man hinh / khung hinh");
        CHECK(!w.findChild<QToolButton*>("screenSetupBtn")->isEnabled() && !w.createProject(dir + "/x.json", makeNewProject({100, 100})), "khoa: nut thiet lap va project moi bi chan");
        CHECK(!w.addNdiOutput("N", 0, {64, 36}, false), "khoa: khong them Output NDI");
        w.setLocked(false);

        if (qEnvironmentVariableIsSet("LC_SHOT_DIR")) { pump(700); w.grab().save(qEnvironmentVariable("LC_SHOT_DIR") + "/screens-main.png"); }
    }
    engine.stopEngine();
}

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);                    // khong lam ban cau hinh that cua may
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.filePath("cfg"));
    qputenv("LUMACAST_CONFIG_DIR", dir.filePath("config").toUtf8());
    QApplication app(argc, argv);
    applyDarkTheme(app);
    if (!appSettings().fileName().startsWith(dir.path())) { std::cerr << "cai dat khong duoc co lap\n"; return 1; }

    testWarnings();
    testProject(dir.path());
    testWindow(dir.path());

    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
