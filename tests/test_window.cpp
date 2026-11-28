// SRC-09: nguon "cua so" (slideshow PowerPoint) va CTL-02: PageDown / PageUp chuyen slide. Dung backend GIA, khong can
// PowerPoint, nen KHONG kiem chung duoc Windows Graphics Capture / COM that.
// Thu tay voi cua so that (T-PPT): dat LC_PPT_PROBE=<thu muc>, mo mot slideshow PowerPoint roi chay file test nay:
// no phong to cua so, bat bang backend that, bam slide tiep / lui, dua len Program trong cua so chinh, thu nho, mo lai,
// roi DONG slideshow; anh luu vao thu muc do.
#include "LiveDialogs.h"
#include "MainWindow.h"
#include "ScenePanel.h"
#include "Shortcuts.h"
#include "Theme.h"
#include "core/I18n.h"
#include "core/Project.h"
#include "live/LiveSource.h"
#include "live/SlideRemote.h"
#include "live/WindowCaptureFake.h"
#include "render/Engine.h"
#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <iostream>
#include <mutex>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

template <class F>
static bool waitFor(F f, int ms = 5000) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) {
        if (f()) return true;
        pump(10);
    }
    return f();
}

namespace {

class FakeSlides : public SlideBackend {
public:
    SlideStep step(int direction) override {
        std::lock_guard lk(m_);
        SlideStep r;
        if (!error.isEmpty()) { r.error = error; return r; }
        position += direction;
        steps.push_back(direction);
        r.ok = true;
        r.position = position;
        r.count = 20;
        return r;
    }
    std::vector<int> taken() { std::lock_guard lk(m_); return steps; }
    void setError(const QString& e) { std::lock_guard lk(m_); error = e; }
private:
    std::mutex m_;
    std::vector<int> steps;
    QString error;
    int position = 5;
};

CaptureWindow win(quint64 h, const char* app, const char* cls, const char* title, bool show = false) {
    return {h, app, cls, title, show};
}

void matching() {
    const std::vector<CaptureWindow> w = {win(1, "POWERPNT.EXE", "PPTFrameClass", "Bai.pptx - PowerPoint"),
                                          win(2, "POWERPNT.EXE", "PPTFrameClass", "PowerPoint Slide Show - Bai", true),
                                          win(3, "notepad.exe", "Notepad", "a.txt"), win(4, "chrome.exe", "Chrome", "Mot"),
                                          win(5, "chrome.exe", "Chrome", "Hai")};
    CHECK(findCaptureWindow(w, {kSlideShowTarget, {}, {}}) == 1, "slideshow: chon cua so dang chieu, khong chon cua so soan thao");
    CHECK(findCaptureWindow({w[0]}, {kSlideShowTarget, {}, {}}) == -1, "slideshow: chi co cua so soan thao -> chua co");
    CHECK(findCaptureWindow(w, {"NOTEPAD.EXE", "Notepad", "b.txt"}) == 2, "mot cua so cung app + lop: nhan du tieu de da doi");
    CHECK(findCaptureWindow(w, {"chrome.exe", "Chrome", "Hai"}) == 4, "nhieu cua so: dung tieu de");
    CHECK(findCaptureWindow(w, {"chrome.exe", "Chrome", "Ba"}) == -1, "nhieu cua so, khong trung tieu de: khong doan bua");
    CHECK(findCaptureWindow(w, {"obs.exe", "Qt", "x"}) == -1, "khong co chuong trinh");
}

void aspect() {
    const QRect a(10, 20, 1401, 693);
    const QRect r = fitAspectRect(a, 16.0 / 9);
    CHECK(r.height() == 693 && r.width() == 1232 && r.x() == 10 + (1401 - 1232) / 2 && r.y() == 20, "vung rong hon slide: bo vien hai ben");
    const QRect tall = fitAspectRect(QRect(0, 0, 800, 800), 16.0 / 9);
    CHECK(tall == QRect(0, 175, 800, 450), "vung cao hon slide: bo vien tren duoi");
    CHECK(fitAspectRect(a, 0) == a && fitAspectRect(a, -1) == a, "khong biet ti le: giu ca vung");
}

void liveBehaviour(const std::shared_ptr<FakeWindowCapture>& fake) {
    auto s = makeWindowSource({kSlideShowTarget, {}, {}});
    CHECK(s->kind() == "window" && s->name() == "Slide PowerPoint" && s->audio() == nullptr, "nguon cua so: loai, ten, khong co tieng");
    CHECK(waitFor([&] { return s->state() == LiveState::Unavailable; }), "chua co slideshow: dang cho");
    CHECK(s->statusText().contains("slideshow"), "trang thai noi ro dang cho slideshow");

    fake->addWindow(win(10, "POWERPNT.EXE", "screenClass", "Slide Show", true), 320, 180);
    CHECK(waitFor([&] { return s->isLive(); }), "mo slideshow sau khi them nguon: tu noi");
    CHECK(s->stats().width == 320 && s->stats().height == 180, "kich thuoc khung theo cua so");

    pump(3000);
    CHECK(s->isLive(), "slide dung yen 3 giay KHONG bi coi la mat tin hieu");

    fake->setColor(10, 0xff0000);
    CHECK(waitFor([&] { return s->snapshot(32).pixelColor(1, 1).red() > 200; }), "doi slide: hinh doi theo");

    fake->setMinimized(10, true);
    CHECK(waitFor([&] { return s->state() == LiveState::Lost; }), "thu nho cua so: bao mat tin hieu");
    CHECK(waitFor([&] { return s->statusText().contains(QString::fromUtf8("thu nhỏ")); }), "trang thai noi ro cua so dang thu nho");
    CHECK(s->snapshot(32).pixelColor(1, 1).red() > 200, "mat tin hieu: giu khung cuoi");
    fake->setMinimized(10, false);
    CHECK(waitFor([&] { return s->isLive(); }), "mo lai cua so: tu noi lai");

    fake->closeWindow(10);
    CHECK(waitFor([&] { return s->state() == LiveState::Unavailable; }), "tat slideshow: quay lai cho");
    CHECK(s->snapshot(32).pixelColor(1, 1).red() > 200, "tat slideshow: giu slide cuoi tren man");
    fake->addWindow(win(11, "POWERPNT.EXE", "PPTFrameClass", "Slide Show 2", true), 640, 360);
    CHECK(waitFor([&] { return s->isLive() && s->stats().width == 640; }), "chieu file khac: tu noi lai");
    fake->closeWindow(11);
}

void project(const QString& dir) {
    Project p;
    SourceDef d;
    d.type = "window";
    d.device = "notepad.exe";
    d.url = "Notepad";
    d.deviceName = "a.txt";
    d.label = "Ghi chu";
    QString err;
    CHECK(p.addSource(d, &err) != nullptr, "tao nguon cua so du cua so chua co");
    SourceDef show;
    show.type = "window";
    show.device = kSlideShowTarget;
    CHECK(p.addSource(show, &err) != nullptr, "tao nguon slideshow");
    auto c = std::make_shared<Channel>();
    c->name = "K1";
    p.channels.push_back(c);
    p.outputs.push_back({});
    const QString path = dir + "/win.lumacast";
    CHECK(saveProject(p, path, &err), "luu project");
    QFile f(path);
    CHECK(f.open(QIODevice::ReadOnly), "doc file");
    const QJsonArray srcs = QJsonDocument::fromJson(f.readAll()).object()["sources"].toArray();
    f.close();
    CHECK(srcs.size() == 2 && srcs[0].toObject()["type"] == "window" && srcs[0].toObject()["app"] == "notepad.exe" &&
          srcs[0].toObject()["windowClass"] == "Notepad" && srcs[0].toObject()["title"] == "a.txt", "ghi dung truong");
    CHECK(srcs[1].toObject()["app"] == kSlideShowTarget && !srcs[1].toObject().contains("title"), "slideshow: khong luu tieu de / HWND");
    Project q;
    CHECK(loadProject(q, path, &err), "mo lai project");
    CHECK(q.sources.size() == 2 && q.sources[0].device == "notepad.exe" && q.sources[0].url == "Notepad" &&
          q.sources[0].deviceName == "a.txt" && q.sources[0].source->name() == "Ghi chu", "doc lai dung");
    SourceDef bad;
    bad.type = "window";
    CHECK(!createSource(bad, &err) && !err.isEmpty(), "thieu ten chuong trinh: bao loi");
}

void ui(const std::shared_ptr<FakeWindowCapture>& fake, const std::shared_ptr<FakeSlides>& slides) {
    fake->addWindow(win(20, "notepad.exe", "Notepad", "a.txt"), 200, 100);
    {
        WindowSourceDialog dlg;
        auto* list = dlg.findChild<QListWidget*>("winList");
        CHECK(list && list->count() == 2 && dlg.target().isSlideShow(), "hop thoai: muc dau la slideshow tu nhan, duoc chon san");
        if (list) list->setCurrentRow(1);
        CHECK(dlg.target() == WindowTarget({"notepad.exe", "Notepad", "a.txt"}), "chon mot cua so dang mo");
    }

    Project p;
    auto c = std::make_shared<Channel>();
    c->name = "K1";
    c->size = QSize(640, 360);
    p.channels.push_back(c);
    RenderEngine engine(p);
    engine.startEngine();
    {
        MainWindow w(p, engine, {});
        w.setAskOnClose(false);
        w.applyKeys(shortcuts::defaults());
        w.resize(1280, 720);
        w.show();
        w.activateWindow();
        (void)QTest::qWaitForWindowActive(&w);

        CHECK(w.findChild<QAbstractButton*>("addWindowBtn") != nullptr, "nut them cua so tren bang Scenes");
        CHECK(w.addWindowSource({kSlideShowTarget, {}, {}}), "them nguon slideshow");
        CHECK(p.sources.size() == 1 && p.scenes.size() == 1 && c->scenes.size() == 1, "them nguon + mot Scene");
        CHECK(w.addWindowSource({kSlideShowTarget, {}, {}}) && p.sources.size() == 1, "them lai: dung Scene cu, khong nhan doi");
        w.undoStack()->undo();
        CHECK(waitFor([&] { return p.scenes.empty(); }), "hoan tac duoc");
        w.undoStack()->redo();
        CHECK(waitFor([&] { return p.scenes.size() == 1; }), "lam lai duoc");
        CHECK(!w.addWindowSource({}), "chua chon cua so: tu choi");

        QSignalSpy spy(&w.slides(), &SlideRemote::stepped);
        QTest::keyClick(&w, Qt::Key_PageDown);
        CHECK(waitFor([&] { return spy.count() == 1; }), "PageDown goi slide tiep");
        CHECK(slides->taken() == std::vector<int>{1}, "dung mot buoc tien");
        CHECK(w.statusBar()->currentMessage().contains("6/20"), "bao so slide o thanh trang thai");
        QTest::keyClick(&w, Qt::Key_PageUp);
        CHECK(waitFor([&] { return spy.count() == 2; }) && slides->taken() == std::vector<int>({1, -1}), "PageUp lui slide");

        // But van phai chay du nguoi van hanh vua bam vao bat ky dieu khien nao (phim di vao widget dang co focus).
        const QList<QWidget*> targets = {w.findChild<QListWidget*>("sceneList"), w.centralWidget()->findChild<QComboBox*>(),
                                         w.centralWidget()->findChild<QSlider*>(), w.centralWidget()->findChild<QDoubleSpinBox*>()};
        for (QWidget* target : targets) {
            if (!target) { CHECK(false, "thieu dieu khien de thu focus"); continue; }
            target->setFocus();
            pump(50);
            const int before = int(spy.count());
            QTest::keyClick(target, Qt::Key_PageDown);
            CHECK(waitFor([&] { return spy.count() == before + 1; }, 2000),
                  std::string("PageDown van chuyen slide khi dang chon ") + target->metaObject()->className());
        }
        spy.clear();
        QTest::keyClick(&w, Qt::Key_PageDown);
        QTest::keyClick(&w, Qt::Key_PageDown);
        CHECK(waitFor([&] { return spy.count() == 2; }), "bam hai lan: hai buoc");
        spy.clear();
        w.setFocus();

        w.setLocked(true);
        CHECK(!w.findChild<QAbstractButton*>("addWindowBtn")->isEnabled() && !w.addWindowSource({"a.exe", "b", "c"}), "khoa van hanh: khong them nguon");
        QTest::keyClick(&w, Qt::Key_PageDown);
        CHECK(waitFor([&] { return spy.count() == 1; }), "khoa van hanh: but trinh chieu van chay");
        w.setLocked(false);

        slides->setError(QString::fromUtf8("PowerPoint chưa mở"));
        QTest::keyClick(&w, Qt::Key_PageDown);
        CHECK(waitFor([&] { return spy.count() == 2; }) && w.statusBar()->currentMessage() == QString::fromUtf8("PowerPoint chưa mở"),
              "loi PowerPoint hien o thanh trang thai");
        slides->setError({});
        w.close();
    }
    engine.stopEngine();
}

void keys(const QString& dir) {
    const KeyMap def = shortcuts::defaults();
    CHECK(def.value("slide.next") == QKeySequence(Qt::Key_PageDown) && def.value("slide.prev") == QKeySequence(Qt::Key_PageUp), "mac dinh PageDown / PageUp");
    CHECK(!LiveAction::fromId("slide.next").externalAllowed() && !LiveAction::fromId("slide.prev").externalAllowed(), "chuyen slide chi tu phim tren may");
    QSettings st(dir + "/slide-keys.ini", QSettings::IniFormat);
    st.setValue("shortcuts/cut", "PgDown");
    const KeyMap l = shortcuts::load(st);
    CHECK(l.value("cut") == QKeySequence(Qt::Key_PageDown) && !l.contains("slide.next") && l.contains("slide.prev"),
          "cau hinh cu gan PageDown cho CUT: giu nguyen, 'slide tiep' nhuong");
}

// Thu voi PowerPoint that (khong chay trong ctest).
int probe(const QString& dir) {
    QDir().mkpath(dir);
    HWND show = nullptr;
    for (const CaptureWindow& w : windowCaptureBackend()->list())
        if (w.slideShow) show = reinterpret_cast<HWND>(quintptr(w.handle));
    if (!show) { std::cout << "khong thay cua so slideshow\n"; return 1; }
    ShowWindow(show, SW_MAXIMIZE);
    auto s = makeWindowSource({kSlideShowTarget, {}, {}});
    if (!waitFor([&] { return s->isLive(); }, 20000)) {
        std::cout << "khong bat duoc: " << s->statusText().toStdString() << "\n";
        return 1;
    }
    auto save = [&](const QString& name) {
        pump(2500);
        const LiveSource::Stats st = s->stats();
        const QImage img = s->snapshot(std::max(1, st.width - 1));
        img.save(dir + "/" + name + ".png");
        std::cout << name.toStdString() << ": " << st.width << "x" << st.height << " frames=" << st.videoFrames << " fps=" << st.fps
                  << " state=" << liveStateId(s->state()) << "\n";
    };
    SlideRemote remote;
    QSignalSpy spy(&remote, &SlideRemote::stepped);
    auto step = [&](int dir2) {
        const int n = spy.count();
        remote.step(dir2);
        if (!waitFor([&] { return spy.count() > n; }, 5000)) { std::cout << "step: khong co tra loi\n"; return; }
        const auto a = spy.last();
        std::cout << "step " << dir2 << ": ok=" << a[1].toBool() << " slide=" << a[2].toInt() << "/" << a[3].toInt() << " " << a[4].toString().toStdString() << "\n";
    };
    std::cout << "slide aspect=" << slideBackend()->slideAspect() << "\n";
    save("1-start");
    step(+1);
    save("2-next");
    step(+1);
    save("3-next");
    step(-1);
    save("4-prev");

    {   // ca duong trong app: nguon -> Scene -> Program -> man hinh Program cua cua so chinh
        Project p;
        auto c = std::make_shared<Channel>();
        c->name = "K1";
        c->size = QSize(1920, 1080);
        p.channels.push_back(c);
        RenderEngine engine(p);
        engine.startEngine();
        {
            MainWindow w(p, engine, {});
            w.setAskOnClose(false);
            w.resize(1400, 800);
            w.show();
            pump(500);
            w.addWindowSource({kSlideShowTarget, {}, {}});
            pump(1500);
            engine.post([c](double now) { c->setPreview(0); c->cut(now); });
            pump(2500);
            w.grab().save(dir + "/5-app.png");
            w.close();
        }
        engine.stopEngine();
    }

    auto report = [&](const char* what) {
        std::cout << what << ": state=" << liveStateId(s->state()) << " status=" << s->statusText().toStdString()
                  << " con-hinh=" << !s->snapshot(64).isNull() << "\n";
    };
    ShowWindow(show, SW_MINIMIZE);
    pump(1500);
    report("thu nho");
    ShowWindow(show, SW_RESTORE);
    waitFor([&] { return s->isLive(); }, 6000);
    pump(500);
    report("mo lai");
    PostMessageW(show, WM_CLOSE, 0, 0);
    pump(3000);
    report("dong slideshow");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc, argv);
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());

    const QString probeDir = qEnvironmentVariable("LC_PPT_PROBE");
    if (!probeDir.isEmpty()) return probe(probeDir);

    applyDarkTheme(app);
    auto fake = std::make_shared<FakeWindowCapture>();
    setWindowCaptureBackend(fake);
    auto slides = std::make_shared<FakeSlides>();
    setSlideBackend(slides);

    matching();
    aspect();
    liveBehaviour(fake);
    project(dir.path());
    keys(dir.path());
    ui(fake, slides);

    setWindowCaptureBackend(nullptr);
    setSlideBackend(nullptr);
    std::cout << (g_fail ? "FAILED " : "PASSED ") << g_fail << "\n";
    return g_fail ? 1 : 0;
}
