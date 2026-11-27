// Test Multiview (UI-06) tren MainWindow + engine that: luoi o, anh thu nho, danh dau On Air/Preview, bam chuot (khong bao gio
// len Program), phim tat cua cua so chinh khi Multiview duoc chon, toan man hinh (ca viec tu choi man hinh co Output toan man hinh).
#include "MainWindow.h"
#include "MultiviewWindow.h"
#include "Theme.h"
#include "core/AppSettings.h"
#include "media/StaticSources.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QImage>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUndoStack>
#include <algorithm>
#include <iostream>

using namespace lc;
using MV = MultiviewWindow;

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
    while (t.elapsed() < ms) { pump(20); if (cond()) return true; }
    return cond();
}

static const MV::Tile* tile(const MV* mv, MV::Kind kind, int channel, int scene = -1) {
    for (const auto& t : mv->tiles())
        if (t.kind == kind && t.channel == channel && (scene < 0 || t.scene == scene)) return &t;
    return nullptr;
}
static int sceneTiles(const MV* mv) {
    int n = 0;
    for (const auto& t : mv->tiles()) n += t.kind == MV::Kind::Scene;
    return n;
}
static bool near(const QImage& img, QColor c) {
    if (img.isNull()) return false;
    const QColor p = img.pixelColor(img.width() / 2, img.height() / 2);
    return std::abs(p.red() - c.red()) < 8 && std::abs(p.green() - c.green()) < 8 && std::abs(p.blue() - c.blue()) < 8;
}
static void click(MV* mv, const MV::Tile* t, Qt::MouseButton button = Qt::LeftButton, QEvent::Type type = QEvent::MouseButtonPress) {
    if (!t) return;
    const QPointF pos = QRectF(t->rect).center();
    QMouseEvent e(type, pos, mv->mapToGlobal(pos), button, button, Qt::NoModifier);
    QApplication::sendEvent(mv, &e);
}
static void makeActive(QWidget* w) {
    // Kich hoat theo logic cua Qt (khong phu thuoc He dieu hanh co cho tien trinh test chiem tieu diem hay khong).
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow(w);
    QT_WARNING_POP
}

