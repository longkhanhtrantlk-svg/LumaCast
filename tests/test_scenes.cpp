// Test danh sach Scenes (UI-02): dieu khien phat video (tam dung / tua / lap), tieng rieng tung Scene, mot file = mot Scene,
// o Scene (bam = Preview, nhap dup = CUT, vung nut khong lot xuong), doi ten / xoa co Hoan tac, Khoa van hanh.
// Phan video can ffmpeg trong PATH (tao file tu khung tho). LC_SHOT_DIR=<thu muc> de luu anh chup giao dien.
#include "MainWindow.h"
#include "ScenePanel.h"
#include "Theme.h"
#include "audio/AudioPlan.h"
#include "core/AppSettings.h"
#include "media/FFmpegSource.h"
#include "media/StaticSources.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QProcess>
#include <QSettings>
#include <QSlider>
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
static bool near(double a, double b, double eps) { return std::abs(a - b) <= eps; }

// Mau o giua anh chup cua nguon: 'r','g','b','w' (mau cua tung giay trong clip thu), '?' = khac / chua co hinh.
static char colorOf(const QImage& im) {
    if (im.isNull()) return '?';
    const QColor c = im.pixelColor(im.width() / 2, im.height() / 2);
    const bool r = c.red() > 180, g = c.green() > 180, b = c.blue() > 180;
    return r && g && b ? 'w' : r && !g && !b ? 'r' : g && !r && !b ? 'g' : b && !r && !g ? 'b' : '?';
}

// Clip 4 giay 160x90 25fps: do, xanh la, xanh duong, trang (moi mau 1 giay) + tieng. Tra false neu khong co ffmpeg.
static bool makeClip(const QString& dir, const QString& out) {
    const QString raw = dir + "/v.raw", pcm = dir + "/a.raw";
    {
        QFile f(raw);
        if (!f.open(QIODevice::WriteOnly)) return false;
        for (const char* px : {"\xff\x00\x00", "\x00\xff\x00", "\x00\x00\xff", "\xff\xff\xff"}) {
            const QByteArray frame = QByteArray(px, 3).repeated(160 * 90);
            for (int i = 0; i < 25; ++i) f.write(frame);
        }
        QFile a(pcm);
        if (!a.open(QIODevice::WriteOnly)) return false;
        std::vector<int16_t> s(48000 * 2 * 4);
        for (size_t i = 0; i < s.size() / 2; ++i) s[i * 2] = s[i * 2 + 1] = int16_t(8000 * std::sin(double(i) * 2 * 3.14159265 * 440 / 48000));
        a.write(reinterpret_cast<const char*>(s.data()), qint64(s.size() * sizeof(int16_t)));
    }
    return QProcess::execute("ffmpeg", {"-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "160x90", "-r", "25", "-i", raw,
                                        "-f", "s16le", "-ar", "48000", "-ac", "2", "-i", pcm, "-pix_fmt", "yuv420p", out}) == 0 && QFile::exists(out);
}

