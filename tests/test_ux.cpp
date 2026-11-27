// Dot 4 + 5: tieng nhac dem nguoc (luat + bo tron), keo doi thu tu, dai "Dang hien", keo overlay tren khung xem truoc, keo hinh tren
// Preview + Fit/Fill/Stretch, tooltip kem phim tat / ly do bi mo. Can man hinh va GL. LC_SHOT_DIR=<thu muc> de luu anh chup.
#include "DragList.h"
#include "InspectorPanel.h"
#include "MainWindow.h"
#include "NowShowing.h"
#include "OverlayPanel.h"
#include "PlaylistPanel.h"
#include "PreviewMonitor.h"
#include "Theme.h"
#include "Ui.h"
#include "audio/AudioPlan.h"
#include "core/AppSettings.h"
#include "core/CountdownAlarm.h"
#include "drag.h"
#include "media/StaticSources.h"
#include <QApplication>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QCheckBox>
#include <QSplitter>
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
    while (t.elapsed() < ms) { if (cond()) return true; pump(20); }
    return cond();
}
static bool near(double a, double b, double tol = 0.02) { return std::abs(a - b) <= tol; }

// ---------------- luat keu ----------------
static std::string runClock(CountdownAlarm& alarm, OverlayStack& s, double from, double to, double step = 1.0 / 60) {
    std::string out;
    for (double now = from; now <= to + 1e-9; now += step) {
        std::vector<AlarmEvent> events;
        alarm.update(0, s.frames(now), events);
        for (const AlarmEvent& e : events)
            out += e.stop ? "x" : e.kind == ToneKind::Tick ? "t" : e.kind == ToneKind::Fast ? "f" : "B";
    }
    return out;
}

static void testAlarm() {
    OverlayStack s;
    OverlayDef d = defaultOverlay(OverlayType::Countdown);
    d.id = 1;
    d.seconds = 12;
    CHECK(!d.alert && d.alertSeconds == 10 && d.fastSeconds == 5 && d.alertVolume == 80, "mac dinh: tieng nhac TAT, tu 10 s, gap tu 5 s, 80%");
    d.alert = true;
    s.setDefs({d});
    s.setOn(1, true, 0);
    CountdownAlarm alarm;
    CHECK(runClock(alarm, s, 0, 1).empty(), "chua chay dong ho: khong keu");
    s.countdownStart(1, 10);
    const std::string all = runClock(alarm, s, 10, 24);
    // 10,9,8,7,6 = tit; 5, 4.5 ... 0.5 = 10 tieng gap; 0 = chuong
    CHECK(all == "tttttffffffffffB", "moi giay mot tit tu 10 s, 5 s cuoi 2 tieng/giay, mot chuong o 00:00 (" + all + ")");
    OverlayFrame f;
    s.frame(1, 25, f);
    CHECK(f.finished && f.alerting && overlayShownDef(f).color == kAlertColor, "het gio: so van do cho den khi dat lai");
    s.countdownReset(1);
    CHECK(runClock(alarm, s, 25, 26) == "x", "dat lai: im ngay (ke ca chuong dang ngan)");
    s.frame(1, 26, f);
    CHECK(!f.alerting && overlayShownDef(f).color == d.color, "dat lai: so tro lai mau cu");

    s.countdownStart(1, 100);
    std::string part = runClock(alarm, s, 100, 104.5);          // con 7.5 s: da keu 10, 9, 8
    CHECK(part == "ttt", "chay lai: keu tu dau (" + part + ")");
    s.countdownPause(1, 104.5);
    CHECK(runClock(alarm, s, 104.5, 107) == "x", "tam dung: im ngay, khong keu them");
    s.countdownStart(1, 200);                                    // con 7.5 s: khong keu lai moc da qua, keu tiep o 7
    std::string more = runClock(alarm, s, 200, 200.4);
    CHECK(more.empty(), "chay tiep giua giay: khong keu bu (" + more + ")");
    CHECK(runClock(alarm, s, 200.4, 200.6) == "t", "tieng tiep theo dung moc 7 s");
    s.setOn(1, false, 200.6);
    CHECK(runClock(alarm, s, 200.6, 203) == "x", "an khoi Program: im ngay du dong ho van chay");
    s.setOn(1, true, 203);
    CHECK(!runClock(alarm, s, 203, 204.5).empty(), "hien lai khi dang chay: keu tiep");
    std::vector<AlarmEvent> ev;
    s.setDefs({});
    alarm.update(0, s.frames(205), ev);
    CHECK(ev.size() == 1 && ev[0].stop, "xoa dong ho: im");

    // mot dong ho da het gio ma moi hien len: khong chuong
    OverlayStack s2;
    d.seconds = 2;
    s2.setDefs({d});
    s2.countdownStart(1, 0);
    OverlayFrame done;
    s2.frame(1, 5, done);
    s2.setOn(1, true, 5);
    CountdownAlarm a2;
    CHECK(runClock(a2, s2, 5, 6).empty(), "hien mot dong ho da het gio: khong keu");

    // tat tieng nhac cua rieng dong ho
    d.alert = false;
    OverlayStack s3;
    s3.setDefs({d});
    s3.setOn(1, true, 0);
    s3.countdownStart(1, 0);
    CountdownAlarm a3;
    CHECK(runClock(a3, s3, 0, 3).empty(), "tat tieng nhac: khong keu");

    // luu / mo
    d.alert = true; d.alertSeconds = 20; d.fastSeconds = 7; d.alertVolume = 35; d.seconds = 90;
    OverlayStack s4, s5;
    s4.setDefs({d});
    overlaysFromJson(s5, overlaysToJson(s4, QDir()), QDir());
    CHECK(s5.defs().size() == 1 && s5.defs()[0] == s4.defs()[0], "luu/mo giu cau hinh tieng nhac");
    QJsonObject bad = overlaysToJson(s4, QDir())[0].toObject();
    bad["alertSeconds"] = 3; bad["fastSeconds"] = 99; bad["alertVolume"] = 500;
    overlaysFromJson(s5, QJsonArray{bad}, QDir());
    const OverlayDef b = s5.defs()[0];
    CHECK(b.alertSeconds == 3 && b.fastSeconds == 3 && b.alertVolume == 100, "gia tri la trong file bi ep ve khoang hop le");
}