static std::shared_ptr<Scene> sceneOf(const std::shared_ptr<Source>& src) {
    auto s = std::make_shared<Scene>();
    s->name = src->name();
    s->size = {640, 360};
    s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
    return s;
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

    const QColor red(255, 0, 0), blue(0, 0, 255), green(0, 255, 0), yellow(255, 255, 0);
    Project p;
    for (const auto& [color, label] : {std::pair{red, "do"}, std::pair{blue, "xanh"}, std::pair{green, "la"}}) {
        SourceDef d; d.type = "color"; d.color = color; d.label = label;
        p.scenes.push_back(sceneOf(p.addSource(d)));
    }
    for (int c = 0; c < 2; ++c) {
        auto ch = std::make_shared<Channel>();
        ch->name = c ? "Phu" : "Chinh";
        ch->size = {640, 360};
        ch->scenes = p.scenes;
        ch->program = c;                                  // kenh 0: do / xanh; kenh 1: xanh / la
        ch->preview = c + 1;
        p.channels.push_back(ch);
        OutputConfig o; o.name = QString("O%1").arg(c + 1); o.size = {160, 90}; o.channel = c;
        p.outputs.push_back(o);
    }
    p.filePath = dir.filePath("mv.json");
    auto ch0 = p.channels[0], ch1 = p.channels[1];

    {
        RenderEngine engine(p);
        engine.startEngine();
        MainWindow w(p, engine, {});
        w.setAskOnClose(false);
        w.show();
        pump(1200);
        CHECK(engine.error().isEmpty() && engine.fps() > 0, "Engine + MainWindow chay");
        CHECK(engine.sceneThumbs().channel == -1, "chua mo Multiview: khong ve anh thu nho Scene");

        // ---------- mo cua so ----------
        auto* act = w.findChild<QAction*>("multiviewAction");
        CHECK(act != nullptr, "menu View co muc Multiview");
        if (!act) return 1;
        act->trigger();
        auto* mv = w.findChild<MV*>("multiviewWindow");
        CHECK(mv && mv->isVisible() && mv->windowType() == Qt::Tool && mv->parentWidget() == &w, "Multiview la cua so Tool cua cua so chinh");
        if (!mv) return 1;
        mv->resize(1200, 760);
        CHECK(waitFor([&] { return mv->tiles().size() == 7; }) && sceneTiles(mv) == 3, "2 kenh x (PVW, PGM) + 3 o Scene");

        // ---------- anh thu nho ----------
        const bool thumbs = waitFor([&] {
            const auto* a = tile(mv, MV::Kind::Scene, 0, 0);
            const auto* b = tile(mv, MV::Kind::Scene, 0, 1);
            const auto* c = tile(mv, MV::Kind::Scene, 0, 2);
            return a && b && c && near(a->image, red) && near(b->image, blue) && near(c->image, green);
        });
        CHECK(thumbs, "o Scene hien dung hinh tung Scene (do, xanh, la)");
        CHECK(engine.sceneThumbs().images.size() == 3 && std::max(engine.sceneThumbs().images[0].width(), engine.sceneThumbs().images[0].height()) <= 320,
              "anh thu nho Scene toi da 320 diem anh");
        CHECK(waitFor([&] {
            const auto* pg = tile(mv, MV::Kind::Program, 0);
            const auto* pv = tile(mv, MV::Kind::Preview, 1);
            return pg && pv && near(pg->image, red) && near(pv->image, green);
        }), "o PGM kenh 1 = do, o PVW kenh 2 = la");

        // khong o nao chong len o khac, tat ca nam trong cua so
        bool layoutOk = true;
        for (size_t i = 0; i < mv->tiles().size(); ++i) {
            const QRect r = mv->tiles()[i].rect;
            layoutOk = layoutOk && mv->rect().contains(r) && r.width() > 40 && r.height() > 30;
            for (size_t j = i + 1; j < mv->tiles().size(); ++j) layoutOk = layoutOk && !r.intersects(mv->tiles()[j].rect);
        }
        CHECK(layoutOk, "cac o nam trong cua so, khong chong nhau");

        // ---------- danh dau On Air / Preview ----------
        auto mark = [&](int scene) { const auto* t = tile(mv, MV::Kind::Scene, mv->tiles().back().channel, scene); return t ? t->mark : MV::Mark::None; };
        CHECK(mark(0) == MV::Mark::Program && mark(1) == MV::Mark::Preview && mark(2) == MV::Mark::None, "Scene: 1 = Program (do), 2 = Preview, 3 = khong");
        CHECK(tile(mv, MV::Kind::Program, 0)->mark == MV::Mark::Program && tile(mv, MV::Kind::Program, 0)->onAir &&
                  tile(mv, MV::Kind::Preview, 0)->mark == MV::Mark::Preview, "o PGM do + ON AIR, o PVW xanh");
        {
            const QImage shot = mv->grab().toImage();
            const qreal dpr = shot.devicePixelRatio();
            auto border = [&](const MV::Tile* t) { return shot.pixelColor(int((t->rect.left() + 1) * dpr), int(t->rect.center().y() * dpr)); };
            CHECK(border(tile(mv, MV::Kind::Scene, 0, 0)) == QColor(colors::kProgram) && border(tile(mv, MV::Kind::Scene, 0, 1)) == QColor(colors::kPreview) &&
                      border(tile(mv, MV::Kind::Program, 0)) == QColor(colors::kProgram) && border(tile(mv, MV::Kind::Preview, 0)) == QColor(colors::kPreview),
                  "vien ve dung mau do / xanh");
            const QByteArray out = qgetenv("LUMACAST_SHOT");                // tuy chon: luu anh de xem bang mat
            if (!out.isEmpty()) shot.save(QString::fromLocal8Bit(out));
        }

        // ---------- bam chuot: chi Preview, khong bao gio Program ----------
        click(mv, tile(mv, MV::Kind::Scene, 0, 2));
        CHECK(waitFor([&] { return ch0->preview == 2 && mark(2) == MV::Mark::Preview; }), "bam o Scene 3 = Preview");
        click(mv, tile(mv, MV::Kind::Scene, 0, 2), Qt::LeftButton, QEvent::MouseButtonDblClick);
        click(mv, tile(mv, MV::Kind::Scene, 0, 1), Qt::RightButton);
        pump(400);
        CHECK(ch0->program == 0 && ch0->preview == 2 && !ch0->inTransition(), "nhap dup / chuot phai: Program khong doi, khong chuyen");
        auto* autoB = w.findChild<QPushButton*>("auto");
        autoB->setChecked(true);
        click(mv, tile(mv, MV::Kind::Scene, 0, 1));
        pump(500);
        CHECK(ch0->program == 0 && ch0->preview == 1 && !ch0->inTransition(), "AUTO bat: bam o trong Multiview van chi vao Preview");
        autoB->setChecked(false);
        click(mv, tile(mv, MV::Kind::Program, 0));
        pump(300);
        CHECK(ch0->program == 0 && ch0->preview == 1, "bam o PGM cua kenh dang chon: khong lam gi");

        // ---------- doi kenh dang chon ----------
        auto* combo = w.findChild<QComboBox*>("chanCombo");
        click(mv, tile(mv, MV::Kind::Program, 1));
        CHECK(combo && waitFor([&] { return combo->currentIndex() == 1 && mv->tiles().back().channel == 1; }), "bam o PGM kenh 2 = chon kenh 2");
        CHECK(ch0->program == 0 && ch0->preview == 1 && ch1->program == 1 && ch1->preview == 2, "doi kenh khong doi Program/Preview nao");
        CHECK(waitFor([&] { return engine.sceneThumbs().channel == 1 && near(tile(mv, MV::Kind::Scene, 1, 0)->image, red); }), "anh thu nho theo kenh moi");
        CHECK(mark(1) == MV::Mark::Program && mark(2) == MV::Mark::Preview && mark(0) == MV::Mark::None, "danh dau theo kenh 2");
        click(mv, tile(mv, MV::Kind::Scene, 1, 0));
        CHECK(waitFor([&] { return ch1->preview == 0; }) && ch1->program == 1 && ch0->preview == 1, "bam o Scene: chi doi Preview cua kenh 2");

        // ---------- khoa van hanh: chon Preview la dieu khien live ----------
        w.setLocked(true);
        click(mv, tile(mv, MV::Kind::Scene, 1, 2));
        CHECK(waitFor([&] { return ch1->preview == 2; }) && ch1->program == 1, "khoa van hanh: van chon duoc Preview");
        w.setLocked(false);
        click(mv, tile(mv, MV::Kind::Preview, 0));
        CHECK(waitFor([&] { return combo->currentIndex() == 0 && mv->tiles().back().channel == 0; }), "bam o PVW kenh 1 = quay lai kenh 1");

        // ---------- dang chuyen + FTB ----------
        engine.post([ch0](double now) { ch0->startTransition(TransitionType::Fade, 1.5, now); });
        CHECK(waitFor([&] { return mark(0) == MV::Mark::Program && mark(1) == MV::Mark::Program; }, 1200), "dang chuyen: ca hai Scene danh dau do");
        CHECK(waitFor([&] { return !ch0->inTransition() && mark(1) == MV::Mark::Program && mark(0) == MV::Mark::Preview; }), "chuyen xong: doi mau");
        engine.post([ch0](double) { ch0->toggleFtb(0.1); });
        CHECK(waitFor([&] { return !tile(mv, MV::Kind::Program, 0)->onAir; }), "FTB den hoan toan: tat nhan ON AIR");
        engine.post([ch0](double) { ch0->toggleFtb(0.1); });
        CHECK(waitFor([&] { return tile(mv, MV::Kind::Program, 0)->onAir; }), "bo FTB: ON AIR lai");

        // ---------- phim tat cua cua so chinh khi Multiview duoc chon ----------
        {
            QWidget other;                                // doi chung: cua so khong lien quan thi phim tat KHONG chay
            other.show();
            makeActive(&other);
            const int pg = ch0->program;
            QTest::keyClick(&other, Qt::Key_Return);
            pump(400);
            CHECK(ch0->program == pg, "doi chung: Enter o cua so khac khong CUT");
        }
        makeActive(mv);
        CHECK(QApplication::activeWindow() == mv, "Multiview la cua so dang chon");
        int pg = ch0->program, pv = ch0->preview;
        QTest::keyClick(mv, Qt::Key_Return);
        CHECK(waitFor([&] { return ch0->program == pv && ch0->preview == pg; }), "Enter trong Multiview = CUT");
        pg = ch0->program; pv = ch0->preview;
        makeActive(mv);
        QTest::keyClick(mv, Qt::Key_Space);
        CHECK(waitFor([&] { return ch0->program == pv && !ch0->inTransition(); }, 6000), "Space trong Multiview = TAKE");
        pump(300);
        makeActive(mv);
        QTest::keyClick(mv, Qt::Key_3);
        CHECK(waitFor([&] { return ch0->preview == 2; }), "phim 3 trong Multiview = chon Preview Scene 3");
        // nut tren Multiview khong giu focus: Space khong bam nut toan man hinh
        CHECK(mv->findChild<QPushButton*>("multiviewFullScreen")->focusPolicy() == Qt::NoFocus && !mv->isFullScreen(), "nut toan man hinh khong nhan phim");

        // ---------- toan man hinh ----------
        makeActive(mv);
        QTest::keyClick(mv, Qt::Key_F11);
        CHECK(waitFor([&] { return mv->isFullScreen(); }), "F11 = toan man hinh");
        pump(300);
        pg = ch0->program; pv = ch0->preview;
        makeActive(mv);
        QTest::keyClick(mv, Qt::Key_Return);
        CHECK(waitFor([&] { return ch0->program == pv; }), "toan man hinh: Enter van CUT");
        makeActive(mv);
        QTest::keyClick(mv, Qt::Key_Escape);
        CHECK(waitFor([&] { return !mv->isFullScreen(); }) && mv->isVisible(), "Esc = thoat toan man hinh, cua so van mo");
        QTest::keyClick(mv, Qt::Key_Escape);
        pump(200);
        CHECK(mv->isVisible(), "Esc khi dang la cua so thuong: khong dong");

        // ---------- them / hoan tac Scene khi Multiview dang mo ----------
        QImage png(640, 360, QImage::Format_RGB32);       // nua tren vang, nua duoi do: bat loi lat nguoc anh thu nho
        png.fill(yellow);
        for (int y = 180; y < 360; ++y)
            for (int x = 0; x < 640; ++x) png.setPixelColor(x, y, red);
        const QString imgPath = dir.filePath("vang.png");
        png.save(imgPath);
        CHECK(w.addMediaFile(imgPath), "nhap them mot anh");
        CHECK(waitFor([&] {
            const auto* t = tile(mv, MV::Kind::Scene, 0, 3);
            return sceneTiles(mv) == 4 && t && !t->image.isNull() && t->image.pixelColor(t->image.width() / 2, t->image.height() / 4) == yellow &&
                   t->image.pixelColor(t->image.width() / 2, t->image.height() * 3 / 4) == red;
        }), "o Scene moi xuat hien dung hinh, dung chieu (tren vang, duoi do)");
        w.undoStack()->undo();
        CHECK(waitFor([&] { return sceneTiles(mv) == 3 && engine.sceneThumbs().images.size() == 3 && near(tile(mv, MV::Kind::Scene, 0, 2)->image, green); }),
              "Hoan tac: o bien mat, cac o con lai dung hinh");

        // ---------- dong cua so ----------
        mv->close();
        CHECK(!mv->isVisible() && waitFor([&] { return engine.sceneThumbs().channel == -1 && engine.sceneThumbs().images.empty(); }),
              "dong Multiview: render thread ngung ve anh thu nho");
        act->trigger();
        CHECK(w.findChildren<MV*>().size() == 1 && mv->isVisible() && waitFor([&] { return engine.sceneThumbs().channel == 0; }),
              "mo lai: dung lai cung cua so");
        appSettings().remove("multiview/geometry");
        engine.stopEngine();
    }
    CHECK(appSettings().contains("multiview/geometry"), "thoat khi Multiview dang mo: van luu vi tri cua so");

    // ---------- khong phong toan man hinh len man dang co Output toan man hinh ----------
    {
        p.outputs.resize(1);
        p.outputs[0].fullscreen = true;
        p.outputs[0].screenIndex = 0;
        p.outputs[0].screenName = QGuiApplication::screens().first()->name();
        RenderEngine engine(p);
        engine.startEngine();
        MainWindow w(p, engine, {});
        w.setAskOnClose(false);
        w.show();
        pump(500);
        w.findChild<QAction*>("multiviewAction")->trigger();
        auto* mv = w.findChild<MV*>("multiviewWindow");
        QScreen* out = QGuiApplication::screens().first();
        mv->move(out->geometry().topLeft() + QPoint(60, 60));
        mv->resize(640, 400);
        pump(600);
        CHECK(mv->screenHasFullscreenOutput(out), "phat hien man hinh co Output toan man hinh");
        CHECK(!mv->toggleFullScreen() && !mv->isFullScreen() && mv->notice().contains("Output"), "tu choi toan man hinh tren man do, co thong bao");
        if (QGuiApplication::screens().size() > 1) {
            QScreen* spare = QGuiApplication::screens().at(1);
            mv->move(spare->geometry().topLeft() + QPoint(60, 60));
            pump(400);
            CHECK(!mv->screenHasFullscreenOutput(spare), "man hinh thu hai khong bi coi la co Output");
            CHECK(mv->toggleFullScreen() && waitFor([&] { return mv->isFullScreen() && mv->screen() == spare; }), "man thu hai: toan man hinh duoc");
            mv->toggleFullScreen();
        } else {
            std::cout << "bo qua: chi co 1 man hinh, khong thu toan man hinh tren man phu\n";
        }
        engine.stopEngine();
    }

    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