// ---------------- 1. bo giai ma: tam dung / tua / lap ----------------
static void testTransport(const QString& clip) {
    FFmpegSource src(clip);
    CHECK(waitFor([&] { return src.ready() && src.durationSeconds() > 3.5; }), "video thu mo duoc, thoi luong ~4 s");
    CHECK(src.hasTransport() && !src.paused() && !src.ended(), "video co dieu khien phat; mac dinh dang phat");

    src.setPaused(true);
    pump(150);
    const double p0 = src.positionSeconds();
    pump(500);
    CHECK(src.paused() && near(src.positionSeconds(), p0, 1e-9), "tam dung: vi tri dung han");

    src.seekTo(1.5);
    CHECK(waitFor([&] { return colorOf(src.snapshot(64)) == 'g'; }), "tua toi 1,5 s khi dang tam dung: hien dung khung (xanh la)");
    pump(400);
    CHECK(src.paused() && near(src.positionSeconds(), 1.5, 0.1) && colorOf(src.snapshot(64)) == 'g', "sau khi tua van dung yen o 1,5 s");
    src.seekTo(2.5);
    CHECK(waitFor([&] { return colorOf(src.snapshot(64)) == 'b'; }) && near(src.positionSeconds(), 2.5, 0.1), "tua tiep 2,5 s: khung xanh duong");
    src.seekTo(0.4);
    CHECK(waitFor([&] { return colorOf(src.snapshot(64)) == 'r'; }), "tua lui 0,4 s: khung do");

    src.setPaused(false);
    CHECK(waitFor([&] { return src.positionSeconds() > 1.2; }, 3000), "phat tiep tu cho da tua");

    // khong lap: het file thi giu khung cuoi
    src.setLooping(false);
    src.seekTo(3.4);
    CHECK(waitFor([&] { return src.ended(); }, 3000), "khong lap: bao het file");
    CHECK(near(src.positionSeconds(), src.durationSeconds(), 0.05) && colorOf(src.snapshot(64)) == 'w', "het file: vi tri = thoi luong, giu khung cuoi");
    pump(500);
    CHECK(src.ended() && colorOf(src.snapshot(64)) == 'w', "van giu khung cuoi, khong tu lap");
    src.setPaused(false);
    CHECK(waitFor([&] { return !src.ended() && src.positionSeconds() < 1.0 && colorOf(src.snapshot(64)) == 'r'; }), "bam Phat khi da het: phat lai tu dau");

    src.setLooping(true);
    src.seekTo(3.4);
    CHECK(waitFor([&] { return colorOf(src.snapshot(64)) == 'r' && src.positionSeconds() < 1.0; }, 3000) && !src.ended(), "lap: het file quay lai dau");

    // phat lai khi len Program luon bo tam dung
    src.seekTo(2.5);
    src.setPaused(true);
    waitFor([&] { return colorOf(src.snapshot(64)) == 'b'; });
    src.rewind();
    CHECK(!src.paused() && waitFor([&] { return colorOf(src.snapshot(64)) == 'r' && src.positionSeconds() > 0.2; }, 3000), "rewind(): ve dau, bo tam dung, dang phat");
}

static void testAudioPause() {
    AudioStream s(4800);
    std::vector<float> in(200, 0.5f), out(200, 1.0f);
    s.push(in.data(), 100);
    s.setPaused(true);
    CHECK(s.pull(out.data(), 100) == 0 && out[0] == 0.0f && s.queued() == 100, "AudioStream tam dung: ra im lang, khong tieu du lieu");
    s.setPaused(false);
    CHECK(s.pull(out.data(), 100) == 100 && out[0] == 0.5f, "bo tam dung: phat tiep dung du lieu cu");
}

// ---------------- 2. tieng rieng tung Scene + luu/mo project ----------------
static void testSceneAudio(const QString& dir, const QString& clip) {
    Project p;
    SourceDef d;
    d.type = clip.isEmpty() ? "color" : "video";
    d.path = clip;
    auto src = p.addSource(d);
    for (int i = 0; i < 2; ++i) {
        auto s = std::make_shared<Scene>();
        s->name = QString("S%1").arg(i);
        s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Fit, true});
        p.scenes.push_back(s);
    }
    auto ch = std::make_shared<Channel>();
    ch->name = "K"; ch->scenes = p.scenes; ch->program = 0; ch->preview = 1;
    p.channels = {ch};
    p.outputs = {OutputConfig{}};

    auto gain = [&] { return buildAudioPlan(p, 0)->entries.at(0).gain; };
    CHECK(near(gain(), 1.0, 1e-6), "mac dinh: Scene co tieng ra loa 100% khi o Program");
    p.scenes[0]->audio.volume = 0.5f;
    CHECK(near(gain(), 0.5, 1e-6), "am luong Scene 50% -> he so tron 0,5");
    p.scenes[0]->audio.mute = true;
    CHECK(near(gain(), 0.0, 1e-6), "tat tieng Scene o Program -> im");
    p.scenes[0]->audio.mute = false;
    p.scenes[1]->audio.volume = 1.5f;
    CHECK(near(gain(), 0.5, 1e-6), "Scene o Preview khong ra loa du de am luong 150%");

    p.scenes[1]->audio.mute = true;
    const QString path = dir + "/scene-audio.json";
    QString err;
    CHECK(saveProject(p, path, &err), "luu project co tieng rieng tung Scene");
    Project q;
    CHECK(loadProject(q, path, &err) && near(q.scenes[0]->audio.volume.load(), 0.5, 1e-6) && !q.scenes[0]->audio.mute.load()
              && near(q.scenes[1]->audio.volume.load(), 1.5, 1e-6) && q.scenes[1]->audio.mute.load(),
          "mo lai: giu am luong / tat tieng cua tung Scene");
    p.scenes[0]->audio.volume = 1.0f;
    p.scenes[1]->audio.volume = 1.0f;
    p.scenes[1]->audio.mute = false;
    saveProject(p, path, &err);
    QFile f(path);
    CHECK(f.open(QIODevice::ReadOnly) && !f.readAll().contains("\"mute\""), "gia tri mac dinh khong ghi vao file (project cu mo y nhu truoc)");
}

