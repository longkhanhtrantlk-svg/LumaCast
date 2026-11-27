// Test playlist / slide anh tu chuyen (CHN-10).
// Phan 1: bo chay playlist voi dong ho gia (khong can GL). Phan 2: luu/mo project. Phan 3: cua so chinh + engine that
// (nut bam, hoan tac, khoa van hanh, keo tha, dieu khien ngoai). Phan 4: video that (can ffmpeg trong PATH).
#include "MainWindow.h"
#include "PlaylistPanel.h"
#include "core/AppSettings.h"
#include "media/FFmpegSource.h"
#include "media/StaticSources.h"
#include "drag.h"
#include <QApplication>
#include <QDockWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QUndoStack>
#include <QUrl>
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
static bool near(double a, double b, double eps = 0.06) { return std::abs(a - b) <= eps; }

namespace {
// Nguon gia: dong vai anh (dur = 0) hoac video (dur > 0), dieu khien duoc loi va "co khung hinh".
class FakeSource : public Source {
public:
    FakeSource(QString n, double dur = 0) : name_(std::move(n)), duration(dur) {}
    QString name() const override { return name_; }
    TexRef acquire(GL&) override { return {}; }
    double durationSeconds() const override { return duration; }
    QString error() const override { return err; }
    bool ready() const override { return isReady; }
    void restart() override { ++restarts; if (slow) isReady = false; }
    QString name_;
    double duration;
    QString err;
    bool isReady = true, slow = false;
    int restarts = 0;
};

// Scene 0 = dang o Program, 1 = dang o Preview, 2.. = cac muc playlist.
struct Rig {
    std::shared_ptr<Channel> ch = std::make_shared<Channel>();
    std::vector<std::shared_ptr<FakeSource>> src;
    double t = 0;

