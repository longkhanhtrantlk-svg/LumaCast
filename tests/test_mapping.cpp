// Test Mapping truc quan: hinh hoc (diem anh Channel) va cua so mapping dieu khien nhu nguoi dung (chuot, phim, o so),
// kiem tra ket qua o render thread, Hoan tac, Khoa van hanh va file project.
#include "InspectorPanel.h"
#include "MainWindow.h"
#include "MappingWindow.h"
#include "media/StaticSources.h"
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
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
    while (t.elapsed() < ms) {
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(5);
    }
}
static void mouse(QWidget* w, QEvent::Type type, QPointF pos, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    const Qt::MouseButtons held = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent e(type, pos, w->mapToGlobal(pos), button, held, mods);
    QApplication::sendEvent(w, &e);
}
static void key(QWidget* w, int k, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QKeyEvent e(QEvent::KeyPress, k, mods);
    QApplication::sendEvent(w, &e);
    QKeyEvent r(QEvent::KeyRelease, k, mods);
    QApplication::sendEvent(w, &r);
}
// Keo tu diem `from` toi `to` (toa do diem anh Channel), qua mot diem giua de kiem tra gop buoc Hoan tac.
static void dragPx(MappingCanvas* c, QPointF from, QPointF to) {
    mouse(c, QEvent::MouseButtonPress, c->toWidget(from));
    mouse(c, QEvent::MouseMove, c->toWidget((from + to) / 2));
    mouse(c, QEvent::MouseMove, c->toWidget(to));
    mouse(c, QEvent::MouseButtonRelease, c->toWidget(to));
}

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);
    QTemporaryDir dir;
    // appSettings() theo QSettings::defaultFormat(): phai dat CA HAI, bien moi truong chi main.cpp moi doc
    QSettings::setDefaultFormat(QSettings::IniFormat);                    // khong lam ban cau hinh that cua may
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.filePath("cfg"));
    qputenv("LUMACAST_CONFIG_DIR", dir.filePath("config").toUtf8());     // log + tien trinh con
    QApplication app(argc, argv);
    const QSize ch1920(1920, 1080);

    // ---------- hinh hoc ----------
    {
        using namespace mapping;
        const QRect left(0, 0, 960, 1080), right(960, 0, 960, 1080);
        CHECK(toPixels(toCrop(QRect(1, 7, 333, 721), ch1920), ch1920) == QRect(1, 7, 333, 721), "doi diem anh <-> toa do chuan hoa khong troi lech");
        CHECK(drag(left, Move, -50, 0, ch1920, {}, 0) == left, "di chuyen: khong ra ngoai mep trai");
        CHECK(drag(left, Move, 5000, 5000, ch1920, {}, 0) == right, "di chuyen: dung o mep phai/duoi, giu nguyen co");
        CHECK(drag(left, Right, -5000, 0, ch1920, {}, 0) == QRect(0, 0, 1, 1080), "keo mep phai qua mep trai: con 1 px");
        CHECK(drag(right, Left, 5000, 0, ch1920, {}, 0) == QRect(1919, 0, 1, 1080), "keo mep trai qua mep phai: con 1 px");
        CHECK(drag(left, Right | Bottom, 100, -80, ch1920, {}, 0) == QRect(0, 0, 1060, 1000), "keo goc doi ca rong va cao");
        CHECK(drag(QRect(100, 0, 500, 500), Move, 355, 0, ch1920, {right}, 10) == QRect(460, 0, 500, 500), "bat dinh: mep phai vao mep Output ben canh");
        CHECK(drag(QRect(0, 0, 800, 1080), Right, 155, 0, ch1920, {right}, 10) == left, "bat dinh khi doi co");
        CHECK(drag(QRect(100, 0, 500, 500), Move, 340, 0, ch1920, {right}, 10).x() == 440, "ngoai khoang bat dinh: giu nguyen");
    }

    Project p;
    SourceDef a; a.type = "color"; a.color = Qt::darkRed; a.label = "do";
    SourceDef b; b.type = "color"; b.color = Qt::darkBlue; b.label = "xanh";
    auto sa = p.addSource(a), sb = p.addSource(b);
    for (auto src : {sa, sb}) {
        auto s = std::make_shared<Scene>();
        s->name = src->name(); s->size = {64, 36};
        s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        p.scenes.push_back(s);
    }
    auto ch = std::make_shared<Channel>();
    ch->name = "K"; ch->size = ch1920; ch->scenes = p.scenes; ch->program = 0; ch->preview = 1;
    p.channels = {ch};
    OutputConfig o1; o1.name = "LED trai"; o1.size = {320, 180}; o1.crop = QRectF(0, 0, 0.5, 1);
    OutputConfig o2; o2.name = "LED phai"; o2.size = {320, 180}; o2.crop = QRectF(0.5, 0, 0.5, 1);
    p.outputs = {o1, o2};
    p.filePath = dir.filePath("map.json");

    RenderEngine engine(p);
    engine.startEngine();
    MainWindow w(p, engine, {});
    w.setAskOnClose(false);
    w.show();
    pump(800);

    auto* openB = w.findChild<QPushButton*>("openMapping");
    CHECK(openB, "tab Output co nut mo Mapping");
    openB->click();
    pump(400);
    MappingWindow* mw = w.mappingWindow();
    CHECK(mw && mw->isVisible(), "bam nut: cua so Mapping hien ra");
    MappingCanvas* cv = mw->canvas();
    auto* in = w.inspector();
    auto px = [&](int i) { return mapping::toPixels(in->outputConfig(i).crop, ch1920); };
    auto live = [&](int i) { pump(150); return mapping::toPixels(engine.output(i)->config().crop, ch1920); };
    auto* snapBox = mw->findChild<QCheckBox*>();
    QUndoStack* undo = w.undoStack();
    CHECK(cv->selected() == 0, "mac dinh chon Output dau tien cua kenh");
    snapBox->setChecked(false);

    // ---------- keo doi co / di chuyen ----------
    int n = undo->count();
    dragPx(cv, {960, 540}, {480, 540});                          // mep phai cua LED trai
    CHECK(cv->selected() == 0 && px(0) == QRect(0, 0, 480, 1080), "keo mep phai: vung con 480 px");
    CHECK(live(0) == QRect(0, 0, 480, 1080), "render thread nhan vung moi");
    CHECK(undo->count() == n + 1, "mot lan keo = mot buoc Hoan tac");
    undo->undo();
    CHECK(px(0) == QRect(0, 0, 960, 1080) && live(0) == QRect(0, 0, 960, 1080), "Hoan tac tra lai vung cu");
    undo->redo();
    CHECK(px(0) == QRect(0, 0, 480, 1080), "Lam lai");
    dragPx(cv, {240, 540}, {440, 540});
    CHECK(px(0) == QRect(200, 0, 480, 1080), "keo giua vung: di chuyen");
    CHECK(px(1) == QRect(960, 0, 960, 1080), "Output khac khong bi anh huong");
    dragPx(cv, {1440, 540}, {1440, 540});
    CHECK(cv->selected() == 1, "bam vao vung khac de chon");
    CHECK(undo->count() == n + 2, "chi bam chon thi khong them buoc Hoan tac");
    cv->select(0);

    // ---------- huy / quay ve cho cu ----------
    n = undo->index();
    mouse(cv, QEvent::MouseButtonPress, cv->toWidget({440, 540}));
    mouse(cv, QEvent::MouseMove, cv->toWidget({640, 540}));
    CHECK(px(0).x() == 400, "dang keo: ap dung ngay");
    key(cv, Qt::Key_Escape);
    mouse(cv, QEvent::MouseButtonRelease, cv->toWidget({640, 540}));
    CHECK(px(0) == QRect(200, 0, 480, 1080) && undo->index() == n, "Esc huy lan keo, khong de lai buoc Hoan tac");
    mouse(cv, QEvent::MouseButtonPress, cv->toWidget({440, 540}));
    mouse(cv, QEvent::MouseMove, cv->toWidget({640, 540}));
    mouse(cv, QEvent::MouseMove, cv->toWidget({440, 540}));
    mouse(cv, QEvent::MouseButtonRelease, cv->toWidget({440, 540}));
    CHECK(px(0) == QRect(200, 0, 480, 1080) && undo->index() == n, "keo roi tra ve cho cu: khong de lai buoc Hoan tac");

    // ---------- bat dinh, mui ten ----------
    snapBox->setChecked(true);
    dragPx(cv, {440, 540}, {715, 540});                          // mep phai den 955 -> dinh vao 960
    CHECK(px(0) == QRect(480, 0, 480, 1080), "bat dinh vao mep Output ben canh");
    snapBox->setChecked(false);
    key(cv, Qt::Key_Left);
    CHECK(px(0).x() == 479, "mui ten trai: nhich 1 px");
    key(cv, Qt::Key_Left, Qt::ShiftModifier);
    CHECK(px(0).x() == 469, "Shift + mui ten: nhich 10 px");

    // ---------- o so, nut, tab Output ----------
    auto* sx = mw->findChild<QSpinBox*>("mapX");
    auto* sw = mw->findChild<QSpinBox*>("mapW");
    auto* sh = mw->findChild<QSpinBox*>("mapH");
    CHECK(sx->value() == 469 && sw->value() == 480 && sh->value() == 1080, "o so hien dung diem anh");
    sw->setValue(640);
    CHECK(px(0) == QRect(469, 0, 640, 1080), "nhap Rong = 640");
    QDoubleSpinBox* tabW = nullptr;
    for (auto* s : in->findChildren<QDoubleSpinBox*>())
        if (s->prefix() == "w ") { tabW = s; break; }
    CHECK(tabW && std::abs(tabW->value() - 640.0 / 1920) < 0.001, "o crop o tab Output cap nhat theo");
    tabW->setValue(0.25);
    CHECK(sw->value() == 480, "sua o tab Output: cua so Mapping cap nhat theo");

    auto* one = mw->findChild<QPushButton*>("mapOneToOne");
    auto* full = mw->findChild<QPushButton*>("mapFull");
    auto* info = mw->findChild<QLabel*>("mapInfo");
    one->click();
    CHECK(px(0).size() == mapping::outputPixels(in->outputConfig(0)), "Khop 1:1: co vung = so diem anh Output");
    CHECK(info->text().contains("đúng điểm ảnh"), "dong thong tin bao 1:1");
    full->click();
    CHECK(px(0) == QRect(0, 0, 1920, 1080) && info->text().contains("co giãn"), "Ca khung; bao dang bi co gian");
    undo->undo();
    CHECK(px(0).size() == mapping::outputPixels(in->outputConfig(0)), "Hoan tac nut Ca khung");

    // ---------- Khoa van hanh ----------
    w.setLocked(true);
    const QRect before = px(0);
    n = undo->count();
    dragPx(cv, QRectF(before).center(), QRectF(before).center() + QPointF(300, 0));
    key(cv, Qt::Key_Right);
    full->click();
    CHECK(px(0) == before && undo->count() == n, "khoa: keo / phim / nut khong sua duoc");
    CHECK(!sw->isEnabled() && !one->isEnabled() && mw->findChild<QLabel*>("mapNote")->text().contains("KHOÁ"), "khoa: o so va nut bi mo, co canh bao");
    w.setLocked(false);
    CHECK(sw->isEnabled(), "mo khoa: sua lai duoc");

    // ---------- phim tat cua cua so chinh van chay khi cua so Mapping duoc chon ----------
    mw->activateWindow();
    cv->setFocus();
    pump(300);
    if (QApplication::activeWindow() == mw) {
        const int prog = ch->program;
        key(cv, Qt::Key_Space);
        QElapsedTimer t; t.start();
        while (ch->program == prog && t.elapsed() < 4000) pump(50);
        CHECK(ch->program != prog, "Space trong cua so Mapping = TAKE");
        pump(300);
        const int prog2 = ch->program;
        sw->setFocus();
        key(sw, Qt::Key_Return);
        pump(500);
        CHECK(ch->program == prog2, "Enter trong o so chi xac nhan so, khong CUT");
    } else {
        std::cout << "bo qua: cua so Mapping khong duoc he dieu hanh kich hoat, khong thu phim tat\n";
    }

    // ---------- luu project ----------
    const QRect saved0 = px(0), saved1 = px(1);
    w.saveProject(false);
    pump(800);                                                   // luu chay bat dong bo tren render thread
    Project q;
    QString err;
    const bool loaded = loadProject(q, p.filePath, &err);
    CHECK(loaded && q.outputs.size() == 2, "mo lai project: " + err.toStdString() + " / outputs " + std::to_string(q.outputs.size()));
    if (q.outputs.size() == 2) CHECK(mapping::toPixels(q.outputs[0].crop, ch1920) == saved0 && mapping::toPixels(q.outputs[1].crop, ch1920) == saved1, "vung mapping duoc luu dung tung diem anh");

    engine.stopEngine();
    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