// ---------------- 3. cua so chinh ----------------
static void sendMouse(QWidget* w, QEvent::Type type, Qt::MouseButton btn = Qt::LeftButton) {
    const QPointF pos(w->width() / 2.0, w->height() / 2.0);
    QMouseEvent ev(type, pos, w->mapToGlobal(pos), btn, type == QEvent::MouseButtonRelease ? Qt::NoButton : btn, Qt::NoModifier);
    QApplication::sendEvent(w, &ev);                       // QApplication chuyen len widget cha neu khong ai nhan
}
static void click(QWidget* w, Qt::MouseButton btn = Qt::LeftButton) {
    sendMouse(w, QEvent::MouseButtonPress, btn);
    sendMouse(w, QEvent::MouseButtonRelease, btn);
}
static void doubleClick(QWidget* w) {
    click(w);
    sendMouse(w, QEvent::MouseButtonDblClick);
    sendMouse(w, QEvent::MouseButtonRelease);
}

static void testWindow(const QString& dir, const QString& clip) {
    auto png = [&](const QString& name, const QColor& c) {
        QImage img(32, 18, QImage::Format_RGB32);
        img.fill(c);
        const QString path = dir + "/" + name;
        img.save(path);
        return path;
    };
    const QString red = png("do.png", Qt::red), blue = png("xanh.png", Qt::blue), yellow = png("vang.png", Qt::yellow);

    Project p;
    auto addScene = [&](const QString& type, const QString& path, const QString& name) {
        SourceDef d;
        d.type = type;
        d.path = path;
        auto src = p.addSource(d);
        auto s = std::make_shared<Scene>();
        s->name = name; s->size = {320, 180};
        s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        p.scenes.push_back(s);
    };
    addScene("image", red, "Do");
    addScene("image", blue, "Xanh");
    const bool video = !clip.isEmpty();
    if (video) addScene("video", clip, "Clip");
    auto ch = std::make_shared<Channel>();
    ch->name = "K"; ch->size = {320, 180}; ch->scenes = p.scenes; ch->program = 0; ch->preview = 1;
    p.channels = {ch};
    OutputConfig o; o.name = "O1"; o.size = {160, 90};
    p.outputs = {o};
    p.filePath = dir + "/scenes.json";

    RenderEngine engine(p);
    engine.startEngine();
    {
        MainWindow w(p, engine, {});
        w.setAskOnClose(false);
        w.resize(1536, 780);
        w.show();
        pump(800);
        ScenePanel* panel = w.scenePanel();
        QListWidget* list = panel->list();
        const int n0 = int(p.scenes.size());
        CHECK(list->count() == n0 && panel->card(0) && panel->card(1), "moi Scene mot o trong danh sach");
        CHECK(panel->card(0)->mark() == SceneCard::Mark::Program && panel->card(1)->mark() == SceneCard::Mark::Preview, "o Scene danh dau PGM / PVW");
        CHECK(waitFor([&] { return !engine.listThumbs().images.empty() && !engine.listThumbs().images[0].isNull(); }), "render thread ve anh thu nho cho danh sach");
        CHECK(engine.sceneThumbs().channel == -1, "chua mo Multiview: anh cua Multiview van rong");

        auto child = [&](int row, const char* name) { return panel->card(row)->findChild<QWidget*>(name); };
        auto tool = [&](int row, const char* name) { return panel->card(row)->findChild<QToolButton*>(name); };

        // bam anh = Preview, nhap dup = CUT
        w.undoStack()->clear();
        click(child(0, "scThumb"));
        CHECK(waitFor([&] { return ch->preview == 0; }) && ch->program == 0, "bam anh Scene: vao Preview, Program khong doi");
        click(child(1, "scName"));
        CHECK(waitFor([&] { return ch->preview == 1; }), "bam ten Scene: vao Preview");
        doubleClick(child(1, "scThumb"));
        CHECK(waitFor([&] { return ch->program == 1; }), "nhap dup anh Scene: CUT len Program");
        CHECK(waitFor([&] { return panel->card(1)->mark() == SceneCard::Mark::Program; }), "nhan PGM theo Program moi");

        // vung nut dieu khien khong lot xuong danh sach (ke ca nut dang bi mo cua anh)
        click(child(1, "scThumb"));
        pump(150);
        const int pv = ch->preview, pg = ch->program;
        for (const char* name : {"scControls", "scPlay", "scSeek", "scMute", "scLoop"}) doubleClick(child(0, name));
        pump(300);
        CHECK(ch->preview == pv && ch->program == pg, "bam/nhap dup vung dieu khien: khong doi Preview, khong CUT");
        click(child(0, "scThumb"), Qt::RightButton);
        pump(100);
        CHECK(ch->preview == pv, "chuot phai: khong doi Preview");
        CHECK(!tool(0, "scPlay")->isEnabled() && !tool(0, "scMute")->isEnabled() && tool(0, "scRename")->isEnabled() && tool(0, "scDelete")->isEnabled(),
              "Scene anh: phat/tieng bi mo, doi ten/xoa dung duoc");
        for (const char* name : {"scPlay", "scRewind", "scMute", "scLoop", "scRename", "scDelete"})
            CHECK(tool(0, name)->focusPolicy() == Qt::NoFocus && !tool(0, name)->toolTip().isEmpty(), std::string(name) + ": co tooltip, khong chiem ban phim");

        if (video) {
            auto src = p.sources[2].source;
            CHECK(waitFor([&] { return tool(2, "scPlay")->isEnabled() && tool(2, "scMute")->isEnabled(); }), "Scene video: nut phat va tieng bat");
            CHECK(waitFor([&] { return tool(2, "scPlay")->toolTip() == "Tạm dừng"; }) && !p.scenes[2]->audio.mute.load() && !tool(2, "scMute")->isChecked(),
                  "video: dang phat, mac dinh bat tieng");
            tool(2, "scPlay")->click();
            CHECK(waitFor([&] { return src->paused(); }) && waitFor([&] { return tool(2, "scPlay")->toolTip() == "Phát"; }), "nut Tam dung: dung video, nut doi thanh Phat");
            auto* seek = panel->card(2)->findChild<QSlider*>("scSeek");
            seek->setSliderDown(true);
            seek->setValue(600);
            seek->setSliderDown(false);
            CHECK(waitFor([&] { return near(src->positionSeconds(), 0.6 * src->durationSeconds(), 0.15) && colorOf(src->snapshot(64)) == 'b'; }),
                  "keo thanh tua toi 60%: video nhay toi khung do");
            CHECK(waitFor([&] { return panel->card(2)->findChild<QLabel*>("scTime")->text() == "-0:02"; }), "hien thoi gian con lai");
            tool(2, "scRewind")->click();
            CHECK(waitFor([&] { return src->positionSeconds() < 0.1 && src->paused(); }), "Ve dau: ve 0, van tam dung");
            tool(2, "scPlay")->click();
            CHECK(waitFor([&] { return !src->paused() && src->positionSeconds() > 0.3; }), "nut Phat: chay lai");
            tool(2, "scMute")->click();
            CHECK(p.scenes[2]->audio.mute.load(), "nut loa: tat tieng Scene");
            tool(2, "scMute")->click();
            panel->card(2)->findChild<QSlider*>("scVolume")->setValue(40);
            CHECK(!p.scenes[2]->audio.mute.load() && near(p.scenes[2]->audio.volume.load(), 0.4, 1e-6), "thanh am luong cua Scene");
            tool(2, "scLoop")->click();
            CHECK(waitFor([&] { return !p.scenes[2]->loop; }) && w.undoStack()->canUndo(), "tat Lap: ghi vao Scene, co Hoan tac");
            tool(2, "scRewind")->click();
            CHECK(waitFor([&] { return src->ended(); }, 7000), "khong lap: video dung o cuoi");
            w.undoStack()->undo();
            CHECK(waitFor([&] { return p.scenes[2]->loop && !src->ended(); }) && waitFor([&] { return tool(2, "scLoop")->isChecked(); }),
                  "Hoan tac: lap lai, video chay tiep");

            // len Program thi phat lai tu dau, ke ca dang tam dung o giua
            src->seekTo(2.5);
            src->setPaused(true);
            waitFor([&] { return colorOf(src->snapshot(64)) == 'b'; });
            doubleClick(child(2, "scThumb"));
            CHECK(waitFor([&] { return ch->program == 2 && !src->paused() && src->positionSeconds() > 0.1 && src->positionSeconds() < 1.5; }),
                  "CUT video dang tam dung: len Program va phat lai tu dau");
            doubleClick(child(1, "scThumb"));
            waitFor([&] { return ch->program == 1; });
        }

        // ---- mot file = mot Scene ----
        w.undoStack()->clear();
        CHECK(w.addMediaFile(red) && w.addMediaFile(blue), "nhap lai file da co");
        pump(200);
        CHECK(int(p.scenes.size()) == n0 && w.undoStack()->count() == 0, "file da co Scene: khong tao Scene thu hai");
        CHECK(w.addMediaFile(yellow) && waitFor([&] { return list->count() == n0 + 1; }), "nhap file moi: them mot o");
        const int yi = n0;                                                  // chi so Scene vang
        auto sc = p.scenes.back();

        // ---- doi ten ----
        const int pg1 = ch->program, pv1 = ch->preview;
        panel->card(yi)->startRename();
        auto* edit = panel->card(yi)->findChild<QLineEdit*>("scNameEdit");
        CHECK(edit && panel->card(yi)->renaming() && edit->text() == sc->name, "doi ten: o nhap hien ten hien tai");
        edit->setText("Backdrop vàng");
        QKeyEvent over(QEvent::ShortcutOverride, Qt::Key_Return, Qt::NoModifier);
        over.ignore();
        QApplication::sendEvent(edit, &over);
        CHECK(over.isAccepted(), "Enter trong o doi ten khong thanh phim CUT");
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(edit, &enter);
        CHECK(waitFor([&] { return sc->name == "Backdrop vàng"; }) && ch->program == pg1 && ch->preview == pv1, "Enter xac nhan ten moi, Program/Preview khong doi");
        CHECK(waitFor([&] { return list->item(yi)->text().contains("Backdrop vàng"); }) && w.controlApi().state()["scenes"].toArray()[yi].toObject()["name"].toString() == "Backdrop vàng",
              "ten moi hien o danh sach va API dieu khien ngoai");
        w.undoStack()->undo();
        CHECK(waitFor([&] { return sc->name == "vang.png"; }), "Hoan tac doi ten");
        w.undoStack()->redo();
        waitFor([&] { return sc->name == "Backdrop vàng"; });
        panel->card(yi)->startRename();
        edit = panel->card(yi)->findChild<QLineEdit*>("scNameEdit");
        edit->setText("bo");
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(edit, &esc);
        pump(150);
        CHECK(sc->name == "Backdrop vàng" && !panel->card(yi)->renaming(), "Esc huy doi ten");

        // ---- xoa ----
        CHECK(!w.deleteScene(p.scenes[size_t(ch->program.load())]) && int(p.scenes.size()) == n0 + 1, "khong xoa duoc Scene dang o Program");
        ch->playlist.data.items.clear();
        engine.postAndWait([&](double) { PlaylistItem it; it.id = newPlaylistItemId(); it.scene = sc; ch->playlist.data.items.push_back(it); });
        click(child(yi, "scThumb"));
        waitFor([&] { return ch->preview == yi; });
        const int sources = int(p.sources.size()), stack = w.undoStack()->count();
        tool(yi, "scDelete")->click();
        CHECK(waitFor([&] { return int(p.scenes.size()) == n0 && list->count() == n0; }), "nut thung rac: xoa Scene khoi danh sach");
        CHECK(int(p.sources.size()) == sources - 1 && int(ch->scenes.size()) == n0 && ch->preview == -1 && ch->program == pg1 && ch->playlist.data.items.empty(),
              "xoa: go nguon khong con ai dung va muc playlist; Preview rong, Program khong doi");
        CHECK(w.undoStack()->count() == stack + 1 && w.undoStack()->undoText().contains("Xoá Scene"), "xoa la mot buoc Hoan tac");
        w.undoStack()->undo();
        CHECK(waitFor([&] { return int(p.scenes.size()) == n0 + 1 && list->count() == n0 + 1; }) && p.scenes.back() == sc && ch->scenes.back() == sc
                  && int(p.sources.size()) == sources && ch->playlist.data.items.size() == 1 && ch->program == pg1,
              "Hoan tac xoa: tra lai Scene, nguon, muc playlist dung cho; khong dua gi len Program");

        // xoa Scene o DAU danh sach (dang o Preview): chi so Program lui theo, Inspector/Timeline khong loi
        auto first = p.scenes[0];
        auto onAir = p.scenes[size_t(ch->program.load())];
        CHECK(w.deleteScene(first) && waitFor([&] { return int(p.scenes.size()) == n0; }), "xoa Scene dau danh sach");
        CHECK(ch->scenes[size_t(ch->program.load())] == onAir, "Program van la Scene cu du chi so doi");
        w.undoStack()->undo();
        CHECK(waitFor([&] { return int(p.scenes.size()) == n0 + 1; }) && p.scenes[0] == first && ch->scenes[size_t(ch->program.load())] == onAir,
              "Hoan tac: chen lai dung vi tri dau, Program van dung");
        w.undoStack()->redo();
        CHECK(waitFor([&] { return int(p.scenes.size()) == n0; }), "Lam lai xoa");
        w.undoStack()->undo();
        waitFor([&] { return int(p.scenes.size()) == n0 + 1; });

        // ---- khoa van hanh ----
        w.setLocked(true);
        pump(200);
        CHECK(!panel->importButton()->isEnabled() && !panel->ndiButton()->isEnabled() && !panel->cameraButton()->isEnabled(), "khoa: nhap media / NDI / camera bi mo");
        CHECK(!tool(yi, "scRename")->isEnabled() && !tool(yi, "scDelete")->isEnabled() && !tool(yi, "scLoop")->isEnabled(), "khoa: doi ten / xoa / lap bi mo");
        CHECK(!w.deleteScene(sc) && !w.renameScene(sc, "x") && sc->name == "Backdrop vàng" && int(p.scenes.size()) == n0 + 1, "khoa: lenh xoa / doi ten bang ma cung bi tu choi");
        click(child(yi, "scThumb"));
        CHECK(waitFor([&] { return ch->preview == yi; }), "khoa: chon Preview van chay");
        if (video) {
            CHECK(waitFor([&] { return tool(2, "scPlay")->isEnabled(); }) && tool(2, "scMute")->isEnabled(), "khoa: phat va tat tieng Scene van dung duoc");
        }
        w.setLocked(false);

        if (qEnvironmentVariableIsSet("LC_SHOT_DIR")) {
            pump(900);
            w.grab().save(qEnvironmentVariable("LC_SHOT_DIR") + "/scenes.png");
        }
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

    QString clip = dir.filePath("clip.mp4");
    if (!makeClip(dir.path(), clip)) {
        std::cout << "skip: khong co ffmpeg de tao video thu - bo cac phan can video\n";
        clip.clear();
    }
    if (!clip.isEmpty()) testTransport(clip);
    testAudioPause();
    testSceneAudio(dir.path(), clip);
    testWindow(dir.path(), clip);

    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
