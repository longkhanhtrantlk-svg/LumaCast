// Test UI: dieu khien MainWindow nhu nguoi dung (bam nut, double-click, doi gia tri) va kiem tra
// ket qua o render thread / file da luu.
#include "InspectorPanel.h"
#include "MainWindow.h"
#include "MonitorWidget.h"
#include "TimelineWidget.h"
#include "media/StaticSources.h"
#include "drag.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QUndoStack>
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
    while (t.elapsed() < ms) {
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);     // nhu vong lap su kien that: xu ly deleteLater()
        QThread::msleep(5);
    }
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

    // plug-in tam: 1 Effect tot, 1 Transition tot, 1 plug-in hong
    QTemporaryDir plugDir;
    auto writePlugin = [&](const QString& sub, const QString& manifest, const QString& file, const QString& shader) {
        QDir().mkpath(plugDir.path() + "/" + sub);
        QFile m(plugDir.path() + "/" + sub + "/plugin.json"); m.open(QIODevice::WriteOnly | QIODevice::Truncate); m.write(manifest.toUtf8()); m.close();
        if (!file.isEmpty()) { QFile f(plugDir.path() + "/" + sub + "/" + file); f.open(QIODevice::WriteOnly | QIODevice::Truncate); f.write(shader.toUtf8()); f.close(); }
    };
    const QString invertShader = "vec4 effect(vec2 uv){ vec4 c = sampleInput(uv); return vec4(mix(c.rgb, 1.0 - c.rgb, amount), c.a); }";
    writePlugin("invert", R"J({"id":"com.ui.invert","name":"Dao mau (test)","apiVersion":1,"type":"effect","version":"1.2","author":"Test","entry":"fx.frag",
        "params":[{"name":"amount","label":"Muc do","type":"float","min":0,"max":1,"default":1}]})J", "fx.frag", invertShader);
    writePlugin("sweep", R"J({"id":"com.ui.sweep","name":"Quet (test)","apiVersion":1,"type":"transition","entry":"t.frag",
        "params":[{"name":"soft","type":"float","min":0.01,"max":0.5,"default":0.1}]})J", "t.frag",
        "vec4 transition(vec2 uv){ return mix(sampleFrom(uv), sampleTo(uv), step(uv.x, uProgress)); }");
    writePlugin("hong", R"J({"id":"com.ui.hong","apiVersion":9,"type":"effect","entry":"x.frag"})J", "x.frag", "x");

    RenderEngine engine(p);
    engine.startEngine();
    MainWindow w(p, engine, {plugDir.path()});
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
    CHECK(scenes && scenes->count() == 2 && !w.findChild<QListWidget*>("mediaList"), "Danh sach Scenes co du muc; khong con Media Pool");

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
    for (auto* t : w.findChildren<QToolButton*>("outFreeze0")) { freeze = t; break; }
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
    insp->findChild<QPushButton*>("layerAdd")->click();
    pump(200);
    CHECK(p.scenes[0]->layers.size() == 2, "Them layer");
    insp->findChild<QPushButton*>("layerDelete")->click();
    pump(200);
    CHECK(p.scenes[0]->layers.size() == 1, "Xoa layer vua them");
    byPrefix(insp, "sáng ")->setValue(0.5);
    pump(200);
    CHECK(std::abs(engine.output(0)->config().cal.brightness - 0.5f) < 1e-4, "Inspector: doi 'sang' -> Output.cal.brightness = 0.5");


    // ---- Undo / Redo ----
    auto* us = w.undoStack();
    alpha->setValue(0.2); alpha->setValue(0.3); alpha->setValue(0.4);     // ba chinh sua lien tiep cung mot truong
    pump(200);
    CHECK(std::abs(p.scenes[0]->layers[0].opacity - 0.4f) < 1e-4, "Doi alpha lien tiep: ap dung gia tri cuoi");
    us->undo();
    pump(200);
    CHECK(std::abs(p.scenes[0]->layers[0].opacity - 0.5f) < 1e-4 && std::abs(alpha->value() - 0.5) < 1e-4,
          "Hoan tac MOT lan: ca 3 lan keo gop thanh mot buoc, ve 0.5, o nhap cung cap nhat");
    us->redo();
    pump(200);
    CHECK(std::abs(p.scenes[0]->layers[0].opacity - 0.4f) < 1e-4 && std::abs(alpha->value() - 0.4) < 1e-4, "Lam lai: 0.4");

    auto* bright = byPrefix(insp, "sáng ");
    bright->setValue(0.8);
    pump(200);
    CHECK(std::abs(engine.output(0)->config().cal.brightness - 0.8f) < 1e-4, "Output: doi sang = 0.8");
    us->undo();
    pump(200);
    CHECK(std::abs(engine.output(0)->config().cal.brightness - 0.5f) < 1e-4 && std::abs(bright->value() - 0.5) < 1e-4,
          "Hoan tac hieu chinh mau Output (engine + o nhap)");
    us->redo();
    pump(100);

    // ---- Layer: an/hien, doi thu tu (keo-tha), them/xoa deu hoan tac duoc ----
    auto* ll = insp->layerList();
    insp->findChild<QPushButton*>("layerAdd")->click();
    pump(200);
    CHECK(p.scenes[0]->layers.size() == 2 && ll->count() == 2 && ll->currentRow() == 0, "Them layer: layer moi o tren cung va duoc chon");
    alpha->setValue(0.3);                                  // danh dau layer moi (index 1)
    pump(200);
    CHECK(std::abs(p.scenes[0]->layers[1].opacity - 0.3f) < 1e-4 && std::abs(p.scenes[0]->layers[0].opacity - 0.4f) < 1e-4,
          "Thuoc tinh ap dung cho layer dang chon");
    testdrag::dragRow(ll, 0, 2);                           // keo layer tren cung xuong duoi
    pump(300);
    CHECK(std::abs(p.scenes[0]->layers[0].opacity - 0.3f) < 1e-4 && std::abs(p.scenes[0]->layers[1].opacity - 0.4f) < 1e-4,
          "Keo-tha doi thu tu layer -> engine nhan thu tu moi");
    us->undo();
    pump(200);
    CHECK(std::abs(p.scenes[0]->layers[1].opacity - 0.3f) < 1e-4, "Hoan tac doi thu tu");
    ll->item(1)->setCheckState(Qt::Unchecked);                      // an layer goc (hang duoi)
    pump(200);
    CHECK(!p.scenes[0]->layers[0].visible, "Bo tick = an layer");
    us->undo();
    pump(200);
    CHECK(p.scenes[0]->layers[0].visible && ll->item(1)->checkState() == Qt::Checked, "Hoan tac an layer");
    us->undo();                                                     // hoan tac alpha 0.3 cua layer moi
    us->undo();                                                     // hoan tac them layer
    pump(300);
    CHECK(p.scenes[0]->layers.size() == 1 && ll->count() == 1, "Hoan tac them layer -> con 1 layer");
    us->redo(); us->redo();
    pump(300);
    CHECK(p.scenes[0]->layers.size() == 2, "Lam lai them layer");
    us->undo(); us->undo();
    pump(200);

    bool hasUndo = false, hasRedo = false;
    for (QAction* a : w.findChildren<QAction*>()) {
        hasUndo = hasUndo || a->text().startsWith("Hoàn tác");
        hasRedo = hasRedo || a->text().startsWith("Làm lại");
    }
    CHECK(hasUndo && hasRedo, "Menu Edit co Hoan tac / Lam lai");

    // ---- T-bar ----
    auto* tbar = w.findChild<QSlider*>("tbar");
    CHECK(tbar != nullptr, "Co T-bar");
    const int pg0 = ch->program;
    tbar->setValue(500);
    pump(300);
    CHECK(ch->manualActive() && ch->inTransition() && ch->program == pg0, "Keo T-bar 50%: dang chuyen, Program chua doi");
    tbar->setValue(0);
    pump(300);
    CHECK(!ch->inTransition() && ch->program == pg0, "Keo T-bar ve 0: huy, Program giu nguyen");
    tbar->setValue(400);
    pump(100);
    tbar->setValue(1000);
    pump(900);
    CHECK(ch->program != pg0 && !ch->manualActive() && tbar->value() == 0, "Keo het T-bar: Program doi, T-bar tu ve 0");

    // ---- AUTO ----
    auto* autoBtn = button(&w, "AUTO");
    autoBtn->click();
    const int target = ch->program == 0 ? 1 : 0;
    emit scenes->itemClicked(scenes->item(target));
    pump(300);
    CHECK(ch->inTransition(), "AUTO bat: bam Scene = chay transition ngay");
    pump(1400);
    CHECK(ch->program == target && !ch->inTransition(), "AUTO: Scene da len Program");
    autoBtn->click();
    const int target2 = ch->program == 0 ? 1 : 0;
    emit scenes->itemClicked(scenes->item(target2));
    pump(300);
    CHECK(!ch->inTransition() && ch->program != target2 && ch->preview == target2, "AUTO tat: bam Scene chi vao Preview");


    // ---- M4a: Plugin Manager, chon transition, tab Effect ----
    {
        auto* plist = w.findChild<QListWidget*>("pluginList");
        QStringList texts;
        for (int i = 0; i < plist->count(); ++i) texts << plist->item(i)->text();
        const QString all = texts.join("\n");
        CHECK(all.contains("Làm mờ") && all.contains("dựng sẵn") && all.contains("Dao mau (test)") && all.contains("Quet (test)"),
              "Plugin Manager: liet ke Effect/Transition dung san va plug-in nguoi dung");
        CHECK(all.contains("⚠") && all.contains("apiVersion 9"), "Plugin Manager: plug-in hong hien kem ly do (apiVersion 9)");

        auto* tc = w.findChild<QComboBox*>("transCombo");
        auto itemIdx = [&](const QString& key) { return tc->findData(key); };
        CHECK(itemIdx("p:builtin.push") >= 0 && itemIdx("p:builtin.luma") >= 0 && itemIdx("p:com.ui.sweep") >= 0 && itemIdx("s") >= 0,
              "Thanh chuyen co Push, Luma wipe, plug-in nguoi dung va Stinger");

        // TAKE bang transition plug-in dung san (Push) + easing
        emit scenes->itemDoubleClicked(scenes->item(0));
        pump(250);
        emit scenes->itemClicked(scenes->item(1));
        pump(250);
        auto* dur = w.findChild<QDoubleSpinBox*>("transDuration");
        dur->setValue(0.4);
        tc->setCurrentIndex(itemIdx("p:builtin.push"));
        w.findChild<QComboBox*>("easingCombo")->setCurrentIndex(1);        // Tuyen tinh
        TransitionSpec sp = w.currentSpec();
        CHECK(sp.type == TransitionType::Plugin && sp.pluginId == "builtin.push" && sp.easing == Easing::Linear && std::abs(sp.duration - 0.4) < 1e-9,
              "currentSpec(): Plugin + id + easing + thoi luong theo thanh chuyen");
        CHECK(!w.findChild<QPushButton*>("transParams")->isHidden(), "Push co tham so (huong) -> hien nut 'Tham so...'");
        const int pgBefore = ch->program;
        button(&w, "TAKE")->click();
        pump(150);
        CHECK(ch->inTransition(), "TAKE voi Push: dang chuyen");
        pump(700);
        CHECK(!ch->inTransition() && ch->program != pgBefore, "Push xong: Program doi");
        CHECK(engine.registry().compileError("builtin.push").isEmpty(), "Push bien dich tren GPU khong loi");

        tc->setCurrentIndex(itemIdx("s"));
        sp = w.currentSpec();
        CHECK(sp.type == TransitionType::Stinger && !sp.stinger && w.findChild<QComboBox*>("stingerCombo")->currentText().contains("chưa có video"),
              "Stinger khi chua co video: bao '(chua co video)', spec khong co nguon (engine roi ve Fade)");
        tc->setCurrentIndex(0);
    }
    {
        // dua scene 0 len Program roi gan Effect vao layer cua no
        emit scenes->itemDoubleClicked(scenes->item(0));
        pump(300);
        insp->setCurrentIndex(1);                                        // tab Effect
        auto* plist = w.findChild<QListWidget*>("pluginList");
        auto* fl = insp->effectList();
        auto* addCombo = insp->findChild<QComboBox*>("effectAddCombo");
        CHECK(fl && addCombo && addCombo->findData("builtin.blur") >= 0 && addCombo->findData("com.ui.invert") >= 0 &&
                  addCombo->findData("builtin.push") < 0, "Tab Effect: danh sach them gom Effect dung san + plug-in, khong lan Transition");
        const size_t scene0 = 0;
        addCombo->setCurrentIndex(addCombo->findData("builtin.colorgrade"));
        insp->findChild<QPushButton*>("effectAdd")->click();
        pump(300);
        CHECK(p.scenes[scene0]->layers[0].effects.size() == 1 && p.scenes[scene0]->layers[0].effects[0].effectId == "builtin.colorgrade" &&
                  fl->count() == 1 && fl->currentRow() == 0, "Them Effect vao layer: engine nhan duoc, danh sach cap nhat");
        auto* sat = insp->findChild<QDoubleSpinBox*>("param_saturation");
        CHECK(sat != nullptr && std::abs(sat->value() - 1.0) < 1e-9, "Bo chinh tham so tu sinh: o 'saturation' co gia tri mac dinh 1");
        sat->setValue(0.0);
        pump(900);
        const QColor px = engine.monitor(0).program.pixelColor(5, 5);
        CHECK(std::abs(p.scenes[scene0]->layers[0].effects[0].params["saturation"].toDouble()) < 1e-9 &&
                  std::abs(px.red() - px.green()) <= 6 && std::abs(px.green() - px.blue()) <= 6,
              "Doi tham so -> Program tren monitor chuyen sang xam (Effect that su chay tren GPU)");
        us->undo();
        pump(300);
        CHECK(std::abs(p.scenes[scene0]->layers[0].effects[0].params["saturation"].toDouble() - 1.0) < 1e-9 &&
                  std::abs(insp->findChild<QDoubleSpinBox*>("param_saturation")->value() - 1.0) < 1e-9, "Hoan tac tham so Effect (engine + o nhap)");
        us->redo();
        pump(200);

        fl->item(0)->setCheckState(Qt::Unchecked);
        pump(200);
        CHECK(!p.scenes[scene0]->layers[0].effects[0].enabled, "Bo tick Effect = tat");
        fl->item(0)->setCheckState(Qt::Checked);
        pump(200);

        // plug-in nguoi dung + tham so + nap lai nong + tat plug-in
        addCombo->setCurrentIndex(addCombo->findData("com.ui.invert"));
        insp->findChild<QPushButton*>("effectAdd")->click();
        pump(300);
        CHECK(fl->count() == 2 && insp->findChild<QDoubleSpinBox*>("param_amount") != nullptr, "Them Effect cua plug-in: tham so (amount) tu sinh tu manifest");
        const int rev1 = engine.registry().find("com.ui.invert")->revision;
        writePlugin("invert", R"J({"id":"com.ui.invert","name":"Dao mau (test)","apiVersion":1,"type":"effect","version":"1.3","author":"Test","entry":"fx.frag",
            "params":[{"name":"amount","label":"Muc do","type":"float","min":0,"max":1,"default":1}]})J", "fx.frag", invertShader + "\n// hot");
        QElapsedTimer tm; tm.start();
        while (engine.registry().find("com.ui.invert")->revision == rev1 && tm.elapsed() < 4000) pump(50);
        CHECK(engine.registry().find("com.ui.invert")->revision > rev1, "Sua file shader plug-in: tu nap lai (hot reload) qua giao dien");
        pump(300);

        for (int i = 0; i < plist->count(); ++i)
            if (plist->item(i)->data(Qt::UserRole).toString() == "com.ui.invert") plist->item(i)->setCheckState(Qt::Unchecked);
        pump(500);
        CHECK(!engine.registry().find("com.ui.invert") && fl->item(1)->text().startsWith("⚠ thiếu plug-in"),
              "Tat plug-in: Effect dung no hien '⚠ thieu plug-in' (khong mat du lieu)");
        CHECK(p.scenes[scene0]->layers[0].effects.size() == 2, "Du lieu Effect van con trong Scene");
        for (int i = 0; i < plist->count(); ++i)
            if (plist->item(i)->data(Qt::UserRole).toString() == "com.ui.invert") plist->item(i)->setCheckState(Qt::Checked);
        pump(500);
        CHECK(engine.registry().find("com.ui.invert") && fl->item(1)->text().startsWith("Dao mau"), "Bat lai plug-in: Effect hoat dong lai");

        fl->setCurrentRow(1);
        insp->findChild<QPushButton*>("effectDel")->click();
        pump(200);
        CHECK(p.scenes[scene0]->layers[0].effects.size() == 1, "Go Effect");
        us->undo();
        pump(200);
        CHECK(p.scenes[scene0]->layers[0].effects.size() == 2, "Hoan tac go Effect");
        us->redo();
        pump(200);
        // go het Effect de cac buoc sau (luu/mo, ...) khong bi anh huong
        fl->setCurrentRow(0);
        insp->findChild<QPushButton*>("effectDel")->click();
        pump(200);
        CHECK(p.scenes[scene0]->layers[0].effects.empty(), "Go het Effect");
        insp->setCurrentIndex(0);
    }


    // ---- M4b: thuoc tinh Layer moi (xoay, cat, blend) ----
    {
        emit scenes->itemDoubleClicked(scenes->item(0));
        pump(300);
        insp->setCurrentIndex(0);
        auto* rot = insp->findChild<QDoubleSpinBox*>("layerRot");
        auto* cropW = insp->findChild<QDoubleSpinBox*>("layerCropW");
        auto* blend = insp->findChild<QComboBox*>("layerBlend");
        CHECK(rot && cropW && blend && insp->findChild<QDoubleSpinBox*>("layerCropX") && blend->count() == 4, "Inspector co o xoay, cat x/y/w/h va chon che do hoa tron");
        rot->setValue(30);
        pump(200);
        CHECK(std::abs(p.scenes[0]->layers[0].rotation - 30.0f) < 1e-4, "Doi 'xoay' tren UI -> Layer.rotation = 30");
        us->undo();
        pump(200);
        CHECK(p.scenes[0]->layers[0].rotation == 0.0f && std::abs(rot->value()) < 1e-9, "Hoan tac xoay (engine + o nhap)");
        us->redo();
        pump(100);
        cropW->setValue(0.5);
        blend->setCurrentIndex(1);
        pump(200);
        CHECK(std::abs(p.scenes[0]->layers[0].crop.width() - 0.5) < 1e-9 && p.scenes[0]->layers[0].blend == BlendMode::Add, "Doi cat w = 0.5 va blend = Add");
        blend->setCurrentIndex(0);
        cropW->setValue(1.0);
        rot->setValue(0);
        pump(200);
        CHECK(p.scenes[0]->layers[0].blend == BlendMode::Normal && std::abs(p.scenes[0]->layers[0].crop.width() - 1.0) < 1e-9, "Tra ve mac dinh");
    }

    // ---- M4b: Timeline ----
    {
        auto* tl = w.timeline();
        auto* alphaSpin = insp->findChild<QDoubleSpinBox*>("layerAlpha");
        CHECK(w.findChild<QLabel*>("tlWarn")->text().contains("PROGRAM"), "Timeline canh bao khi Scene dang chon dang o Program (thao tac tac dong truc tiep len man hinh)");
        tl->setRestartOnAir(false);                                   // Scene vua len Program nen dang tu chay; dung lai de thu co kiem soat
        tl->pause();
        tl->seek(0.0);
        pump(300);
        CHECK(tl != nullptr && std::abs(tl->duration() - 10.0) < 1e-9 && !tl->playing(), "Co Timeline: thoi luong mac dinh 10 s, dang dung");
        auto layer = [&]() -> Layer& { return p.scenes[0]->layers[0]; };

        // dat keyframe opacity 0 -> 1 (them key roi sua gia tri tai playhead = tu dat key)
        tl->seek(0.0);
        pump(200);
        tl->addKey(AnimProp::Opacity, 0.0);
        tl->addKey(AnimProp::Opacity, 1.0);
        pump(200);
        const Track* ot = layer().track(AnimProp::Opacity);
        CHECK(ot && ot->keys.size() == 2 && ot->keys[0].time == 0.0 && ot->keys[1].time == 1.0, "Them 2 keyframe opacity tai 0 s va 1 s");
        CHECK(alphaSpin->styleSheet().contains("e0a526"), "O alpha co vien vang khi thuoc tinh co keyframe");
        alphaSpin->setValue(0.0);                                     // playhead = 0 -> sua key tai 0
        pump(200);
        tl->seek(1.0);
        pump(300);
        alphaSpin->setValue(1.0);                                     // playhead = 1 -> sua key tai 1
        pump(200);
        ot = layer().track(AnimProp::Opacity);
        CHECK(ot->keys.size() == 2 && std::abs(ot->keys[0].value) < 1e-9 && std::abs(ot->keys[1].value - 1.0) < 1e-9,
              "Sua gia tri o nhap khi thuoc tinh co keyframe: tu dat/sua KEYFRAME tai playhead (khong doi gia tri tinh)");
        CHECK(std::abs(layer().opacity - 0.4f) < 1e-4, "Gia tri tinh cua layer khong bi doi");

        // hinh Program that o cac thoi diem
        tl->seek(0.5);
        pump(900);
        QColor mid = engine.monitor(0).program.pixelColor(5, 5);
        CHECK(std::abs(mid.red() - 127) <= 10 && mid.green() < 10 && mid.blue() < 10, "t=0.5 s: Program tren monitor ~ do 50% (opacity 0.5 tren nen den)");
        tl->seek(0.0);
        pump(600);
        CHECK(engine.monitor(0).program.pixelColor(5, 5).red() < 10, "t=0: Program den (opacity 0)");
        tl->seek(1.0);
        pump(600);
        CHECK(engine.monitor(0).program.pixelColor(5, 5).red() > 245, "t=1 s: Program do day du (opacity 1)");

        // phat / tam dung / ve 0
        tl->seek(0.0);
        pump(300);
        tl->play();
        pump(600);
        CHECK(tl->playing() && p.scenes[0]->clock.time > 0.3, "Play: dong ho Scene chay");
        tl->pause();
        pump(300);
        const double held = p.scenes[0]->clock.time;
        pump(400);
        CHECK(!tl->playing() && std::abs(p.scenes[0]->clock.time - held) < 1e-9, "Pause: dong ho dung han");
        tl->stopTransport();
        pump(400);
        CHECK(p.scenes[0]->clock.time == 0.0 && !tl->playing(), "Ve 0");
        tl->setDuration(1.0);
        tl->setLoop(false);
        pump(200);
        tl->play();
        pump(1600);
        CHECK(!tl->playing() && std::abs(p.scenes[0]->clock.time - 1.0) < 1e-9, "Het thoi luong 1 s, khong lap: tu dung o cuoi");
        tl->setLoop(true);
        tl->setDuration(10.0);
        tl->stopTransport();
        pump(300);

        // thao tac keyframe: doi, doi kieu noi, xoa, hoan tac
        const size_t undoCount = us->count();
        const int mi = tl->moveKey(AnimProp::Opacity, 1, 0.5, false);
        pump(200);
        CHECK(mi == 1 && std::abs(layer().track(AnimProp::Opacity)->keys[1].time - 0.5) < 1e-9, "Doi keyframe tu 1 s ve 0.5 s");
        tl->setKeyInterp(AnimProp::Opacity, 1, Interp::Hold);
        pump(200);
        CHECK(layer().track(AnimProp::Opacity)->keys[1].interp == Interp::Hold, "Doi kieu noi sang Hold");
        us->undo();
        pump(200);
        CHECK(layer().track(AnimProp::Opacity)->keys[1].interp == Interp::Linear, "Hoan tac doi kieu noi");
        us->undo();
        pump(200);
        CHECK(std::abs(layer().track(AnimProp::Opacity)->keys[1].time - 1.0) < 1e-9, "Hoan tac doi keyframe: tro ve 1 s");
        us->redo(); us->redo();
        pump(200);
        CHECK(us->count() == undoCount + 2, "Moi thao tac keyframe la mot buoc trong ngan xep Hoan tac");
        tl->deleteKey(AnimProp::Opacity, 1);
        pump(200);
        CHECK(layer().track(AnimProp::Opacity)->keys.size() == 1, "Xoa keyframe");
        tl->deleteKey(AnimProp::Opacity, 0);
        pump(200);
        CHECK(!layer().track(AnimProp::Opacity) && std::abs(layer().opacity - 0.0f) < 1e-4,
              "Xoa key cuoi cung: track bi go, thuoc tinh giu gia tri cua key vua xoa (khong nhay hinh)");
        tl->seek(0.5);
        pump(300);
        tl->toggleKeyAtPlayhead(AnimProp::Rotation);
        pump(200);
        CHECK(layer().track(AnimProp::Rotation) && layer().track(AnimProp::Rotation)->keys.size() == 1 && std::abs(layer().track(AnimProp::Rotation)->keys[0].time - 0.5) < 0.01,
              "Nut ◆ (toggleKeyAtPlayhead): them key tai playhead");
        tl->toggleKeyAtPlayhead(AnimProp::Rotation);
        pump(200);
        CHECK(!layer().track(AnimProp::Rotation), "Bam lai: xoa key tai playhead");

        // chuot that tren TrackView: nhap doi them key, keo key, tua tren thuoc, Delete
        QWidget* tv = tl->trackView();
        auto sendMouse = [&](QEvent::Type type, QPoint pos, Qt::MouseButton btn = Qt::LeftButton) {
            QMouseEvent ev(type, QPointF(pos), QPointF(tv->mapToGlobal(pos)), btn, type == QEvent::MouseButtonRelease ? Qt::NoButton : btn, Qt::NoModifier);
            QApplication::sendEvent(tv, &ev);
        };
        const int rowRot = int(AnimProp::Rotation), rowX = int(AnimProp::X);
        const QPoint lane(tl->timeToX(2.0), tl->rowTop(rowX) + 10);
        sendMouse(QEvent::MouseButtonDblClick, lane);
        pump(200);
        CHECK(layer().track(AnimProp::X) && layer().track(AnimProp::X)->keys.size() == 1 && std::abs(layer().track(AnimProp::X)->keys[0].time - 2.0) < 0.06,
              "Nhap doi tren lan X: them keyframe dung thoi diem (lam tron 0.05 s)");
        const QPoint onKey(tl->timeToX(layer().track(AnimProp::X)->keys[0].time), tl->rowTop(rowX) + 12);
        sendMouse(QEvent::MouseButtonPress, onKey);
        sendMouse(QEvent::MouseMove, QPoint(tl->timeToX(4.0), onKey.y()));
        sendMouse(QEvent::MouseButtonRelease, QPoint(tl->timeToX(4.0), onKey.y()));
        pump(200);
        CHECK(std::abs(layer().track(AnimProp::X)->keys[0].time - 4.0) < 0.06, "Keo keyframe bang chuot den 4 s");
        const size_t afterDrag = us->count();
        sendMouse(QEvent::MouseButtonPress, QPoint(tl->timeToX(layer().track(AnimProp::X)->keys[0].time), onKey.y()));
        sendMouse(QEvent::MouseMove, QPoint(tl->timeToX(5.0), onKey.y()));
        sendMouse(QEvent::MouseMove, QPoint(tl->timeToX(6.0), onKey.y()));
        sendMouse(QEvent::MouseMove, QPoint(tl->timeToX(7.0), onKey.y()));
        sendMouse(QEvent::MouseButtonRelease, QPoint(tl->timeToX(7.0), onKey.y()));
        pump(200);
        CHECK(us->count() == afterDrag + 1 && std::abs(layer().track(AnimProp::X)->keys[0].time - 7.0) < 0.06, "Keo lien tuc mot keyframe = MOT buoc hoan tac (gop)");
        QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QApplication::sendEvent(tv, &del);
        pump(200);
        CHECK(!layer().track(AnimProp::X), "Phim Delete xoa keyframe dang chon");
        sendMouse(QEvent::MouseButtonPress, QPoint(tl->timeToX(3.0), 8));            // tren thuoc: tua
        sendMouse(QEvent::MouseButtonRelease, QPoint(tl->timeToX(3.0), 8));
        pump(300);
        CHECK(std::abs(p.scenes[0]->clock.time - 3.0) < 0.2, "Bam len thuoc thoi gian: playhead nhay den ~3 s");
        (void)rowRot;
        tl->seek(0.0);
        pump(200);
    }
    {
        // Scene tu phat lai khi len Program
        w.timeline()->setRestartOnAir(true);
        emit scenes->itemDoubleClicked(scenes->item(1));
        pump(300);
        p.scenes[0]->clock.stop();
        emit scenes->itemClicked(scenes->item(0));
        pump(200);
        button(&w, "CUT")->click();
        pump(300);
        CHECK(ch->program == 0 && p.scenes[0]->clock.playing, "Scene len Program (CUT): dong ho tu chay lai");
        w.timeline()->stopTransport();
        pump(300);
        w.timeline()->addKey(AnimProp::Rotation, 1.0);                 // de lai mot keyframe de kiem tra luu/mo o cuoi
        pump(200);
    }

    // ---- Nhap media -> Scene moi ----
    QImage img(32, 18, QImage::Format_RGBA8888); img.fill(Qt::green);
    const QString png = dir.filePath("g.png");
    img.save(png);
    CHECK(w.addMediaFile(png), "Nhap anh");
    CHECK(!w.addMediaFile(dir.filePath("khong_co.mp4")), "Nhap file khong ton tai -> bao loi, khong crash");
    pump(500);
    CHECK(p.scenes.size() == 3 && p.sources.size() == 3 && scenes->count() == 3 && ch->scenes.size() == 3,
          "Nhap media: them nguon + Scene, cap nhat danh sach Scenes");

    // ---- Luu / autosave ----
    w.saveProject(false);
    pump(600);
    CHECK(!QFileInfo::exists(p.filePath + ".autosave"), "Luu thanh cong -> xoa ban autosave cu");
    w.autosaveNow();
    pump(600);
    Project q;
    QString err;
    CHECK(loadProject(q, p.filePath, &err), "Project luu tu UI mo lai duoc: " + err.toStdString());
    CHECK(q.scenes.size() == 3 && q.sources.size() == 3 && std::abs(q.outputs[0].cal.brightness - 0.8f) < 1e-4 &&
              std::abs(q.scenes[0]->layers[0].opacity - 0.0f) < 1e-4, "File luu chua scene moi + hieu chinh mau + alpha sau Undo/Redo");
    {
        const Layer& sl = q.scenes[0]->layers[0];
        CHECK(sl.track(AnimProp::Rotation) && sl.track(AnimProp::Rotation)->keys.size() == 1 && std::abs(sl.track(AnimProp::Rotation)->keys[0].time - 1.0) < 1e-9 &&
                  std::abs(q.scenes[0]->duration - 10.0) < 1e-9 && q.scenes[0]->restartOnAir, "File luu chua keyframe tao tu giao dien + thuoc tinh Scene");
    }
    Project r;
    CHECK(loadProject(r, p.filePath + ".autosave", &err) && r.scenes.size() == 3, "Autosave: file duoc tao va mo lai duoc");

    engine.stopEngine();
    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