    explicit Rig(const std::vector<double>& itemClips) {
        add("pgm", 0);
        add("pvw", 0);
        for (size_t i = 0; i < itemClips.size(); ++i) {
            add(QString("muc%1").arg(i + 1), itemClips[i]);
            PlaylistItem it;
            it.id = newPlaylistItemId();
            it.scene = ch->scenes.back();
            ch->playlist.data.items.push_back(it);
        }
        ch->name = "K";
        ch->program = 0;
        ch->preview = 1;
        ch->playlist.data.imageSeconds = 5;
        ch->playlist.data.transitionSeconds = 1;
    }
    void add(const QString& name, double dur) {
        auto s = std::make_shared<Scene>();
        s->name = name;
        auto source = std::make_shared<FakeSource>(name, dur);
        s->layers.push_back({source, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        src.push_back(source);
        ch->scenes.push_back(s);
    }
    Playlist& pl() { return ch->playlist; }
    // Chay dong ho toi `to` (buoc 20 ms) nhu render thread.
    void run(double to) {
        while (t < to - 1e-9) {
            t = std::min(to, t + 0.02);
            ch->advance(t);
        }
    }
    void play(int itemIndex = 0) { ch->playlist.play(*ch, t, itemIndex ? ch->playlist.data.items[size_t(itemIndex - 1)].id : 0); ch->advance(t); }
    // Muc (1-based) dang la muc hien tai; 0 = khong co.
    int item() {
        for (size_t i = 0; i < pl().data.items.size(); ++i)
            if (pl().data.items[i].id == pl().currentId()) return int(i + 1);
        return 0;
    }
    // Chi so Scene dang o Program (item k = chi so k + 1).
    int pgm() const { return ch->program; }
    int pvw() const { return ch->preview; }
    bool noticed(const QString& part) {
        bool hit = false;
        for (const QString& n : pl().takeNotices()) hit = hit || n.contains(part);
        return hit;
    }
};

using State = Playlist::State;

void coreTests() {
    // ---------------- chay co ban: anh theo thoi luong, khong dung Preview ----------------
    {
        Rig r({0, 0, 0});
        r.run(3);
        CHECK(r.pl().state() == State::Stopped && r.pgm() == 0 && r.pvw() == 1, "chua bam Chay: co muc trong playlist nhung Program/Preview khong doi");
        r.play();
        CHECK(r.pl().state() == State::Playing && r.ch->inTransition() && r.ch->isIncoming(r.ch->scenes[2].get()), "Chay: bat dau chuyen muc 1 vao bang hieu ung");
        CHECK(r.item() == 1 && near(r.pl().remaining(), 6.0), "muc 1 la muc hien tai; con lai = chuyen 1 s + anh 5 s");
        std::vector<float> w;
        std::shared_ptr<Source> stinger;
        r.run(4.5);
        r.ch->audioWeights(4.5, w, stinger);
        CHECK(!r.ch->inTransition() && r.pgm() == 2 && r.pvw() == 1 && near(w[2], 1.0), "chuyen xong: muc 1 len Program, Preview cua nguoi van hanh giu nguyen");
        CHECK(near(r.pl().remaining(), 4.5), "thoi gian con lai giam theo dong ho");
        r.run(8.9);
        CHECK(r.pgm() == 2 && r.item() == 1, "chua het chuyen 1 s + anh 5 s: van la muc 1");
        r.run(9.2);
        r.ch->audioWeights(r.t, w, stinger);
        CHECK(r.item() == 2 && r.ch->inTransition() && w[2] > 0.5f && w[3] > 0.0f && w[1] == 0.0f, "het gio: chuyen sang muc 2; am thanh crossfade giua muc cu va moi, khong tinh Preview");
        r.run(10.5);
        CHECK(r.pgm() == 3 && r.pvw() == 1, "muc 2 len Program");
        r.run(22.5);
        CHECK(r.pgm() == 2 && r.item() == 1, "lap lai: sau muc 3 quay ve muc 1");
    }
    // ---------------- thoi luong rieng cua muc; dong ho that (khong dem khung) ----------------
    {
        Rig r({0, 0});
        r.pl().data.transition = TransitionType::Cut;
        r.pl().data.items[0].seconds = 2;
        r.play();
        CHECK(r.pgm() == 2 && !r.ch->inTransition() && near(r.pl().remaining(), 2.0), "CUT: muc 1 len ngay, thoi luong rieng 2 s");
        r.ch->advance(1.9);                     // mot khung duy nhat sau 1,9 s
        const bool early = r.pgm() == 2;
        r.ch->advance(2.01);
        CHECK(early && r.pgm() == 3 && r.item() == 2, "chuyen dung theo thoi gian troi qua, du chi co 2 khung hinh");
        r.pl().data.items[1].seconds = 30;
        r.ch->advance(12.0);
        CHECK(r.item() == 2 && near(r.pl().remaining(), 30 - (12.0 - 2.01)), "doi thoi luong muc dang phat co hieu luc ngay");
    }
    // ---------------- video: chuyen khi phat het ----------------
    {
        Rig r({10, 0});
        r.play();
        CHECK(r.src[2]->restarts == 1 && r.item() == 1, "muc video: tua ve dau khi len song");
        CHECK(near(r.pl().remaining(), 9.0), "video: con lai tinh toi luc bat dau chuyen (do dai - thoi gian chuyen)");
        r.run(8.9);
        CHECK(r.item() == 1, "video chua het: giu nguyen");
        r.run(9.2);
        CHECK(r.item() == 2, "video gan het: hieu ung bat dau de ket thuc dung luc video het");
    }
    {
        Rig r({4, 0});
        r.pl().data.transition = TransitionType::Cut;
        r.play();
        r.run(3.7);
        const bool still = r.item() == 1;
        r.run(3.95);
        CHECK(still && r.item() == 2, "video + CUT: cat ngay truoc cuoi video (khong lo khung dau vong lap)");
    }
    {
        Rig r({20, 0});
        r.pl().data.transition = TransitionType::Cut;
        r.pl().data.items[0].seconds = 3;
        r.play();
        r.run(3.1);
        CHECK(r.item() == 2, "video co dat thoi luong rieng: chuyen theo thoi luong do");
    }
    // ---------------- video can thoi gian ra khung dau: giu hinh cu, khong len man den ----------------
    {
        Rig r({10});
        r.src[2]->slow = true;
        r.play();
        r.run(1);
        CHECK(r.pgm() == 0 && !r.ch->inTransition(), "video chua co khung hinh: Program giu nguyen");
        r.src[2]->isReady = true;
        r.run(1.1);
        CHECK(r.ch->isIncoming(r.ch->scenes[2].get()) && r.item() == 1, "co khung hinh dau thi moi chuyen vao");
    }
    // ---------------- muc loi: bo qua va bao ----------------
    {
        Rig r({0, 10, 0});
        r.pl().data.transition = TransitionType::Cut;
        r.src[3]->err = "Khong doc duoc video";
        r.play();
        r.run(5.1);
        CHECK(r.item() == 3 && r.pgm() == 4 && r.noticed("bỏ qua mục 2"), "muc 2 bao loi: bo qua sang muc 3 va co thong bao");
    }
    {
        Rig r({0, 10, 0});
        r.pl().data.transition = TransitionType::Cut;
        r.src[3]->slow = true;                       // tua xong khong bao gio ra khung
        r.play();
        r.run(5.1);
        const bool held = r.pgm() == 2;
        r.run(8.3);
        CHECK(held && r.pgm() == 4 && r.noticed("không ra hình"), "video khong ra hinh qua 3 s: giu muc cu roi bo qua, co thong bao");
    }
    {
        Rig r({10, 0});
        r.pl().data.transition = TransitionType::Cut;
        r.play();
        r.run(1);
        r.src[2]->err = "mat file";
        r.run(1.1);
        CHECK(r.item() == 2 && r.noticed("mat file"), "video hong giua chung: chuyen sang muc sau ngay, co thong bao");
    }
    {
        Rig r({10, 10});
        r.src[2]->err = "hong";
        r.src[3]->err = "hong";
        r.play();
        r.run(5);
        CHECK(r.pl().state() == State::Stopped && r.pgm() == 0 && r.noticed("không còn mục nào"), "tat ca muc loi: dung playlist, Program giu nguyen, khong lap vo tan");
    }
    {
        Rig r({0, 0});
        r.pl().data.transition = TransitionType::Cut;
        r.play();
        r.ch->scenes.erase(r.ch->scenes.begin() + 3);        // Scene cua muc 2 bi go khoi kenh
        r.run(5.1);
        CHECK(r.item() == 1 && r.noticed("không còn trong kênh") && r.pl().state() == State::Playing, "Scene bi go: bo qua muc do, playlist chay tiep");
    }
    // ---------------- khong lap: het danh sach thi dung, giu hinh cuoi ----------------
    {
        Rig r({0, 0});
        r.pl().data.loop = false;
        r.play();
        r.run(20);
        CHECK(r.pl().state() == State::Stopped && r.pgm() == 3 && r.pl().remaining() < 0, "khong lap: het muc cuoi thi dung, Program giu muc cuoi");
        r.play();
        r.run(r.t + 1.5);
        CHECK(r.pl().state() == State::Playing && r.pgm() == 2, "bam Chay lai: phat tu muc dau");
    }
    {
        Rig r({10});
        r.pl().data.loop = false;
        r.play();
        r.run(9.5);
        const bool playing = r.pl().state() == State::Playing;
        r.run(10.2);
        CHECK(playing && r.pl().state() == State::Stopped, "muc video cuoi (khong lap): phat tron do dai roi moi dung");
    }
    // ---------------- mot muc lap lai ----------------
    {
        Rig r({4});
        r.pl().data.transition = TransitionType::Cut;
        r.play();
        r.run(4.2);
        CHECK(r.item() == 1 && r.pgm() == 2 && r.src[2]->restarts == 2 && !r.ch->inTransition(), "mot video lap: phat lai tu dau, khong chuyen canh voi chinh no");
    }
    // ---------------- tam dung / tiep tuc ----------------
    {
        Rig r({0, 0});
        r.play();
        r.run(3);
        r.pl().pause(r.t);
        r.run(60);
        CHECK(r.pl().state() == State::Paused && r.item() == 1 && r.pgm() == 2 && near(r.pl().remaining(), 3.0), "tam dung: giu muc va thoi gian con lai");
        r.play();
        r.run(62.9);
        const bool still = r.item() == 1;
        r.run(63.2);
        CHECK(still && r.item() == 2, "tiep tuc: chay not thoi gian con lai");
        r.pl().stop();
        r.run(100);
        CHECK(r.pl().state() == State::Stopped && r.pl().currentId() == 0 && r.pgm() == 3, "Dung: khong chuyen nua, Program giu nguyen");
    }
    // ---------------- muc truoc / muc sau ----------------
    {
        Rig r({0, 0, 0});
        r.pl().data.transition = TransitionType::Cut;
        r.pl().next(*r.ch, r.t);
        CHECK(r.pgm() == 0, "dang dung: Muc sau khong lam gi");
        r.play();
        r.pl().next(*r.ch, r.t);
        CHECK(r.pgm() == 3 && r.item() == 2, "Muc sau");
        r.pl().prev(*r.ch, r.t);
        CHECK(r.pgm() == 2 && r.item() == 1, "Muc truoc");
        r.pl().prev(*r.ch, r.t);
        CHECK(r.pgm() == 4 && r.item() == 3, "Muc truoc o muc dau (co lap): ve muc cuoi");
        r.pl().pause(r.t);
        r.pl().next(*r.ch, r.t);
        r.run(r.t + 30);
        CHECK(r.pl().state() == State::Paused && r.pgm() == 2 && near(r.pl().remaining(), 5.0), "dang tam dung: Muc sau doi hinh nhung van tam dung");
        r.play(3);
        CHECK(r.pl().state() == State::Playing && r.pgm() == 4, "chay tu mot muc chi dinh");
        r.pl().data.loop = false;
        r.pl().next(*r.ch, r.t);
        CHECK(r.pgm() == 4 && r.pl().state() == State::Playing, "khong lap: Muc sau o muc cuoi khong lam gi");
        r.src[3]->err = "hong";
        r.pl().prev(*r.ch, r.t);
        CHECK(r.pgm() == 2, "Muc truoc bo qua muc loi theo chieu lui");
    }
    // ---------------- nguoi van hanh can thiep: tam dung, giu hinh cua ho ----------------
    {
        Rig r({0, 0});
        r.play();
        r.run(3);
        r.ch->cut(r.t);                                       // CUT: Preview (1) len Program
        r.run(r.t + 30);
        CHECK(r.pl().state() == State::Paused && r.pgm() == 1 && r.pvw() == 2 && r.noticed("người vận hành"), "CUT khi dang chay: playlist tam dung, giu hinh cua nguoi van hanh");
        r.play();
        r.run(r.t + 1.5);
        CHECK(r.pl().state() == State::Playing && r.pgm() == 2 && r.item() == 1, "Chay lai: dua muc dang do len roi di tiep");
        r.run(r.t + 5);
        CHECK(r.item() == 2, "... va tu chuyen tiep");
    }
    {
        Rig r({0, 0});
        r.play();
        r.run(0.5);                                           // dang giua hieu ung cua playlist
        r.ch->cut(r.t);
        r.run(r.t + 10);
        CHECK(!r.ch->inTransition() && r.pgm() == 1 && r.pvw() == 0 && r.pl().state() == State::Paused, "CUT giua hieu ung cua playlist: cat sang Preview, tam dung");
    }
    {
        Rig r({0, 0});
        r.play();
        r.run(3);
        r.ch->startTransition(TransitionType::Fade, 1.0, r.t);
        r.run(r.t + 20);
        CHECK(r.pl().state() == State::Paused && r.pgm() == 1, "TAKE khi dang chay: playlist tam dung");
    }
    {
        Rig r({0, 0});
        r.play();
        r.run(3);
        r.ch->setManual(TransitionType::Fade, 0.4f);
        r.run(r.t + 20);
        CHECK(r.pl().state() == State::Paused && r.ch->manualActive(), "T-bar khi dang chay: playlist tam dung, T-bar van o tay nguoi van hanh");
    }
    {
        Rig r({0, 0});
        r.play();
        r.run(3);
        r.ch->setPreview(0);
        r.run(6.3);                                           // dang chuyen muc 1 -> 2
        r.ch->setPreview(1);
        r.ch->setPreview(0);
        r.run(8);
        CHECK(r.pl().state() == State::Playing && r.pgm() == 3 && r.pvw() == 0, "chi chon Preview: playlist chay tiep, Preview giu dung lua chon (ke ca luc playlist dang chuyen)");
    }
    // ---------------- sua danh sach khi dang chay ----------------
    {
        Rig r({0, 0, 0});
        r.pl().data.transition = TransitionType::Cut;
        r.play(2);
        r.pl().data.items.erase(r.pl().data.items.begin() + 1);
        r.run(1);
        CHECK(r.pgm() == 3 && r.pl().state() == State::Playing, "xoa muc dang phat: hinh giu nguyen cho het gio");
        r.run(5.1);
        CHECK(r.pgm() == 4, "... roi sang muc nam o vi tri cua no");
        r.pl().data.items.clear();
        r.run(6);
        CHECK(r.pl().state() == State::Stopped && r.pgm() == 4, "xoa het muc: playlist dung, Program giu nguyen");
        r.play();
        CHECK(r.pl().state() == State::Stopped, "danh sach rong: Chay khong lam gi");
    }
}

void projectTests(const QDir& dir) {
    Project p;
    SourceDef c; c.type = "color"; c.color = Qt::red; c.label = "do";
    auto src = p.addSource(c);
    for (int i = 0; i < 3; ++i) {
        auto s = std::make_shared<Scene>();
        s->name = QString("S%1").arg(i);
        s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        p.scenes.push_back(s);
    }
    auto ch = std::make_shared<Channel>();
    ch->scenes = p.scenes; ch->program = 0; ch->preview = 1;
    p.channels = {ch};
    p.outputs = {OutputConfig()};

    auto readJson = [](const QString& path) {
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        return QJsonDocument::fromJson(f.readAll()).object();
    };
    const QString plain = dir.filePath("plain.json");
    QString err;
    CHECK(saveProject(p, plain, &err), "luu project khong dung playlist");
    CHECK(!readJson(plain)["channels"].toArray()[0].toObject().contains("playlist"), "playlist mac dinh khong ghi ra file");
    Project old;
    CHECK(loadProject(old, plain, &err) && old.channels[0]->playlist.data.items.empty() && old.channels[0]->playlist.data.loop
          && old.channels[0]->playlist.state() == State::Stopped, "project cu (khong co truong playlist) mo binh thuong");

    PlaylistData& d = ch->playlist.data;
    PlaylistItem a; a.id = newPlaylistItemId(); a.scene = p.scenes[2]; a.seconds = 12.5;
    PlaylistItem b; b.id = newPlaylistItemId(); b.scene = p.scenes[0];
    PlaylistItem gone; gone.id = newPlaylistItemId(); gone.scene = std::make_shared<Scene>();   // Scene khong con trong project
    d.items = {a, gone, b, a};
    d.loop = false;
    d.imageSeconds = 3;
    d.transition = TransitionType::Plugin;
    d.pluginId = "builtin.push";
    d.transitionSeconds = 0.5;
    const QString full = dir.filePath("full.json");
    CHECK(saveProject(p, full, &err) && readJson(full)["version"].toInt() == 1, "luu playlist, version van la 1");
    Project q;
    CHECK(loadProject(q, full, &err), "mo lai project co playlist");
    const PlaylistData& e = q.channels[0]->playlist.data;
    CHECK(e.items.size() == 3 && e.items[0].scene == q.scenes[2] && e.items[0].seconds == 12.5 && e.items[1].scene == q.scenes[0]
          && e.items[1].seconds == 0 && e.items[2].scene == q.scenes[2] && e.items[0].id != e.items[2].id,
          "muc, thu tu, thoi luong giu nguyen; muc tro Scene da go khong luu; moi muc co id rieng");
    CHECK(!e.loop && e.imageSeconds == 3 && e.transition == TransitionType::Plugin && e.pluginId == "builtin.push" && e.transitionSeconds == 0.5,
          "cai dat lap/thoi luong anh/hieu ung giu nguyen");
    CHECK(q.channels[0]->playlist.state() == State::Stopped, "mo project: playlist o trang thai dung (khong tu chay)");

    QJsonObject bad{{"imageSeconds", -4}, {"transition", "khong-co"}, {"transitionSeconds", 999},
                    {"items", QJsonArray{QJsonObject{{"scene", 99}}, QJsonObject{{"scene", 1}, {"seconds", 1e9}}, QJsonValue(7)}}};
    const PlaylistData bd = playlistFromJson(bad, p.scenes);
    CHECK(bd.items.size() == 1 && bd.items[0].scene == p.scenes[1] && bd.items[0].seconds == kPlaylistMaxSeconds
          && bd.imageSeconds == kPlaylistMinSeconds && bd.transition == TransitionType::Fade && bd.transitionSeconds == 10,
          "du lieu rac: muc sai bi bo qua, gia tri bi ep ve khoang hop le");
}

void uiTests(const QDir& dir) {
    Project p;
    const std::pair<const char*, QColor> colors[] = {{"do", Qt::red}, {"xanh duong", Qt::blue}, {"xanh la", Qt::green}};
    for (const auto& [name, color] : colors) {
        SourceDef d; d.type = "color"; d.color = color; d.label = name;
        auto src = p.addSource(d);
        auto s = std::make_shared<Scene>();
        s->name = name; s->size = {64, 36};
        s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        p.scenes.push_back(s);
    }
    auto ch = std::make_shared<Channel>();
    ch->name = "K"; ch->size = {64, 36}; ch->scenes = p.scenes; ch->program = 0; ch->preview = 2;
    p.channels = {ch};
    OutputConfig o; o.name = "O1"; o.size = {160, 90};
    p.outputs = {o};
    p.filePath = dir.filePath("ui.json");

    RenderEngine engine(p);
    engine.startEngine();
    QTemporaryDir plugDir;
    MainWindow w(p, engine, {plugDir.path()});
    w.setAskOnClose(false);
    w.show();
    pump(600);

    PlaylistPanel* panel = w.playlistPanel();
    auto* list = panel->findChild<QListWidget*>("playlistList");
    auto btn = [&](const char* name) { return panel->findChild<QPushButton*>(name); };
    auto spin = [&](const char* name) { return panel->findChild<QDoubleSpinBox*>(name); };
    auto* loop = panel->findChild<QCheckBox*>("plLoop");
    auto* trans = panel->findChild<QComboBox*>("plTransition");
    auto* status = panel->findChild<QLabel*>("plStatus");
    CHECK(list && btn("plPlay") && btn("plPause") && btn("plStop") && btn("plPrev") && btn("plNext") && loop && trans && status, "khung Playlist co du dieu khien");
    CHECK(!btn("plPlay")->isEnabled() && !btn("plPause")->isEnabled() && !btn("plStop")->isEnabled(), "playlist rong: khong bam Chay duoc");
    auto press = [&](const char* name) {
        waitFor([&] { return btn(name)->isEnabled(); }, 1000);
        btn(name)->click();
    };
    auto state = [&] { return ch->playlist.state(); };
    auto items = [&] { return int(ch->playlist.data.items.size()); };

    // ---------------- sua danh sach: khong doi Program/Preview, hoan tac duoc ----------------
    panel->addScenes({p.scenes[0], p.scenes[1]});
    CHECK(items() == 2 && list->count() == 2 && list->item(1)->text().contains("xanh duong"), "them Scene vao playlist");
    spin("plImageSeconds")->setValue(1.0);
    trans->setCurrentIndex(trans->findText("Cut"));
    CHECK(ch->playlist.data.imageSeconds == 1.0 && ch->playlist.data.transition == TransitionType::Cut && !spin("plTransSeconds")->isEnabled(), "dat thoi luong anh va hieu ung");
    list->setCurrentRow(1);
    spin("plItemSeconds")->setValue(7);
    CHECK(ch->playlist.data.items[1].seconds == 7 && list->item(1)->text().contains("7 s"), "dat thoi luong rieng cho muc");
    list->setCurrentRow(1);
    for (QDockWidget* d : w.findChildren<QDockWidget*>("dockPlaylist")) d->raise();
    pump(200);
    CHECK(!panel->findChild<QPushButton*>("plUp") && !panel->findChild<QPushButton*>("plDown"), "khong con nut len/xuong");
    testdrag::dragRow(list, 1, 0);
    CHECK(ch->playlist.data.items[0].scene == p.scenes[1] && ch->program == 0, "keo muc len dau; Program khong doi");
    w.undoStack()->undo();
    CHECK(ch->playlist.data.items[0].scene == p.scenes[0], "hoan tac doi thu tu");
    w.undoStack()->undo();
    CHECK(ch->playlist.data.items[1].seconds == 0, "hoan tac thoi luong muc");
    w.undoStack()->redo();
    w.undoStack()->redo();
    CHECK(ch->playlist.data.items[0].scene == p.scenes[1] && ch->playlist.data.items[0].seconds == 7, "lam lai");
    list->clearSelection();
    list->item(0)->setSelected(true);
    btn("plRemove")->click();
    CHECK(items() == 1, "bo muc");
    w.undoStack()->undo();
    CHECK(items() == 2, "hoan tac bo muc");
    w.undoStack()->undo();               // thoi tu len/xuong
    w.undoStack()->undo();               // thoi luong 7 s
    pump(200);
    CHECK(ch->program == 0 && ch->preview == 2 && state() == State::Stopped, "sua playlist khong doi Program/Preview, khong tu chay");

    // ---------------- Enter trong o so khong kich hoat CUT ----------------
    {
        QKeyEvent ov(QEvent::ShortcutOverride, Qt::Key_Return, Qt::NoModifier);
        ov.ignore();
        QApplication::sendEvent(spin("plImageSeconds"), &ov);
        CHECK(ov.isAccepted(), "Enter trong o so cua playlist khong thanh phim tat CUT");
    }

    // ---------------- chay that tren engine ----------------
    CHECK(btn("plPlay")->isEnabled() || waitFor([&] { return btn("plPlay")->isEnabled(); }), "co muc: bam Chay duoc");
    QElapsedTimer clock;
    clock.start();
    press("plPlay");
    CHECK(waitFor([&] { return state() == State::Playing && ch->program == 0 && ch->playlist.currentId() != 0; }), "Chay: muc 1 la muc dang phat");
    CHECK(waitFor([&] { return status->text().contains("Mục 1/2") && status->text().contains("còn 00:0"); }), "hien muc dang phat va thoi gian con lai");
    CHECK(waitFor([&] { return ch->program == 1; }) && clock.elapsed() >= 850 && clock.elapsed() < 2500, "sau ~1 s dong ho that: tu chuyen sang muc 2");
    CHECK(ch->preview == 2, "Preview khong doi khi playlist chuyen");
    CHECK(waitFor([&] { return list->item(1)->text().startsWith("▶"); }), "danh dau muc dang phat trong danh sach");
    press("plPause");
    CHECK(waitFor([&] { return state() == State::Paused; }), "Tam dung");
    pump(1400);
    CHECK(ch->program == 1 && status->text().contains("Tạm dừng"), "tam dung: khong tu chuyen");
    press("plNext");
    CHECK(waitFor([&] { return ch->program == 0; }) && state() == State::Paused, "Muc sau khi tam dung");
    press("plPrev");
    CHECK(waitFor([&] { return ch->program == 1; }), "Muc truoc");
    press("plStop");
    CHECK(waitFor([&] { return state() == State::Stopped; }) && ch->program == 1 && waitFor([&] { return status->text() == "Đã dừng"; }), "Dung: Program giu nguyen");

    // ---------------- hieu ung chuyen: pha hai muc, khong dinh Preview ----------------
    trans->setCurrentIndex(trans->findText("Fade"));
    spin("plTransSeconds")->setValue(3.0);
    spin("plImageSeconds")->setValue(30.0);
    press("plPlay");                                   // Program dang la muc 2 (xanh duong) -> muc 1 (do), Preview la xanh la
    CHECK(waitFor([&] { return ch->autoTransition(); }), "playlist chuyen bang Fade");
    pump(1500);
    {
        const QImage im = engine.monitor(0).program;
        const QColor px = im.isNull() ? QColor() : im.pixelColor(im.width() / 2, im.height() / 2);
        CHECK(px.red() > 40 && px.blue() > 40 && px.green() < 30, "giua Fade: Program pha muc cu va muc moi, khong lay hinh Preview");
    }
    CHECK(waitFor([&] { return !ch->inTransition(); }, 5000) && ch->program == 0 && ch->preview == 2, "Fade xong: muc 1 len Program, Preview giu nguyen");

    // ---------------- nguoi van hanh CUT/AUTO/nhap dup: tam dung ----------------
    auto cutButton = [&]() -> QPushButton* { return w.findChild<QPushButton*>("cut"); };
    cutButton()->click();
    CHECK(waitFor([&] { return state() == State::Paused && ch->program == 2; }), "nut CUT khi playlist dang chay: tam dung, hinh cua nguoi van hanh len");
    press("plPlay");
    CHECK(waitFor([&] { return state() == State::Playing && ch->program == 0 && !ch->inTransition(); }, 6000), "Chay lai: muc dang do quay lai Program");
    auto* scenes = w.findChild<QListWidget*>("sceneList");
    emit scenes->itemDoubleClicked(scenes->item(1));
    CHECK(waitFor([&] { return state() == State::Paused && ch->program == 1; }), "nhap dup Scene: tam dung");

    // ---------------- dieu khien ngoai + khoa van hanh ----------------
    ControlApi& api = w.controlApi();
    auto pl = [&] { return api.state()["channels"].toArray()[0].toObject()["playlist"].toObject(); };
    CHECK(pl()["state"].toString() == "paused" && pl()["items"].toInt() == 2 && pl()["item"].toInt() == 1 && pl()["remaining"].toDouble() > 0, "API: trang thai playlist trong /api/state");
    w.setLocked(true);
    CHECK(!spin("plImageSeconds")->isEnabled() && !btn("plAddScene")->isEnabled() && !btn("plAddFile")->isEnabled() && !loop->isEnabled() && !trans->isEnabled(),
          "khoa van hanh: cac dieu khien SUA playlist bi vo hieu");
    {
        const int n = items();
        panel->addScenes({p.scenes[2]});
        CHECK(items() == n, "khoa: khong them muc duoc");
    }
    CHECK(waitFor([&] { return btn("plStop")->isEnabled() && btn("plNext")->isEnabled() && btn("plPlay")->isEnabled(); }, 1000), "khoa: dieu khien chay van bat");
    press("plPlay");
    CHECK(waitFor([&] { return state() == State::Playing; }), "khoa: Chay duoc");
    auto http = [&](const QString& path, const QByteArray& body = {}) {
        HttpRequest req;
        req.method = "POST";
        req.path = path.toUtf8();
        req.body = body;
        return api.handleHttp(req).status;
    };
    CHECK(http("/api/channels/1/playlist/pause") == 200 && waitFor([&] { return state() == State::Paused; }), "HTTP pause (lenh live, chay ca khi khoa)");
    CHECK(http("/api/channels/1/playlist/play", "{\"item\":2}") == 200 && waitFor([&] { return state() == State::Playing && pl()["item"].toInt() == 2; }, 6000), "HTTP play item 2");
    CHECK(http("/api/channels/1/playlist/stop") == 200 && waitFor([&] { return state() == State::Stopped; }), "HTTP stop");
    CHECK(http("/api/channels/1/playlist/next") == 409, "HTTP next khi dang dung: 409");
    CHECK(http("/api/channels/9/playlist/play") == 404 && http("/api/channels/1/playlist/play", "{\"item\":9}") == 404
          && http("/api/channels/1/playlist/play", "{\"item\":1.5}") == 400, "HTTP chi so sai: 404/400");
    CHECK(api.handleOsc({"/lumacast/playlist/1/play", {}}).ok() && waitFor([&] { return state() == State::Playing; }), "OSC play");
    CHECK(api.handleOsc({"/lumacast/playlist/1/stop", {}}).ok() && waitFor([&] { return state() == State::Stopped; }), "OSC stop");
    w.setLocked(false);

    // ---------------- them file / keo tha vao khung playlist ----------------
    {
        const QString png = dir.filePath("slide.png");
        QImage im(32, 18, QImage::Format_RGB32);
        im.fill(Qt::yellow);
        CHECK(im.save(png), "tao anh thu");
        const int sceneCount = int(p.scenes.size()), before = items();
        const int pg = ch->program, pv = ch->preview;
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(png)});
        QWidget* target = list->viewport();
        QDragEnterEvent enter(QPoint(5, 5), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &enter);
        QDropEvent drop(QPointF(5, 5), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &drop);
        pump(200);
        CHECK(int(p.scenes.size()) == sceneCount + 1 && items() == before + 1 && ch->playlist.data.items.back().scene == p.scenes.back(),
              "tha anh vao khung Playlist: nhap media (them Scene) va them vao cuoi playlist");
        CHECK(ch->program == pg && ch->preview == pv, "... khong doi Program/Preview");
        w.undoStack()->undo();
        CHECK(items() == before && int(p.scenes.size()) == sceneCount + 1, "hoan tac 1: bo muc khoi playlist");
        w.undoStack()->undo();
        CHECK(int(p.scenes.size()) == sceneCount, "hoan tac 2: go media");
        w.undoStack()->redo();
        w.undoStack()->redo();
        CHECK(items() == before + 1, "lam lai ca hai");
    }