// ---------------- bo tron ----------------
static float peakOf(AudioMixer& m, double seconds) {
    std::vector<float> out(size_t(kMixBlock) * kAudioChannels);
    float peak = 0;
    for (int i = 0; i < int(seconds * 100); ++i) {
        m.renderBlock(out.data(), kMixBlock);
        for (float v : out) peak = std::max(peak, std::abs(v));
    }
    return peak;
}

static void testMixer() {
    AudioMixer m;
    auto plan = std::make_shared<AudioPlan>();
    plan->channels = 2;
    plan->channelGain = {1.0f, 0.0f};                    // kenh 2 tat tieng
    m.setPlan(plan);
    CHECK(peakOf(m, 0.1) == 0.0f, "khong co tieng nhac: im");
    m.playTone(0, 1, ToneKind::Tick, 0.8f);
    const float full = peakOf(m, 0.3);
    CHECK(full > 0.3f && full <= 0.5f, "tit 80% nghe duoc, khong vuot muc (" + std::to_string(full) + ")");
    CHECK(peakOf(m, 0.2) == 0.0f, "het tieng thi im");
    m.playTone(0, 1, ToneKind::Bell, 0.8f);
    CHECK(peakOf(m, 0.1) > 0.2f, "chuong keu");
    CHECK(m.channelMeter(0).peak[0] > 0.1f, "tieng di qua bus cua kenh (meter nhay)");
    m.stopTones(0, 1);
    peakOf(m, 0.02);                                      // 5 ms tat dan
    CHECK(peakOf(m, 0.2) == 0.0f, "stopTones: im ngay");
    m.playTone(0, 1, ToneKind::Bell, 0.8f, 0.5);
    m.stopTones(0, 1);
    CHECK(peakOf(m, 1.0) == 0.0f, "stopTones bo ca tieng dang hen");
    m.playTone(1, 1, ToneKind::Bell, 1.0f);
    CHECK(peakOf(m, 0.2) == 0.0f, "kenh dang tat tieng: tieng nhac cung tat");
    m.playTone(0, 1, ToneKind::Tick, 0.2f);
    const float quiet = peakOf(m, 0.3);
    CHECK(quiet > 0.05f && quiet < full * 0.4f, "am luong rieng cua dong ho co tac dung");

    Project p;
    auto ch = std::make_shared<Channel>();
    ch->volume = 0.5f;
    p.channels = {ch, std::make_shared<Channel>()};
    p.channels[1]->mute = true;
    auto built = buildAudioPlan(p, 0);
    CHECK(built->channelGain.size() == 2 && near(built->channelGain[0], 0.5, 1e-4) && built->channelGain[1] == 0.0f,
          "ke hoach tron mang am luong / mute cua tung kenh cho tieng nhac");
    auto other = std::make_shared<AudioPlan>(*built);
    other->channelGain[0] = 0.9f;
    CHECK(!built->sameAs(*other), "doi am luong kenh: ke hoach moi duoc day cho mixer");
}

