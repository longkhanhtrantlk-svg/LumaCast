// Bo cuc cua so chinh: MOI bo cuc dung san phai vua man hinh nho (1920x1080 phong 125% = 1536x816 diem logic), cac khung khong de
// len nhau, khong bi ep, cot phai khong co thanh cuon ngang; doi bo cuc / F9 / bo cuc cua toi / khoa keo khung / bo cuc cu da luu.
// LC_SHOT_DIR=<thu muc> de luu anh chup giao dien. Tham so "en": chay voi giao dien tieng Anh (ctest layout_en).
#include "DockChrome.h"
#include "MainWindow.h"
#include "OverlayPanel.h"
#include "Text.h"
#include "Theme.h"
#include "core/AppSettings.h"
#include "media/StaticSources.h"
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPushButton>
#include <QRubberBand>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
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
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(5);
    }
}
static QString str(const QRect& r) { return QString("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()); }
static QString str(const QSize& s) { return QString("%1x%2").arg(s.width()).arg(s.height()); }

static QDockWidget* dock(MainWindow& w, const char* name) { return w.findChild<QDockWidget*>(name); }
static bool onScreen(QDockWidget* d) { return !d->isHidden() && !d->visibleRegion().isEmpty(); }

// Cac khung dang thuc su hien (tab dang bi che khong tinh) + vung giua.
static std::vector<std::pair<QString, QRect>> visibleAreas(MainWindow& w) {
    std::vector<std::pair<QString, QRect>> v;
    for (QDockWidget* d : w.findChildren<QDockWidget*>())
        if (!d->isFloating() && d->isVisible() && !d->visibleRegion().isEmpty()) v.push_back({d->objectName(), d->geometry()});
    v.push_back({"central", w.centralWidget()->geometry()});
    return v;
}

static void checkAreas(MainWindow& w, const std::string& tag) {
    const auto areas = visibleAreas(w);
    const QRect inside(0, 0, w.width(), w.height());
    QStringList overlaps, outside;
    for (size_t i = 0; i < areas.size(); ++i) {
        if (!inside.contains(areas[i].second)) outside << areas[i].first + " " + str(areas[i].second);
        for (size_t j = i + 1; j < areas.size(); ++j)
            if (areas[i].second.intersects(areas[j].second)) overlaps << areas[i].first + " x " + areas[j].first;
    }
    CHECK(overlaps.isEmpty(), tag + "khong khung nao de len nhau " + overlaps.join("; ").toStdString());
    CHECK(outside.isEmpty(), tag + "khong khung nao loi ra ngoai " + outside.join("; ").toStdString());
    // widget ben trong khong duoc bi ep nho hon muc toi thieu cua no (nut/o nhap de len nhau)
    QStringList squeezed, sideways;
    for (QDockWidget* d : w.findChildren<QDockWidget*>()) {
        if (!d->isVisible() || d->visibleRegion().isEmpty() || !d->widget()) continue;
        const QSize need = d->widget()->minimumSizeHint(), got = d->widget()->size();
        if (need.width() > got.width() + 1 || need.height() > got.height() + 1)
            squeezed << d->objectName() + " can " + str(need) + " co " + str(got);
        if (w.dockWidgetArea(d) != Qt::RightDockWidgetArea) continue;
        QList<QScrollArea*> areasIn = d->findChildren<QScrollArea*>();
        if (auto* own = qobject_cast<QScrollArea*>(d->widget())) areasIn.prepend(own);
        for (QScrollArea* sa : areasIn)
            if (sa->isVisible() && sa->objectName() != "nowShowing" && sa->horizontalScrollBar()->maximum() > 0)
                sideways << QString("%1 thua %2 px (can %3, co %4)").arg(d->objectName()).arg(sa->horizontalScrollBar()->maximum())
                                .arg(sa->widget() ? sa->widget()->minimumSizeHint().width() : 0).arg(sa->viewport()->width());
    }
    CHECK(squeezed.isEmpty(), tag + "noi dung khung khong bi ep " + squeezed.join("; ").toStdString());
    CHECK(sideways.isEmpty(), tag + "cot phai khong phai cuon ngang " + sideways.join("; ").toStdString());
}

static void shot(MainWindow& w, const char* name) {
    if (qEnvironmentVariableIsSet("LC_SHOT_DIR")) w.grab().save(qEnvironmentVariable("LC_SHOT_DIR") + "/" + name + ".png");
}

// ---------------- hang ten khung (tab tren dau) ----------------
static QTabBar* barWith(MainWindow& w, const QString& title) {
    for (QTabBar* b : w.findChildren<QTabBar*>())
        if (DockChrome::isDockTabBar(b) && b->isVisible())
            for (int i = 0; i < b->count(); ++i)
                if (b->tabText(i) == title) return b;
    return nullptr;
}

static void checkChrome(MainWindow& w, const std::string& tag) {
    QStringList cut, titles;
    for (QTabBar* b : w.findChildren<QTabBar*>()) {
        if (!DockChrome::isDockTabBar(b) || !b->isVisible()) continue;
        QFont f = b->font();
        f.setBold(true);
        for (int i = 0; i < b->count(); ++i)
            if (b->tabRect(i).width() < QFontMetrics(f).horizontalAdvance(b->tabText(i)) + 8) cut << b->tabText(i);
    }
    for (QDockWidget* d : w.findChildren<QDockWidget*>()) {
        if (!onScreen(d) || d->isFloating()) continue;
        auto* t = dynamic_cast<DockTitle*>(d->titleBarWidget());
        bool mates = false;
        for (QDockWidget* m : w.tabifiedDockWidgets(d)) mates = mates || !m->isHidden();
        const int want = mates ? DockTitle::kStripHeight : DockTitle::kHeaderHeight;
        if (t && !mates && t->shownText() != d->windowTitle()) titles << d->objectName() + " ten bi cat: " + t->shownText();
        if (!t || t->tabbed() != mates || t->height() != want)
            titles << QString("%1 cao %2 (can %3)").arg(d->objectName()).arg(t ? t->height() : -1).arg(want);
        if (mates) {                                          // gop tab: hang ten nam TREN noi dung
            QTabBar* bar = barWith(w, d->windowTitle());
            if (!bar || bar->mapTo(&w, QPoint(0, 0)).y() >= d->mapTo(&w, QPoint(0, 0)).y()) titles << d->objectName() + " tab khong o tren";
        }
    }
    for (QTabBar* b : w.findChildren<QTabBar*>())
        if (DockChrome::isDockTabBar(b) && b->isVisible() && b->drawBase()) cut << "con duong ke duoi hang tab";
    CHECK(cut.isEmpty(), tag + "ten tren tab khong bi cat, khong co duong ke thua " + cut.join(", ").toStdString());
    CHECK(titles.isEmpty(), tag + "gop tab: chi con vach mong duoi hang ten; dung mot minh: mot the ten " + titles.join("; ").toStdString());
}

// Chuot gia gui vao widget dang giu chuot (Qt giu chuot khi bat dau keo khung); toa do man hinh tinh tu diem bam.
static void mouse(QWidget* target, QEvent::Type type, const QPoint& global, Qt::MouseButtons held) {
    QWidget* to = QWidget::mouseGrabber() ? QWidget::mouseGrabber() : target;
    QMouseEvent e(type, QPointF(to->mapFromGlobal(global)), QPointF(global), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, held,
                  Qt::NoModifier);
    QApplication::sendEvent(to, &e);
    QApplication::processEvents();
}

static void dragBy(QWidget* from, const QPoint& local, const QPoint& delta) {
    const QPoint start = from->mapToGlobal(local);
    mouse(from, QEvent::MouseButtonPress, start, Qt::LeftButton);
    for (int i = 1; i <= 12; ++i) mouse(from, QEvent::MouseMove, start + delta * i / 12, Qt::LeftButton);
    mouse(from, QEvent::MouseButtonRelease, start + delta, Qt::NoButton);
    if (QWidget* g = QWidget::mouseGrabber()) g->releaseMouse();
    pump(300);
}

static bool inMain(MainWindow& w, QDockWidget* d) { return d->parentWidget() == &w && !d->isFloating(); }

static void testChrome(MainWindow& w) {
    auto action = [&](const char* n) { return w.findChild<QAction*>(n); };
    auto* insp = dock(w, "dockInspector"), *ov = dock(w, "dockOverlay"), *out = dock(w, "dockOutputs");
    auto menuAction = [&](QDockWidget* d, const char* name, bool trigger) {
        std::unique_ptr<QMenu> m(w.dockChrome()->menuFor(d));
        QAction* a = m->findChild<QAction*>(name);
        if (!a) for (QAction* x : m->actions()) if (x->objectName() == name) a = x;
        const bool ok = a && a->isEnabled();
        if (ok && trigger) a->trigger();
        pump(300);
        return ok;
    };

    action("layout_prepare")->trigger();
    w.resize(1536, 780);
    pump(500);
    CHECK(w.tabPosition(Qt::RightDockWidgetArea) == QTabWidget::North && w.tabPosition(Qt::BottomDockWidgetArea) == QTabWidget::North,
          "ten khung nam tren dau");

    // ---- menu chuot phai: tach / gan lai / dong ----
    CHECK(menuAction(ov, "dockDetach", true) && ov->isFloating(), "menu: tach khung ra");
    CHECK(menuAction(ov, "dockAttach", true) && !ov->isFloating() && inMain(w, ov), "menu: gan lai vao cua so");
    CHECK(menuAction(ov, "dockClose", true) && ov->isHidden(), "menu: dong khung");
    action("layout_prepare")->trigger();
    pump(500);
    w.setLocked(true);
    pump(200);
    CHECK(!menuAction(ov, "dockDetach", false) && !menuAction(ov, "dockClose", false), "khoa: menu tach / dong bi mo");
    QTabBar* bar = barWith(w, "Overlay");
    CHECK(bar && !bar->isMovable(), "khoa: tab khong keo duoc");
    if (bar) {
        int at = 0;
        for (int i = 0; i < bar->count(); ++i) if (bar->tabText(i) == "Overlay") at = i;
        dragBy(bar, bar->tabRect(at).center(), QPoint(-500, 200));
    }
    ov->raise();
    pump(200);
    if (auto* t = ov->titleBarWidget()) dragBy(t, QPoint(t->width() / 2, 3), QPoint(-500, 200));
    CHECK(inMain(w, ov) && inMain(w, insp), "khoa: keo tab va keo vach deu khong tach khung");
    w.setLocked(false);
    pump(300);

    // ---- keo mot tab ra ngoai (thao tac goc cua Qt) ----
    bar = barWith(w, "Overlay");
    CHECK(bar && bar->isMovable(), "mo khoa: tab keo duoc");
    if (bar) {
        int at = 0;
        for (int i = 0; i < bar->count(); ++i) if (bar->tabText(i) == "Overlay") at = i;
        dragBy(bar, bar->tabRect(at).center(), QPoint(-600, 150));
    }
    CHECK(!inMain(w, ov) && inMain(w, insp), "keo mot ten tab ra ngoai: chi khung do tach ra");
    action("layout_prepare")->trigger();
    pump(500);
    CHECK(inMain(w, ov), "ap lai bo cuc: khung ve cho");

    // ---- keo vach mong: ca nhom di cung ----
    insp->raise();
    pump(300);
    auto* strip = insp->titleBarWidget();
    CHECK(strip && strip->height() == DockTitle::kStripHeight, "khung gop tab co vach nam de keo ca nhom");
    if (strip) dragBy(strip, QPoint(strip->width() / 2, 3), QPoint(-600, 150));
    CHECK(!inMain(w, insp) && !inMain(w, ov) && insp->parentWidget() == ov->parentWidget(),
          std::string("keo vach: ca nhom tach ra cung nhau (cha: ") + insp->parentWidget()->metaObject()->className() + " / " +
              ov->parentWidget()->metaObject()->className() + ")");
    if (QWidget* group = insp->parentWidget(); group != &w) {
        CHECK((group->windowFlags() & Qt::Tool) == Qt::Tool, "nhom tach ra la cua so Tool: giu phim tat");
        pump(600);
        shot(w, "nhom-tach-ra-cua-so-chinh");
        if (qEnvironmentVariableIsSet("LC_SHOT_DIR")) group->grab().save(qEnvironmentVariable("LC_SHOT_DIR") + "/nhom-tach-ra.png");
    }
    action("layout_prepare")->trigger();
    pump(600);
    CHECK(inMain(w, insp) && inMain(w, ov) && w.tabifiedDockWidgets(insp).contains(ov), "ap lai bo cuc: nhom ve cho, van la tab");

    // ---- khung dung mot minh: keo bang "the" ten ----
    action("layout_live")->trigger();
    pump(500);
    auto* head = out->titleBarWidget();
    CHECK(head && head->height() == DockTitle::kHeaderHeight, "khung dung mot minh co the ten");
    if (head) dragBy(head, QPoint(20, 12), QPoint(-600, 200));
    CHECK(out->isFloating(), "keo the ten: khung tach ra");
    action("layout_live")->trigger();
    pump(500);
    CHECK(inMain(w, out), "ve lai bo cuc Dang dien");
}

static void checkLayout(MainWindow& w, const QSize& size, const std::string& name) {
    w.resize(size);
    pump(500);
    const std::string tag = name + ": ";
    CHECK(w.width() <= size.width() && w.height() <= size.height(),
          tag + "cua so co duoc ve " + str(size).toStdString() + " (thuc te " + str(w.size()).toStdString() + ")");
    checkAreas(w, tag);
    shot(w, name.c_str());
}

static Project makeProject() {
    Project p;
    SourceDef a; a.type = "color"; a.color = Qt::blue; a.label = "Xanh";
    auto sa = p.addSource(a);
    auto s = std::make_shared<Scene>();
    s->name = "Xanh"; s->size = {1320, 360};
    s->layers.push_back({sa, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
    p.scenes = {s};
    auto ch = std::make_shared<Channel>();
    ch->name = "K"; ch->size = {1320, 360}; ch->scenes = p.scenes; ch->program = 0; ch->preview = 0;
    p.channels = {ch};
    OutputConfig o1; o1.name = "Wall 1,1"; o1.size = {160, 90};
    OutputConfig o2; o2.name = "Wall 1,2"; o2.size = {160, 90};
    p.outputs = {o1, o2};
    return p;
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
    if (argc > 1 && QString(argv[1]) == "en") i18n::install(Language::English);
    applyDarkTheme(app);

    Project p = makeProject();
    // vai overlay de khung Overlay co noi dung that (dong cao, dong ho, dong dang hien)
    for (OverlayType t : {OverlayType::Title, OverlayType::Countdown, OverlayType::Ticker}) {
        OverlayDef d = defaultOverlay(t);
        std::vector<OverlayDef> defs = p.channels[0]->overlays.defs();
        d.id = int(defs.size()) + 1;
        if (t == OverlayType::Title) d.name = "Nguyễn Thị Ánh Tuyết — Giám đốc điều hành";
        defs.push_back(d);
        p.channels[0]->overlays.setDefs(defs);
    }

    RenderEngine engine(p);
    engine.startEngine();
    {
        CHECK(appSettings().fileName().startsWith(dir.path()), "test dung cai dat rieng, khong dung registry that");
        if (g_fail) return 1;
        MainWindow w(p, engine, {});
        w.setAskOnClose(false);
        w.show();
        pump(700);
        const bool bigScreen = w.screen()->availableGeometry().width() >= 1800;
        CHECK(w.layoutId() == (bigScreen ? "big" : "prepare"), "lan mo dau chon bo cuc theo kich thuoc man: " + w.layoutId().toStdString());
        std::cout << "minimumSizeHint cua so: " << str(w.minimumSizeHint()).toStdString() << "  menu " << str(w.menuBar()->sizeHint()).toStdString()
                  << "  thanh trang thai " << str(w.statusBar()->minimumSizeHint()).toStdString()
                  << "  vung giua " << str(w.centralWidget()->minimumSizeHint()).toStdString() << "\n";
        for (QDockWidget* d : w.findChildren<QDockWidget*>())
            std::cout << "  " << d->objectName().toStdString() << " toi thieu " << str(d->minimumSizeHint()).toStdString() << "\n";
        CHECK((w.dockOptions() & QMainWindow::AllowNestedDocks) && (w.dockOptions() & QMainWindow::GroupedDragging) &&
                  (w.dockOptions() & QMainWindow::AllowTabbedDocks) && w.isDockNestingEnabled(),
              "cho dat khung canh nhau, gop tab, keo ca nhom");
        w.overlayPanel()->setOn(1, true);
        w.overlayPanel()->selectId(2);

        auto* label = w.findChild<QToolButton*>("layoutButton");
        auto action = [&](const char* n) { return w.findChild<QAction*>(n); };
        CHECK(action("layout_prepare")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_F1) &&
                  action("layout_live")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_F2) &&
                  action("layout_big")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_F3) &&
                  action("toggleRightColumn")->shortcut() == QKeySequence(Qt::Key_F9),
              "phim Ctrl+F1/F2/F3 va F9");
        CHECK(!action("layout_mine")->isEnabled() && !w.applyLayout("mine"), "chua luu bo cuc cua toi: muc bi mo");

        const int program = p.channels[0]->program, preview = p.channels[0]->preview;
        const uint64_t presented = engine.output(0)->presented;

        // ---------- ba bo cuc dung san ----------
        const struct { const char* id; QString name; } presets[3] = {{"prepare", text::layoutPrepare()}, {"live", text::layoutLive()}, {"big", text::layoutBig()}};
        const QString modified = text::layoutModified().arg(QString()).trimmed();      // "(đã chỉnh)" / "(modified)"
        int centralWidth[3] = {};
        for (int i = 0; i < 3; ++i) {
            const std::string id = presets[i].id;
            action(("layout_" + id).c_str())->trigger();
            pump(300);
            CHECK(w.layoutId() == presets[i].id && label->text().contains(presets[i].name) && !label->text().contains(modified),
                  id + ": hien ten bo cuc dang dung (" + label->text().toStdString() + ")");
            checkLayout(w, {1536, 780}, id + "-125");
            checkChrome(w, id + ": ");
            centralWidth[i] = w.centralWidget()->width();
            auto* out = dock(w, "dockOutputs"), *aud = dock(w, "dockAudio"), *insp = dock(w, "dockInspector"), *ov = dock(w, "dockOverlay"),
                *tl = dock(w, "dockTimeline"), *plug = dock(w, "dockPlugins"), *scenes = dock(w, "dockScenes");
            CHECK(onScreen(scenes), id + ": danh sach Scenes luon hien");
            if (id == "prepare") {
                CHECK(onScreen(insp) && !ov->isHidden() && out->isHidden() && aud->isHidden() && w.tabifiedDockWidgets(insp).contains(ov),
                      "Chuan bi: mot cot Inspector (co tab Effect) + Overlay");
                CHECK(insp->width() >= 260 && insp->width() <= 340, "Chuan bi: cot phai hep lai (" + std::to_string(insp->width()) + " px)");
            } else if (id == "live") {
                CHECK(onScreen(out) && onScreen(aud) && onScreen(ov) && insp->isHidden() && tl->isHidden() && plug->isHidden(),
                      "Dang dien: Outputs / Am thanh / Overlay; an Inspector, Timeline");
                CHECK(out->x() == aud->x() && aud->x() == ov->x() && out->y() < aud->y() && aud->y() < ov->y(), "Dang dien: xep doc mot cot");
                CHECK(ov->width() >= 200 && ov->width() <= 250, "Dang dien: cot hep (" + std::to_string(ov->width()) + " px)");
            } else {
                CHECK(onScreen(out) && onScreen(aud) && onScreen(ov) && onScreen(insp) && !plug->isHidden(), "Man lon: du hai cot");
                CHECK(out->x() == ov->x() && insp->x() > ov->x() + ov->width() - 2 && w.tabifiedDockWidgets(insp).contains(plug),
                      "Man lon: cot hep Outputs/Am thanh/Overlay, cot Inspector/Plug-in ben canh");
                CHECK(ov->width() >= 200 && ov->width() <= 250 && insp->width() >= 250 && insp->width() <= 320,
                      "Man lon: be rong hai cot " + std::to_string(ov->width()) + " + " + std::to_string(insp->width()));
            }
            // khung Overlay: dong nao cung bam duoc (nut trong dong khong bi cat)
            if (onScreen(ov) || !ov->isHidden()) {
                ov->raise();
                pump(300);
                checkAreas(w, id + " (tab Overlay): ");
                for (int ovId : {1, 2, 3}) {
                    QWidget* row = w.overlayPanel()->row(ovId);
                    auto* del = row ? row->findChild<QPushButton*>("ovDel") : nullptr;
                    // so voi VUNG NHIN cua danh sach (dong rong hon vung nhin thi van "chua" nut ma mat thuong khong thay)
                    QWidget* view = w.overlayPanel()->findChild<QListWidget*>("ovList")->viewport();
                    bool whole = del != nullptr;
                    if (row)
                        for (auto* b : row->findChildren<QPushButton*>()) {
                            const QRect r(b->mapTo(view, QPoint(0, 0)), b->size());
                            whole = whole && r.left() >= 0 && r.right() <= view->width();
                        }
                    if (auto* clock = row ? row->findChild<QLabel*>("ovClock") : nullptr)
                        whole = whole && clock->width() >= clock->sizeHint().width();
                    CHECK(whole, id + ": dong overlay " + std::to_string(ovId) + " hien du nut va dong ho (dong " +
                                     std::to_string(row ? row->width() : -1) + ", vung " + std::to_string(view->width()) + ")");
                }
                shot(w, (id + "-overlay").c_str());
            }
            // tung tab con lai trong bo cuc: van khong de, khong bi ep
            for (QDockWidget* d : w.findChildren<QDockWidget*>()) {
                if (d->isHidden()) continue;
                d->raise();
                pump(150);
                checkAreas(w, id + " mo " + d->objectName().toStdString() + ": ");
                shot(w, (id + "-tab-" + d->objectName().toStdString()).c_str());
            }
            checkLayout(w, {1920, 1000}, id + "-man-lon");
            action(("layout_" + id).c_str())->trigger();
            w.resize(1536, 780);
            pump(300);
        }
        CHECK(centralWidth[1] > centralWidth[2] && centralWidth[1] >= 1250, "Dang dien: hinh to nhat (" + std::to_string(centralWidth[1]) + " px)");
        CHECK(p.channels[0]->program == program && p.channels[0]->preview == preview && engine.output(0)->presented > presented + 100 &&
                  !engine.output(0)->black && !engine.output(0)->freeze && p.channels[0]->overlays.isOn(1),
              "doi bo cuc khong dung toi hinh dang phat");

        // Timeline du cho o bo cuc Chuan bi
        w.applyLayout("prepare");
        w.resize(1536, 780);
        pump(500);
        auto* tl = dock(w, "dockTimeline");
        tl->raise();
        pump(300);
        auto* sa = qobject_cast<QScrollArea*>(tl->widget());
        CHECK(sa && sa->verticalScrollBar()->maximum() == 0 && sa->horizontalScrollBar()->maximum() == 0, "man nho: Timeline hien du, khong phai cuon (cuon " + std::to_string(sa ? sa->verticalScrollBar()->maximum() : -1) + "/" + std::to_string(sa ? sa->horizontalScrollBar()->maximum() : -1) +
                  ", vung " + str(sa ? sa->viewport()->size() : QSize()).toStdString() + ", can " + str(sa ? sa->widget()->minimumSizeHint() : QSize()).toStdString() + ", cua so " + str(w.size()).toStdString() + ")");
        shot(w, "prepare-timeline");

        // ---------- F9: an / hien cot phai ----------
        for (const char* id : {"prepare", "big"}) {
            w.applyLayout(id);
            pump(500);
            std::vector<std::pair<QString, bool>> before;
            for (QDockWidget* d : w.findChildren<QDockWidget*>()) before.push_back({d->objectName(), d->isHidden()});
            const int narrow = w.centralWidget()->width();
            const int column = dock(w, "dockInspector")->width();
            action("toggleRightColumn")->trigger();
            pump(400);
            bool anyRight = false;
            for (QDockWidget* d : w.findChildren<QDockWidget*>()) anyRight = anyRight || (w.dockWidgetArea(d) == Qt::RightDockWidgetArea && !d->isHidden());
            CHECK(!anyRight && w.centralWidget()->width() > narrow + 200, std::string(id) + ": F9 an ca cot phai, hinh rong ra");
            if (std::string(id) == "big") shot(w, "big-an-cot-phai");
            action("toggleRightColumn")->trigger();
            pump(500);
            bool same = true;
            for (const auto& [name, hidden] : before) same = same && w.findChild<QDockWidget*>(name)->isHidden() == hidden;
            CHECK(same && std::abs(dock(w, "dockInspector")->width() - column) <= 12, std::string(id) + ": F9 lan nua hien lai dung nhu cu (" +
                  std::to_string(dock(w, "dockInspector")->width()) + "/" + std::to_string(column) + ")");
        }

        testChrome(w);

        // ---------- vung tha khi keo khung phai thay ro ----------
        {
            QRubberBand band(QRubberBand::Rectangle, w.centralWidget());
            band.setGeometry(40, 40, 200, 120);
            band.show();
            pump(200);
            const QImage img = band.grab().toImage();
            const QColor edge = img.pixelColor(2, img.height() / 2), inside = img.pixelColor(img.width() / 2, img.height() / 2);
            CHECK(edge.blue() > 200 && edge.blue() > edge.red() + 60 && inside.blue() > inside.red() + 30,
                  "vung tha to mau nhan, vien day (" + edge.name().toStdString() + " / " + inside.name().toStdString() + ")");
        }

        // ---------- bo cuc cua toi ----------
        w.applyLayout("big");
        pump(400);
        dock(w, "dockPlaylist")->hide();
        pump(700);
        CHECK(label->text().contains(modified), "sua khung bang tay: nhan bao da chinh (" + label->text().toStdString() + ")");
        CHECK(w.saveMyLayout() && w.layoutId() == "mine" && action("layout_mine")->isEnabled() && label->text().contains(text::layoutMine()), "luu thanh bo cuc cua toi");
        w.applyLayout("big");
        pump(400);
        CHECK(!dock(w, "dockPlaylist")->isHidden(), "bo cuc dung san khong bi ghi de");
        CHECK(w.applyLayout("mine"), "mo lai bo cuc cua toi");
        pump(400);
        CHECK(dock(w, "dockPlaylist")->isHidden() && !dock(w, "dockOverlay")->isHidden(), "bo cuc cua toi giu dung cac khung");

        // ---------- khoa van hanh: khong keo khung ----------
        w.setLocked(true);
        auto features = [&] {
            bool none = true;
            for (QDockWidget* d : w.findChildren<QDockWidget*>()) none = none && d->features() == QDockWidget::NoDockWidgetFeatures;
            return none;
        };
        CHECK(features(), "khoa: khong keo / tach / dong khung duoc");
        CHECK(!action("layoutSaveMine")->isEnabled() && !w.saveMyLayout(), "khoa: khong luu bo cuc");
        action("layout_live")->trigger();
        pump(300);
        CHECK(w.layoutId() == "live" && features(), "khoa: van doi bo cuc bang phim, khung van khoa");
        w.setLocked(false);
        CHECK((dock(w, "dockOverlay")->features() & QDockWidget::DockWidgetMovable) && (dock(w, "dockOverlay")->features() & QDockWidget::DockWidgetClosable),
              "mo khoa: keo khung lai duoc");

        // ---------- tach mot khung: cua so phu giu phim tat (Qt::Tool) ----------
        auto* out = dock(w, "dockOutputs");
        out->setFloating(true);
        pump(300);
        CHECK(out->isWindow() && (out->windowFlags() & Qt::Tool) == Qt::Tool, "khung tach roi la cua so Tool");
        w.applyLayout("live");
        pump(300);
        CHECK(!out->isFloating(), "ap bo cuc dung san: khung tach roi ve lai cho");
        w.close();
    }
    // ---------- mo lai: giu bo cuc dang dung ----------
    {
        MainWindow w(p, engine, {});
        w.setAskOnClose(false);
        w.show();
        pump(600);
        CHECK(w.layoutId() == "live" && !dock(w, "dockOverlay")->isHidden() && dock(w, "dockInspector")->isHidden(), "mo lai: nho bo cuc Dang dien");
        CHECK(w.findChild<QAction*>("layout_mine")->isEnabled(), "mo lai: van con bo cuc cua toi");
    }

    // ---------- bo cuc cu da luu (ban truoc khi co khung moi) khong duoc lam vo tab ----------
    {
        QByteArray old;
        {
            MainWindow w(p, engine, {});
            w.setAskOnClose(false);
            w.show();
            pump(300);
            // tach cac tab ra canh nhau roi luu kieu cu (khong co so phien ban)
            auto* media = w.findChild<QDockWidget*>("dockScenes");
            for (const char* n : {"dockTimeline", "dockPlaylist"}) {
                auto* d = w.findChild<QDockWidget*>(n);
                w.removeDockWidget(d);
                w.addDockWidget(Qt::BottomDockWidgetArea, d);
                d->show();
            }
            pump(200);
            CHECK(w.tabifiedDockWidgets(media).isEmpty(), "chuan bi: bo cuc da tach tab");
            old = w.saveState();
        }
        appSettings().setValue("state", old);
        MainWindow w(p, engine, {});
        w.setAskOnClose(false);
        w.show();
        pump(600);
        const bool bigScreen = w.screen()->availableGeometry().width() >= 1800;
        CHECK(w.layoutId() == (bigScreen ? "big" : "prepare"), "bo cuc cu bi bo qua: chon theo man hinh");
        CHECK(!w.tabifiedDockWidgets(dock(w, "dockScenes")).isEmpty() && w.tabifiedDockWidgets(dock(w, "dockScenes")).contains(dock(w, "dockPlaylist")),
              "Scenes / Timeline / Playlist van la tab");
        checkLayout(w, {1536, 780}, "sau-bo-cuc-cu");
    }
    engine.stopEngine();
    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