    // ---------------- luu tu giao dien ----------------
    {
        w.saveProject(false);
        CHECK(waitFor([&] { return QFile::exists(p.filePath); }), "luu project");
        pump(200);
        Project q;
        QString err;
        CHECK(loadProject(q, p.filePath, &err) && int(q.channels[0]->playlist.data.items.size()) == items(), "mo lai: playlist con nguyen");
    }
    w.close();
    engine.stopEngine();
}

void videoTests(const QDir& dir) {
    const QString clip = dir.filePath("clip.mp4");
    const QString raw = dir.filePath("raw.rgb");
    {                                                    // 2 s mau do, 25 fps (khong dung lavfi: ban FFmpeg cua vcpkg khong co)
        QFile f(raw);
        f.open(QIODevice::WriteOnly);
        const QByteArray frame = QByteArray("\xff\x00\x00", 3).repeated(160 * 90);
        for (int i = 0; i < 50; ++i) f.write(frame);
    }
    if (QProcess::execute("ffmpeg", {"-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "160x90", "-r", "25", "-i", raw,
                                     "-pix_fmt", "yuv420p", clip}) != 0 || !QFile::exists(clip)) {
        std::cout << "skip: khong co ffmpeg de tao video thu\n";
        return;
    }
    const QString junk = dir.filePath("hong.mp4");
    {
        QFile f(junk);
        f.open(QIODevice::WriteOnly);
        f.write(QByteArray(4096, 'x'));
    }
    Project p;
    auto addScene = [&](const SourceDef& d) {
        auto src = p.addSource(d);
        auto s = std::make_shared<Scene>();
        s->name = src->name(); s->size = {160, 90};
        s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        p.scenes.push_back(s);
        return src;
    };
    SourceDef blue; blue.type = "color"; blue.color = Qt::blue; blue.label = "xanh";
    addScene(blue);
    SourceDef bad; bad.type = "video"; bad.path = junk;
    auto badSrc = addScene(bad);
    SourceDef good; good.type = "video"; good.path = clip;
    auto goodSrc = addScene(good);
    SourceDef green; green.type = "color"; green.color = Qt::green; green.label = "la";
    addScene(green);
    auto ch = std::make_shared<Channel>();
    ch->name = "K"; ch->size = {160, 90}; ch->scenes = p.scenes; ch->program = 0; ch->preview = 0;
    for (int i : {1, 2, 3}) {
        PlaylistItem it;
        it.id = newPlaylistItemId();
        it.scene = p.scenes[size_t(i)];
        ch->playlist.data.items.push_back(it);
    }
    ch->playlist.data.transitionSeconds = 0.3;
    ch->playlist.data.imageSeconds = 30;
    p.channels = {ch};
    OutputConfig o; o.size = {160, 90};
    p.outputs = {o};

    RenderEngine engine(p);
    engine.startEngine();
    CHECK(waitFor([&] { return !badSrc->error().isEmpty() && goodSrc->durationSeconds() > 1.5; }, 6000), "video rac bao loi, video tot biet do dai");
    engine.post([ch](double now) { ch->playlist.play(*ch, now); });
    CHECK(waitFor([&] { return ch->program == 2 || ch->isIncoming(p.scenes[2].get()); }, 5000), "muc video rac bi bo qua, video tot len song");
    QElapsedTimer clock;
    clock.start();
    bool noticed = false;
    for (const QString& n : ch->playlist.takeNotices()) noticed = noticed || n.contains("bỏ qua mục 1");
    CHECK(noticed, "co thong bao bo qua muc loi");
    pump(700);
    {
        const QImage im = engine.monitor(0).program;
        const QColor px = im.isNull() ? QColor() : im.pixelColor(im.width() / 2, im.height() / 2);
        CHECK(px.red() > 180 && px.blue() < 60, "video that hien tren Program (khong den)");
    }
    CHECK(waitFor([&] { return ch->program == 3; }, 6000), "video phat het: tu chuyen sang muc sau");
    CHECK(clock.elapsed() > 1400 && clock.elapsed() < 3500, "thoi diem chuyen khop do dai video 2 s");
    engine.stopEngine();
}
} // namespace

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    QSettings::setDefaultFormat(QSettings::IniFormat);                    // khong lam ban cau hinh that cua may
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.filePath("cfg"));
    const QDir dir(tmp.path());

    coreTests();
    projectTests(dir);
    // --core: chi phan khong mo cua so (dung khi may dang co nguoi dung)
    if (!app.arguments().contains("--core")) {
        uiTests(dir);
        videoTests(dir);
    }

    std::cout << (g_fail ? "THAT BAI: " : "TAT CA DAT") << (g_fail ? std::to_string(g_fail) : std::string()) << "\n";
    return g_fail ? 1 : 0;
}