static void testMovedRows() {
    const std::vector<int> v{0, 1, 2, 3, 4};
    CHECK((movedRows(v, {3}, 0) == std::vector<int>{3, 0, 1, 2, 4}), "keo mot hang len dau");
    CHECK((movedRows(v, {0}, 5) == std::vector<int>{1, 2, 3, 4, 0}), "keo xuong cuoi");
    CHECK((movedRows(v, {0, 2}, 4) == std::vector<int>{1, 3, 0, 2, 4}), "keo nhieu hang giu thu tu");
    CHECK((movedRows(v, {1}, 2) == v), "tha ngay duoi chinh no: khong doi");
}

// ---------------- giao dien ----------------
static void testUi(const QString& tmp) {
    Project p;
    auto scene = [&](const char* name, QColor c) {
        SourceDef d; d.type = "color"; d.color = c; d.label = name;
        auto src = p.addSource(d);
        auto s = std::make_shared<Scene>();
        s->name = name; s->size = {320, 180};
        s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        p.scenes.push_back(s);
    };
    scene("do", Qt::darkRed);
    scene("xanh", Qt::darkBlue);
    auto ch = std::make_shared<Channel>();
    ch->name = "K"; ch->size = {320, 180}; ch->scenes = p.scenes; ch->program = 0; ch->preview = 1;
    p.channels = {ch};
    OutputConfig o; o.name = "O1"; o.size = {160, 90};
    p.outputs = {o};
    p.filePath = tmp + "/ux.json";

    RenderEngine engine(p);
    engine.startEngine();
    {
        MainWindow w(p, engine, {});
        w.setAskOnClose(false);
        w.resize(1536, 780);
        w.show();
        pump(400);
        w.applyLayout("big");
        pump(800);
        QUndoStack* undo = w.undoStack();
        OverlayPanel* ov = w.overlayPanel();
        auto& stack = p.channels[0]->overlays;
        auto* pv = dynamic_cast<PreviewMonitor*>(w.findChild<QWidget*>("previewMonitor"));
        CHECK(pv && waitFor([&] { return pv->hasImage(); }), "man Preview co hinh");
        if (!pv) return;

        // ---- keo hinh tren Preview ----
        auto layer = [&](int s) -> const Layer& { return p.scenes[size_t(s)]->layers[0]; };
        const QRect view = pv->imageRect();
        const int steps0 = undo->index();
        testdrag::press(pv, view.center());
        testdrag::release(pv, view.center());
        CHECK(undo->index() == steps0 && pv->active(), "bam (khong keo): chi chon hinh, khong sua gi");
        testdrag::drag(pv, view.center(), view.center() + QPoint(view.width() / 4, view.height() / 5));
        CHECK(waitFor([&] { return near(layer(1).rect.x(), 0.25) && near(layer(1).rect.y(), 0.2); }), "keo trong khung: doi vi tri hinh cua Scene o Preview");
        CHECK(layer(0).rect == QRectF(0, 0, 1, 1) && ch->program == 0 && ch->preview == 1, "Program khong doi");
        CHECK(undo->index() == steps0 + 1, "mot lan keo = mot buoc Hoan tac");
        undo->undo();
        CHECK(waitFor([&] { return layer(1).rect == QRectF(0, 0, 1, 1); }), "Hoan tac tra hinh ve cho cu");
        pump(200);
        testdrag::drag(pv, pv->boxRect({0, 0, 1, 1}).bottomRight(), pv->boxRect({0, 0, 1, 1}).center());
        CHECK(waitFor([&] { return near(layer(1).rect.width(), 0.5, 0.03) && near(layer(1).rect.height(), layer(1).rect.width(), 1e-3); }),
              "keo goc: doi co, giu ti le khung");
        pump(200);
        const QRect half = pv->boxRect(layer(1).rect);
        testdrag::drag(pv, QPoint(half.right(), half.center().y()), QPoint(half.right() + view.width() / 4, half.center().y()));
        CHECK(waitFor([&] { return near(layer(1).rect.width(), 0.75, 0.04) && near(layer(1).rect.height(), 0.5, 0.03); }), "keo canh: chi doi mot chieu");

        auto fit = [&](const char* name) { return w.findChild<QPushButton*>(name); };
        CHECK(fit("pvFit") && fit("pvFill") && fit("pvStretch") && fit("pvFill")->isEnabled(), "co nut Fit / Fill / Stretch");
        const int steps1 = undo->index();
        fit("pvFill")->click();
        CHECK(waitFor([&] { return layer(1).fit == LayerFit::Fill && layer(1).rect == QRectF(0, 0, 1, 1); }) && undo->index() == steps1 + 1,
              "Fill: hinh phu kin khung man, mot buoc Hoan tac");
        fit("pvFit")->click();
        CHECK(waitFor([&] { return layer(1).fit == LayerFit::Fit; }), "Fit");
        fit("pvStretch")->click();
        CHECK(waitFor([&] { return layer(1).fit == LayerFit::Stretch; }), "Stretch");

        w.setLocked(true);
        const QRectF kept = layer(1).rect;
        const int stepsL = undo->index();
        testdrag::drag(pv, view.center(), view.center() + QPoint(80, 40));
        pump(300);
        CHECK(layer(1).rect == kept && undo->index() == stepsL && !fit("pvFill")->isEnabled(), "khoa: khong keo duoc hinh, nut khung bi mo");
        CHECK(ui::hintText(fit("pvFill")).contains("khoá"), "nut mo noi ly do khoa: " + ui::hintText(fit("pvFill")).toStdString());
        w.setLocked(false);
        CHECK(fit("pvFill")->isEnabled(), "mo khoa: nut bat lai");

        // ---- tooltip: phim tat + ly do ----
        auto* play = w.playlistPanel()->findChild<QPushButton*>("plPlay");
        CHECK(play && !play->isEnabled() && ui::hintText(play).contains("chưa có mục"), "playlist rong: nut Chay noi ly do (" + ui::hintText(play).toStdString() + ")");
        CHECK(play->text().isEmpty() && !play->icon().isNull(), "nut phu chi co bieu tuong");
        for (const char* n : {"cut", "take", "blackAll", "ftb"}) {
            auto* b = w.findChild<QPushButton*>(n);
            CHECK(b && !b->text().isEmpty(), std::string(n) + " giu chu");
        }
        KeyMap keys = w.keyMap();
        keys.insert("output.1.black", QKeySequence(Qt::Key_F5));
        keys.insert("cut", QKeySequence(Qt::Key_F6));
        w.applyKeys(keys);
        auto* black = w.findChild<QToolButton*>("outBlack0");
        CHECK(black && black->toolTip().contains("F5") && w.findChild<QPushButton*>("cut")->toolTip().contains("Enter / F6"),
              "tooltip hien phim nguoi dung dat (" + black->toolTip().toStdString() + ")");

        // ---- overlay: keo tren khung xem truoc + dat nhanh ----
        for (QDockWidget* d : w.findChildren<QDockWidget*>("dockOverlay")) d->raise();
        const int title = ov->addOverlay(OverlayType::Title);
        ov->editCurrent([](OverlayDef& d) { d.rect = QRectF(0.25, 0.25, 0.5, 0.5); d.fontSize = 40; d.fade = 0; }, "x");
        auto def = [&](int id) { for (const auto& d : stack.defs()) if (d.id == id) return d; return OverlayDef(); };
        OverlayPreview* prev = ov->preview();
        if (auto* split = ov->findChild<QSplitter*>("ovSplit")) split->setSizes({90, 600});
        pump(500);
        const QRect box = prev->boxRect(), area = prev->viewRect();
        CHECK(!box.isEmpty() && area.width() > 60, "khung xem truoc co hop de keo");
        const int steps2 = undo->index();
        testdrag::drag(prev, box.center(), box.center() + QPoint(area.width() / 8, -area.height() / 8));
        CHECK(near(def(title).rect.x(), 0.375, 0.03) && near(def(title).rect.y(), 0.125, 0.04) && def(title).fontSize == 40 && undo->index() == steps2 + 1,
              "keo hop overlay: doi vi tri, mot buoc Hoan tac");
        testdrag::drag(prev, prev->boxRect().center(), prev->boxRect().center() + QPoint(2000, 2000));
        CHECK(near(def(title).rect.x(), 0.5, 1e-6) && near(def(title).rect.y(), 0.5, 1e-6), "keo qua xa: hop van nam trong man");
        CHECK(undo->index() == steps2 + 2, "lan keo moi = buoc Hoan tac moi");
        undo->undo();
        undo->undo();
        CHECK(near(def(title).rect.x(), 0.25, 1e-6), "Hoan tac tra vi tri");
        pump(200);
        testdrag::drag(prev, prev->boxRect().bottomRight(), prev->boxRect().center());
        CHECK(def(title).rect.width() < 0.35 && def(title).fontSize < 28, "keo goc: hop nho lai thi chu nho theo (" + std::to_string(def(title).fontSize) + " px)");
        undo->undo();
        pump(200);
        testdrag::drag(prev, QPoint(prev->boxRect().right(), prev->boxRect().center().y()), QPoint(prev->boxRect().right() + area.width() / 8, prev->boxRect().center().y()));
        CHECK(def(title).rect.width() > 0.55 && def(title).fontSize == 40, "keo canh: chi doi hop, giu co chu");
        undo->undo();

        auto place = [&](int i) { ov->findChild<QPushButton*>(QString("ovPlace%1").arg(i))->click(); };
        place(1);
        const double mx = 5.0 / 320, my = 5.0 / 180;
        CHECK(near(def(title).rect.x(), mx, 1e-6) && near(def(title).rect.y(), my, 1e-6), "goc tren trai");
        place(2);
        CHECK(near(def(title).rect.right(), 1 - mx, 1e-6) && near(def(title).rect.y(), my, 1e-6), "goc tren phai");
        place(3);
        CHECK(near(def(title).rect.x(), mx, 1e-6) && near(def(title).rect.bottom(), 1 - my, 1e-6), "goc duoi trai");
        place(4);
        CHECK(near(def(title).rect.right(), 1 - mx, 1e-6) && near(def(title).rect.bottom(), 1 - my, 1e-6), "goc duoi phai");
        place(0);
        CHECK(near(def(title).rect.center().x(), 0.5, 1e-6) && near(def(title).rect.center().y(), 0.5, 1e-6), "giua man");

        // ---- keo dong overlay bang chinh noi dung cua dong (nhu nguoi dung bam vao ten) ----
        {
            const int second = ov->addOverlay(OverlayType::Ticker);
            pump(300);
            QWidget* topRow = ov->row(second);
            QWidget* label = topRow ? topRow->findChild<QWidget*>("ovRowName") : nullptr;
            CHECK(label && stack.defs().back().id == second, "them chu chay: dong nam tren cung");
            if (label) {
                const QRect below = ov->list()->visualItemRect(ov->list()->item(1));
                const QPoint from(4, label->height() / 2);
                const QPoint to = label->mapFromGlobal(ov->list()->viewport()->mapToGlobal(QPoint(below.center().x(), below.bottom() - 2)));
                testdrag::drag(label, from, to);
                pump(100);
                CHECK(stack.defs().front().id == second, "keo tu ten cua dong: doi thu tu");
                undo->undo();
            }
            ov->selectId(second);
            ov->removeCurrent();
            ov->selectId(title);
            pump(100);
        }

        // ---- dai "Dang hien" ----
        auto* strip = w.findChild<QWidget*>("nowShowing");
        auto* hideAll = w.findChild<QPushButton*>("nsHideAll");
        CHECK(strip && hideAll && !hideAll->isEnabled() && ui::hintText(hideAll).contains("Không có overlay"), "chua hien gi: Tat tat ca bi mo, co ly do");
        const int cd = ov->addOverlay(OverlayType::Countdown);
        ov->setOn(title, true);
        ov->setOn(cd, true);
        pump(300);
        auto chip = [&](int id) { return strip->findChild<QWidget*>(QString("nsChip%1").arg(id)); };
        CHECK(chip(title) && chip(cd) && hideAll->isEnabled(), "moi overlay dang hien co mot the");
        const size_t count = stack.defs().size();
        strip->findChild<QPushButton*>(QString("nsClose%1").arg(title))->click();
        pump(200);
        CHECK(!stack.isOn(title) && stack.isOn(cd) && stack.defs().size() == count && !chip(title), "✕ chi an, khong xoa");
        w.setLocked(true);
        ov->setOn(title, true);
        pump(200);
        hideAll->click();
        pump(200);
        CHECK(!stack.isOn(title) && !stack.isOn(cd) && stack.defs().size() == count, "Tat tat ca: an het, ke ca khi khoa, khong xoa");
        w.setLocked(false);

        // ---- tieng nhac qua engine that ----
        CHECK(!engine.audio().running(), "test tu lay tieng tu bo tron (khong co thread mixer)");
        ov->selectId(cd);
        ov->findChild<QCheckBox*>("ovAlertOn")->setChecked(true);
        ov->editCurrent([](OverlayDef& d) { d.seconds = 3; d.text = "Còn"; d.fontSize = 60; d.rect = QRectF(0, 0, 1, 1); d.fade = 0; }, "x");
        CHECK(def(cd).alert && def(cd).seconds == 3, "bat tieng nhac trong dock");
        auto listen = [&](int ms) {
            float peak = 0;
            QElapsedTimer t; t.start();
            while (t.elapsed() < ms) {
                peak = std::max(peak, peakOf(engine.audio(), 0.01));
                pump(8);
            }
            return peak;
        };
        listen(300);
        ov->setOn(cd, true);
        CHECK(listen(400) == 0.0f, "hien ma chua chay: khong keu");
        ov->toggleClock(cd);
        CHECK(listen(900) > 0.2f, "dang chay + dang hien: keu");
        QWidget* row = ov->row(cd);
        CHECK(row && row->findChild<QLabel*>("ovClock")->property("alerting").toBool(), "so trong danh sach doi do khi nhac");
        auto redOnProgram = [&] {
            const QImage img = engine.monitor(0).program;
            int n = 0;
            for (int y = 0; y < img.height(); y += 2)
                for (int x = 0; x < img.width(); x += 2) {
                    const QColor c = img.pixelColor(x, y);
                    n += c.red() > 220 && c.green() > 30 && c.green() < 100 && c.blue() < 90;
                }
            return n;
        };
        CHECK(waitFor([&] { return redOnProgram() > 40; }, 1500), "chu so tren Program doi do");
        ov->toggleClock(cd);                      // tam dung
        listen(150);
        CHECK(listen(900) == 0.0f, "tam dung: im ngay");
        CHECK(waitFor([&] { return redOnProgram() == 0; }, 1500), "tam dung: so het do");
        ov->toggleClock(cd);
        CHECK(listen(3500) > 0.2f, "chay tiep: keu den het gio");
        ov->setOn(cd, false);
        listen(150);
        CHECK(listen(600) == 0.0f, "an: im ngay (ke ca chuong)");
        ch->mute = true;
        ov->resetClock(cd);
        ov->setOn(cd, true);
        ov->toggleClock(cd);
        listen(100);
        CHECK(listen(1200) == 0.0f, "tat tieng kenh: tieng nhac cung tat");
        ch->mute = false;
        ov->resetClock(cd);
        ov->testAlert();
        CHECK(listen(900) > 0.2f, "Nghe thu: nghe duoc du chua hien");
        listen(2600);

        // ---- layer: keo doi thu tu ----
        InspectorPanel* insp = w.inspector();
        for (QDockWidget* d : w.findChildren<QDockWidget*>("dockInspector")) d->raise();
        insp->selectLayer(0, 0);
        insp->findChild<QPushButton*>("layerAdd")->click();
        pump(400);
        auto* layers = static_cast<DragList*>(insp->layerList());
        CHECK(layers->count() == 2 && p.scenes[0]->layers.size() == 2, "them layer");

        const auto top = p.scenes[0]->layers[1].source, bottom = p.scenes[0]->layers[0].source;
        const int steps3 = undo->index();
        testdrag::dragRow(layers, 0, 2);
        CHECK(waitFor([&] { return p.scenes[0]->layers[0].source == top && p.scenes[0]->layers[1].source == bottom; }) && undo->index() == steps3 + 1,
              "keo layer tren cung xuong duoi");
        undo->undo();
        CHECK(waitFor([&] { return p.scenes[0]->layers[1].source == top; }), "Hoan tac thu tu layer");

        if (qEnvironmentVariableIsSet("LC_SHOT_DIR")) {
            const QString shots = qEnvironmentVariable("LC_SHOT_DIR");
            undo->undo();                                             // bo layer vua them
            for (QDockWidget* d : w.findChildren<QDockWidget*>("dockOverlay")) d->raise();
            ov->selectId(cd);
            ov->editCurrent([](OverlayDef& d) { d.seconds = 600; d.fontSize = 40; d.rect = QRectF(0.2, 0.05, 0.6, 0.3); }, "x");
            ov->selectId(title);
            ov->editCurrent([](OverlayDef& d) { d.name = "Nguyễn Thị Ánh Tuyết"; d.text = "Nguyễn Thị Ánh Tuyết"; d.fontSize = 22; d.rect = QRectF(0.1, 0.7, 0.8, 0.2); }, "x");
            ov->addOverlay(OverlayType::Ticker);
            ov->selectId(title);
            ov->setOn(title, true);
            ov->setOn(cd, true);
            ov->toggleClock(cd);
            const QRect v2 = pv->imageRect();
            testdrag::drag(pv, v2.center(), v2.center() + QPoint(v2.width() / 6, 0));
            pump(700);
            if (auto* split = ov->findChild<QSplitter*>("ovSplit")) split->setSizes({230, 400});
            w.applyLayout("live");
            pump(900);
            w.grab().save(shots + "/ux-dang-dien.png");
            w.applyLayout("big");
            pump(900);
            w.grab().save(shots + "/ux-man-lon.png");
            w.applyLayout("prepare");
            for (QDockWidget* d : w.findChildren<QDockWidget*>("dockOverlay")) d->raise();
            pump(900);
            w.grab().save(shots + "/ux-chuan-bi-overlay.png");
            ov->selectId(cd);
            pump(300);
            if (auto* sa = ov->findChild<QScrollArea*>("ovEditor")) sa->verticalScrollBar()->setValue(sa->verticalScrollBar()->maximum());
            pump(300);
            w.grab().save(shots + "/ux-tieng-nhac.png");
        }
        w.close();
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
    if (!appSettings().fileName().startsWith(dir.path())) {
        std::cerr << "FAIL: cai dat khong duoc co lap\n";
        return 1;
    }
    testAlarm();
    testMixer();
    testMovedRows();
    testUi(dir.path());
    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
