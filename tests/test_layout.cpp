// Bo cuc cua so chinh: phai vua man hinh nho (1920x1080 phong 125% = 1536x816 diem logic), cac khung khong de len nhau,
// va bo cuc cu da luu (truoc khi co khung moi) khong duoc ap lai. LC_SHOT_DIR=<thu muc> de luu anh chup giao dien.
#include "MainWindow.h"
#include "Theme.h"
#include "core/AppSettings.h"
#include "media/StaticSources.h"
#include <QApplication>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QScrollArea>
#include <QScrollBar>
#include <QMenuBar>
#include <QStatusBar>
#include <QTabBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
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

// Cac khung dang thuc su hien (tab dang bi che khong tinh) + vung giua.
static std::vector<std::pair<QString, QRect>> visibleAreas(MainWindow& w) {
    std::vector<std::pair<QString, QRect>> v;
    for (QDockWidget* d : w.findChildren<QDockWidget*>())
        if (!d->isFloating() && d->isVisible() && !d->visibleRegion().isEmpty()) v.push_back({d->objectName(), d->geometry()});
    v.push_back({"central", w.centralWidget()->geometry()});
    return v;
}

static void checkLayout(MainWindow& w, const QSize& size, const char* name) {
    w.resize(size);
    pump(500);
    const std::string tag = std::string(name) + ": ";
    CHECK(w.width() <= size.width() && w.height() <= size.height(),
          tag + "cua so co duoc ve " + str(size).toStdString() + " (thuc te " + str(w.size()).toStdString() + ")");
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
    QStringList squeezed;
    for (QDockWidget* d : w.findChildren<QDockWidget*>())
        if (d->isVisible() && !d->visibleRegion().isEmpty() && d->widget()) {
            const QSize need = d->widget()->minimumSizeHint(), got = d->widget()->size();
            if (need.width() > got.width() + 1 || need.height() > got.height() + 1)
                squeezed << d->objectName() + " can " + str(need) + " co " + str(got);
        }
    CHECK(squeezed.isEmpty(), tag + "noi dung khung khong bi ep " + squeezed.join("; ").toStdString());
    if (qEnvironmentVariableIsSet("LC_SHOT_DIR")) w.grab().save(qEnvironmentVariable("LC_SHOT_DIR") + "/" + name + ".png");
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
    applyDarkTheme(app);

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

    RenderEngine engine(p);
    engine.startEngine();
    {
        CHECK(appSettings().fileName().startsWith(dir.path()), "test dung cai dat rieng, khong dung registry that");
        if (g_fail) return 1;
        MainWindow w(p, engine, {});
        w.setAskOnClose(false);
        w.show();
        pump(600);
        std::cout << "minimumSizeHint cua so: " << str(w.minimumSizeHint()).toStdString() << "  menu " << str(w.menuBar()->sizeHint()).toStdString()
                  << "  thanh trang thai " << str(w.statusBar()->minimumSizeHint()).toStdString()
                  << "  vung giua " << str(w.centralWidget()->minimumSizeHint()).toStdString() << "\n";
        for (QDockWidget* d : w.findChildren<QDockWidget*>())
            std::cout << "  " << d->objectName().toStdString() << " toi thieu " << str(d->minimumSizeHint()).toStdString() << "\n";
        CHECK(w.minimumSizeHint().width() <= 1536 && w.minimumSizeHint().height() <= 780, "kich thuoc toi thieu vua man 1920x1080 phong 125%");

        checkLayout(w, {1536, 780}, "man-125");
        checkLayout(w, {1920, 1000}, "man-lon");

        // mo tung khung dang an trong tab: van khong de, khong bi ep
        for (QDockWidget* d : w.findChildren<QDockWidget*>()) {
            d->raise();
            checkLayout(w, {1536, 780}, d->objectName().toUtf8().constData());
        }

        auto* tl = w.findChild<QDockWidget*>("dockTimeline");
        tl->raise();
        pump(300);
        auto* sa = qobject_cast<QScrollArea*>(tl->widget());
        CHECK(sa && sa->verticalScrollBar()->maximum() == 0 && sa->horizontalScrollBar()->maximum() == 0, "man nho: Timeline hien du, khong phai cuon (cuon " + std::to_string(sa ? sa->verticalScrollBar()->maximum() : -1) + "/" + std::to_string(sa ? sa->horizontalScrollBar()->maximum() : -1) +
                  ", vung " + str(sa ? sa->viewport()->size() : QSize()).toStdString() + ", can " + str(sa ? sa->widget()->minimumSizeHint() : QSize()).toStdString() + ", cua so " + str(w.size()).toStdString() + ")");
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
        pump(500);
        CHECK(w.tabifiedDockWidgets(w.findChild<QDockWidget*>("dockScenes")).size() == 2, "bo cuc cu bi bo qua: Scenes / Timeline / Playlist van la tab");
        CHECK(w.tabifiedDockWidgets(w.findChild<QDockWidget*>("dockOutputs")).size() == 1, "Outputs / Am thanh van la tab");
        CHECK(w.tabifiedDockWidgets(w.findChild<QDockWidget*>("dockInspector")).size() == 2, "Inspector / Plug-in / Overlay van la tab");
        checkLayout(w, {1536, 780}, "sau-bo-cuc-cu");
    }
    engine.stopEngine();
    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
