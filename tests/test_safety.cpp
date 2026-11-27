// An toan khi dang dien: Enter trong o nhap khong CUT; video phat tu dau khi len Program; dong cua so Output mo lai duoc va
// khong chan viec thoat; thieu file media van mo duoc project.
#include "MainWindow.h"
#include "core/AppSettings.h"
#include "media/FFmpegSource.h"
#include "media/StaticSources.h"
#include "render/OutputWindow.h"
#include <QAbstractSpinBox>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QDockWidget>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
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
template <class F> static bool waitFor(F cond, int ms = 4000) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { pump(10); if (cond()) return true; }
    return cond();
}
// Qt hoi widget dang co tieu diem "co giu phim nay khong" truoc khi kich hoat phim tat.
static bool keepsKey(QWidget* w, int key) {
    QKeyEvent ev(QEvent::ShortcutOverride, key, Qt::NoModifier);
    ev.ignore();
    QApplication::sendEvent(w, &ev);
    return ev.isAccepted();
}
// Video thu 160x90, 25 hinh/giay, 6 giay (khong dung lavfi: ban FFmpeg nay khong co).
static bool makeClip(const QTemporaryDir& dir, const QString& name) {
    const QString raw = dir.filePath(name + ".rgb");
    QFile f(raw);
    if (!f.open(QIODevice::WriteOnly)) return false;
    for (int i = 0; i < 150; ++i) f.write(QByteArray(160 * 90 * 3, char(40 + i)));
    f.close();
    const QString out = dir.filePath(name + ".mp4");
    return QProcess::execute("ffmpeg", {"-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "160x90", "-r", "25", "-i", raw,
                                        "-pix_fmt", "yuv420p", out}) == 0 && QFile::exists(out);
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
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    const bool haveFfmpeg = makeClip(dir, "a") && makeClip(dir, "b") && makeClip(dir, "c");

    Project p;
    auto addScene = [&](const QString& name, std::shared_ptr<Source> src, bool restart) {
        auto s = std::make_shared<Scene>();
        s->name = name; s->size = {160, 90}; s->restartOnAir = restart;
        s->layers.push_back({std::move(src), {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        p.scenes.push_back(s);
    };
    std::shared_ptr<FFmpegSource> va, vb, vc;
    if (haveFfmpeg) {
        auto video = [&](const QString& n) {
            SourceDef d; d.type = "video"; d.path = dir.filePath(n + ".mp4");
            return std::dynamic_pointer_cast<FFmpegSource>(p.addSource(d));
        };
        va = video("a"); vb = video("b"); vc = video("c");
        addScene("A", va, true);              // 0: Program
        addScene("B", vb, true);              // 1: Preview
        addScene("C chay lien tuc", vc, false);
        addScene("A lan hai", va, true);      // 3: dung chung video voi A
    } else {
        std::cout << "skip: khong co ffmpeg de tao video thu\n";
        SourceDef d; d.type = "color"; d.color = Qt::red;
        auto src = p.addSource(d);
        addScene("1", src, true);
        addScene("2", src, true);
    }
    auto ch = std::make_shared<Channel>();
    ch->name = "K"; ch->size = {160, 90}; ch->scenes = p.scenes; ch->program = 0; ch->preview = 1;
    p.channels = {ch};
    OutputConfig o; o.name = "O1"; o.size = {160, 90};
    p.outputs = {o};

    RenderEngine engine(p);
    engine.startEngine();
    {
        MainWindow w(p, engine, {});
        w.setAskOnClose(false);
        w.show();
        pump(1500);
        auto post = [&](auto fn) { engine.post(fn); pump(80); };

        // ---------- Enter trong o nhap ----------
        const auto spins = w.findChildren<QAbstractSpinBox*>();
        int kept = 0;
        for (QAbstractSpinBox* s : spins) kept += keepsKey(s, Qt::Key_Return) && keepsKey(s, Qt::Key_Enter);
        CHECK(spins.size() > 10 && kept == spins.size(),
              "Enter trong MOI o so cua cua so chinh khong kich hoat CUT (" + std::to_string(kept) + "/" + std::to_string(spins.size()) + ")");
        int keptEdit = 0;
        const auto edits = w.findChildren<QLineEdit*>();
        for (QLineEdit* e : edits) keptEdit += keepsKey(e, Qt::Key_Return);
        CHECK(keptEdit == edits.size(), "Enter trong o chu cung vay");
        CHECK(!keepsKey(w.findChild<QListWidget*>("sceneList"), Qt::Key_Return), "ngoai o nhap, Enter van la phim tat CUT");

        // ---------- video: Preview van chay, len Program thi phat tu dau ----------
        if (haveFfmpeg) {
            // khung thu i cua video thu co do xam 40 + i, nen do sang cho biet dang o doan nao
            auto level = [](const std::shared_ptr<FFmpegSource>& v) { const QImage im = v->snapshot(32); return im.isNull() ? -1 : qRed(im.pixel(1, 1)); };
            CHECK(va->presentedTime() > 0.6, "video o Program dang chay");
            const double pv0 = vb->presentedTime();
            pump(300);
            CHECK(pv0 > 0.6 && vb->presentedTime() > pv0 + 0.15, "video o Preview VAN CHAY");
            const int midLevel = level(vb);
            engine.post([&](double now) { ch->cut(now); });
            pump(35);                                          // hai khung render: du de tua, chua du de chay toi doan giua
            const int atCut = level(vb);
            CHECK(midLevel > 60 && atCut > 20 && atCut < midLevel - 15,
                  "CUT: hien NGAY khung dau, khong chop den, khong lo doan giua (truoc " + std::to_string(midLevel) + ", sau " + std::to_string(atCut) + ")");
            pump(300);
            CHECK(vb->ready() && vb->presentedTime() > 0.05 && vb->presentedTime() < 0.9 && level(vb) < 75,
                  "CUT: phat tu dau (dong ho " + std::to_string(vb->presentedTime()) + " s, do sang " + std::to_string(level(vb)) + ")");
            CHECK(waitFor([&] { return vb->presentedTime() > 0.8; }), "va chay tiep");
            const double left = va->presentedTime();
            pump(300);
            CHECK(va->presentedTime() > left, "video vua roi Program (sang Preview) van chay");
            post([&](double now) { ch->cut(now); });
            CHECK(va->presentedTime() < 0.6, "CUT lai: A phat tu dau, khong tiep tuc doan cu");
            pump(1000);
            const double before = va->presentedTime();
            post([&](double now) { ch->setPreview(3); ch->cut(now); });
            pump(300);
            CHECK(va->presentedTime() > before, "hai Scene dung chung video: chuyen qua lai khong giat ve dau");
            pump(500);
            const double cont = vc->presentedTime();
            post([&](double now) { ch->setPreview(2); ch->cut(now); });
            pump(300);
            CHECK(cont > 1.5 && vc->presentedTime() > cont, "Scene bo chon 'Phat lai khi len Program': len song khong tua");
            const double aOut = va->presentedTime();
            post([&](double now) { ch->setPreview(1); ch->startTransition(TransitionType::Fade, 1.0, now); });
            CHECK(vb->presentedTime() < 0.6, "bat dau Fade: video moi phat tu dau");
            post([&](double now) { Q_UNUSED(now); });
            CHECK(va->presentedTime() >= aOut, "video da roi Program tu truoc khong bi anh huong");
        }

        // ---------- dong cua so Output ----------
        auto* out = dynamic_cast<OutputWindow*>(engine.output(0));
        auto* reopen = w.findChild<QToolButton*>("reopenOutput0");
        CHECK(out && reopen && out->isVisible() && !reopen->isVisibleTo(&w), "Output dang hien: khong co nut Mo lai");
        out->close();
        pump(900);
        CHECK(!out->isVisible() && out->handle() != nullptr, "dong cua so Output: chi an, khong huy cua so");
        auto* status = w.findChild<QPushButton*>("statusReopen");
        auto* outDock = w.findChild<QDockWidget*>("dockOutputs");
        w.applyLayout("prepare");                       // bo cuc an khung Outputs
        pump(900);
        CHECK(outDock->isHidden() && status && status->isVisibleTo(&w), "khung Outputs an: thanh trang thai van co nut Mo lai");
        w.applyLayout("live");
        pump(900);
        CHECK(!outDock->isHidden() && reopen->isVisibleTo(&w) && !status->isVisibleTo(&w), "bang Outputs hien nut Mo lai");
        reopen->click();
        pump(900);
        const uint64_t n = out->presented;
        pump(500);
        CHECK(out->isVisible() && out->presented > n, "bam Mo lai: cua so hien va ve tiep");
        CHECK(!reopen->isVisibleTo(&w), "nut Mo lai an di");

        // ---------- thoat khong bi cua so Output chan ----------
        QTimer::singleShot(100, &w, [&] { w.close(); });
        QTimer::singleShot(4000, &app, [] { QCoreApplication::exit(7); });
        CHECK(app.exec() == 0, "dong cua so chinh: ung dung thoat (cua so Output khong chan)");
    }
    engine.stopEngine();

    // ---------- thieu file media ----------
    {
        QTemporaryDir pd;
        QImage img(32, 18, QImage::Format_RGB32);
        img.fill(Qt::green);
        img.save(pd.filePath("co.png"));
        img.save(pd.filePath("se_mat.png"));
        Project q;
        SourceDef good; good.type = "image"; good.path = pd.filePath("co.png");
        SourceDef gone; gone.type = "image"; gone.path = pd.filePath("se_mat.png");
        auto g = q.addSource(good), l = q.addSource(gone);
        auto s = std::make_shared<Scene>();
        s->name = "S"; s->size = {32, 18};
        s->layers.push_back({g, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        s->layers.push_back({l, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        q.scenes = {s};
        auto qc = std::make_shared<Channel>();
        qc->name = "K"; qc->size = {32, 18}; qc->scenes = q.scenes; qc->program = 0;
        q.channels = {qc};
        OutputConfig qo; qo.name = "Q"; qo.size = {64, 36};
        q.outputs = {qo};
        QString err;
        const QString pj = pd.filePath("p.json");
        CHECK(saveProject(q, pj, &err), "luu project");
        QFile::remove(pd.filePath("se_mat.png"));
        Project r;
        const bool loaded = loadProject(r, pj, &err);
        CHECK(loaded && r.loadWarnings.size() == 1 && r.loadWarnings.value(0).contains("se_mat.png"),
              "thieu mot file: project VAN mo duoc va bao dung file (" + err.toStdString() + ")");
        CHECK(r.sources.size() == 2 && r.sources[0].source->error().isEmpty() && !r.sources[1].source->error().isEmpty() &&
                  !r.sources[1].source->snapshot(64).isNull(), "nguon thieu co hinh bao loi, nguon con lai binh thuong");
        CHECK(r.scenes.size() == 1 && r.scenes[0]->layers.size() == 2, "Scene va Layer giu nguyen");
        CHECK(saveProject(r, pj, &err), "luu lai");
        img.save(pd.filePath("se_mat.png"));
        Project again;
        CHECK(loadProject(again, pj, &err) && again.loadWarnings.isEmpty(), "chep file ve roi mo lai: het canh bao, khong mat tham chieu");

        QFile::remove(pd.filePath("se_mat.png"));
        Project u;
        loadProject(u, pj, &err);
        RenderEngine e2(u);
        e2.startEngine();
        {
            MainWindow w2(u, e2, {});
            w2.setAskOnClose(false);
            w2.show();
            pump(400);
            auto* box = w2.findChild<QMessageBox*>("missingFiles");
            CHECK(box && box->isVisible() && !box->isModal() && box->text().contains("se_mat.png"), "giao dien bao file thieu, khong chan thao tac");
        }
        e2.stopEngine();
    }

    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
