// Test UI-07 (phim tat tu dat) va CTL-01 (MIDI). MIDI dung BACKEND GIA: khong can thiet bi; backend winmm that chi kiem tra khong crash.
// Phim: neu cua so test dang kich hoat thi bom phim that qua QTest (di dung duong QShortcut cua Qt); neu khong (may dang duoc dung,
// Windows khong cho gianh focus) thi phat tin hieu cua QShortcut dung phim do. Cuoi bai in ra so lan di moi duong.
#include "app/InputDialog.h"
#include "app/LockButton.h"
#include "app/MainWindow.h"
#include "core/AppSettings.h"
#include "core/Features.h"
#include "media/StaticSources.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QShortcut>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>

using namespace lc;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << msg << "  (" #cond ")\n"; } else std::cout << "ok:   " << msg << "\n"; } while (0)

static void pump(int ms) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { QApplication::processEvents(); QThread::msleep(2); }
}
template <class F> static bool waitFor(F cond, int ms = 3000) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { QApplication::processEvents(); if (cond()) return true; QThread::msleep(5); }
    return cond();
}

// ---------------------------------------------------------------- thiet bi MIDI gia

// "Cam/rut" bang danh sach ten, "bam nut" bang send() tu mot luong khac (nhu driver that).
struct FakeDevices {
    std::mutex m;
    QStringList names;
    MidiBackend::Handler handler;
    int opens = 0, closes = 0;
    bool busy = false;

    void plug(const QString& n) { std::lock_guard lk(m); names << n; }
    void unplug(const QString& n) { std::lock_guard lk(m); names.removeAll(n); }
    MidiBackend::Handler current() { std::lock_guard lk(m); return handler; }
    static void sendVia(const MidiBackend::Handler& h, int status, int d1, int d2) {
        std::thread([&] { h(quint8(status), quint8(d1), quint8(d2)); }).join();
    }
    bool send(int status, int d1, int d2) {
        const auto h = current();
        if (!h) return false;
        sendVia(h, status, d1, d2);
        QApplication::processEvents();
        return true;
    }
    bool note(int n, int vel = 127) { return send(0x90, n, vel); }
};

class FakeBackend : public MidiBackend {
public:
    explicit FakeBackend(std::shared_ptr<FakeDevices> d) : d_(std::move(d)) {}
    ~FakeBackend() override { close(); }
    QStringList inputs() override { std::lock_guard lk(d_->m); return d_->names; }
    bool open(const QString& name, Handler h, QString* error) override {
        std::lock_guard lk(d_->m);
        if (d_->busy) { if (error) *error = "đang được chương trình khác dùng"; return false; }
        if (!d_->names.contains(name)) return false;
        d_->handler = std::move(h);
        ++d_->opens;
        return true;
    }
    void close() override {
        std::lock_guard lk(d_->m);
        if (d_->handler) ++d_->closes;
        d_->handler = nullptr;
    }
private:
    std::shared_ptr<FakeDevices> d_;
};

// ---------------------------------------------------------------- phim

static int g_realKeys = 0, g_signalKeys = 0;

static void press(QWidget& win, const QKeySequence& k, QWidget* target = nullptr) {
    if (QApplication::activeWindow() == &win) {
        ++g_realKeys;
        QTest::keyClick(target ? target : &win, Qt::Key(k[0].key()), k[0].keyboardModifiers());
    } else {
        ++g_signalKeys;
        for (QShortcut* s : win.findChildren<QShortcut*>(Qt::FindDirectChildrenOnly))
            if (s->key() == k && s->isEnabled()) { emit s->activated(); break; }
    }
    pump(30);
}

static QPushButton* button(QWidget* w, const QString& text) {
    for (auto* b : w->findChildren<QPushButton*>()) if (b->text() == text) return b;
    return nullptr;
}

// ---------------------------------------------------------------- phan khong can cua so

static void testActions() {
    bool roundTrip = true;
    for (const QString& id : LiveAction::allIds(3)) roundTrip = roundTrip && LiveAction::fromId(id).valid() && LiveAction::fromId(id).id() == id;
    CHECK(roundTrip && LiveAction::allIds(3).size() == 5 + 9 + 9, "ma lenh: doc/ghi khop, 3 Output -> 23 lenh");
    bool bad = false;
    for (const char* id : {"", "preview.0", "preview.10", "output.0.black", "output.1.red", "output.x.black", "unlock", "preview.01"})
        bad = bad || LiveAction::fromId(id).valid();
    CHECK(!bad, "ma lenh sai/la bi tu choi (khong co lenh 'mo khoa')");
    CHECK(!LiveAction::fromId("lock").externalAllowed() && LiveAction::fromId("cut").externalAllowed(), "khoa van hanh khong goi duoc tu ngoai (MIDI)");
}

static void testBindingAndDecoder() {
    const MidiBinding n{MidiBinding::Note, 2, 36};
    CHECK(n.toString() == "note:2:36" && MidiBinding::fromString("note:2:36") == n && n.label().contains("kênh 3"), "nut MIDI: ghi/doc chuoi, nhan 1-based");
    bool bad = false;
    for (const char* s : {"", "note:16:1", "note:0:128", "note:0", "x:0:1", "cc:a:1", "cc:0:-1"}) bad = bad || MidiBinding::fromString(s).valid();
    CHECK(!bad, "nut MIDI: chuoi sai -> khong hop le");

    MidiDecoder d;
    CHECK(d.press(0x90, 36, 100) == (MidiBinding{MidiBinding::Note, 0, 36}), "Note On = mot lan bam");
    CHECK(!d.press(0x90, 36, 0).valid() && !d.press(0x80, 36, 64).valid(), "nha nut (Note Off / luc 0): bo");
    CHECK(d.press(0xB0, 7, 127).valid() && !d.press(0xB0, 7, 127).valid() && !d.press(0xB0, 7, 0).valid() && d.press(0xB0, 7, 127).valid(), "CC: chi tinh khi vuot len, nha ra roi bam lai moi tinh tiep");
    int hits = 0;
    d.reset();
    for (int v = 0; v <= 127; ++v) hits += d.press(0xB1, 1, v).valid();
    for (int v = 127; v >= 0; --v) hits += d.press(0xB1, 1, v).valid();
    CHECK(hits == 1, "CC: keo num/fader qua lai mot vong chi kich mot lan");
    CHECK(d.press(0xC3, 5, 0) == (MidiBinding{MidiBinding::Program, 3, 5}), "Program Change (ban dap chan)");
    CHECK(!d.press(0xF8, 0, 0).valid() && !d.press(0xFE, 0, 0).valid() && !d.press(0xE0, 0, 64).valid() && !d.press(0x10, 1, 1).valid(), "thong diep he thong/pitch bend/rac: bo");
}

static void testKeyStore(const QString& dir) {
    const KeyMap def = shortcuts::defaults();
    CHECK(def.value("blackAll") == QKeySequence(Qt::Key_B) && def.value("preview.9") == QKeySequence(Qt::Key_9) &&
          def.value("lock") == QKeySequence(Qt::CTRL | Qt::Key_L) && def.size() == 11 && !def.contains("take") && !def.contains("cut") && !def.contains("ftb"),
          "mac dinh y nhu truoc: Ctrl+L, B, 1-9 (TAKE/CUT la phim co dinh)");
    bool reserved = true;
    for (const QKeySequence& k : {QKeySequence(Qt::Key_Space), QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter), QKeySequence(Qt::CTRL | Qt::Key_Space),
                                  QKeySequence(Qt::SHIFT | Qt::Key_Return), QKeySequence(Qt::Key_Escape), QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Tab),
                                  QKeySequence(Qt::CTRL | Qt::Key_S), QKeySequence(Qt::CTRL | Qt::Key_O), QKeySequence(Qt::CTRL | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y),
                                  QKeySequence(Qt::CTRL | Qt::Key_C), QKeySequence(Qt::ALT | Qt::Key_F), QKeySequence(Qt::CTRL | Qt::Key_M), QKeySequence(Qt::Key_F11)})
        reserved = reserved && !shortcuts::reservedReason(k).isEmpty();
    CHECK(reserved, "phim co dinh / phim menu / phim timeline khong gan duoc");
    CHECK(shortcuts::reservedReason(QKeySequence(Qt::Key_F5)).isEmpty() && shortcuts::reservedReason(QKeySequence(Qt::Key_PageDown)).isEmpty() &&
          shortcuts::reservedReason(QKeySequence(Qt::CTRL | Qt::Key_B)).isEmpty() && shortcuts::reservedReason(QKeySequence(Qt::Key_B)).isEmpty(), "F5, PageDown, Ctrl+B, B gan duoc");

    KeyMap m = def;
    m["ftb"] = QKeySequence(Qt::Key_B);
    const QStringList dup = shortcuts::duplicates(m);
    CHECK(dup.size() == 2 && dup.contains("ftb") && dup.contains("blackAll"), "phat hien phim trung (ca hai lenh)");

    QSettings st(dir + "/keys.ini", QSettings::IniFormat);
    CHECK(shortcuts::load(st) == def, "chua luu gi: dung mac dinh");
    m = def;
    m.remove("preview.1");
    m["output.2.black"] = QKeySequence(Qt::Key_1);
    m["take"] = QKeySequence(Qt::Key_PageDown);
    m["blackAll"] = QKeySequence(Qt::Key_F9);
    shortcuts::save(st, m);
    CHECK(shortcuts::load(st) == m, "luu roi doc lai dung (ke ca phim mac dinh da go)");
    CHECK(!st.contains("shortcuts/lock") && !st.contains("shortcuts/preview.2") && st.contains("shortcuts/preview.1"), "chi ghi phan khac mac dinh");
    shortcuts::save(st, def);
    st.beginGroup("shortcuts");
    CHECK(st.childKeys().isEmpty(), "tro ve mac dinh: xoa het muc da ghi");
    st.endGroup();

    st.setValue("shortcuts/take", "Space");                  // sua tay: phim co dinh
    st.setValue("shortcuts/ftb", "F7");
    st.setValue("shortcuts/output.1.black", "F7");           // trung
    st.setValue("shortcuts/khong.co", "F8");
    st.setValue("shortcuts/preview.3", "B");                 // lay phim mac dinh cua lenh khac
    const KeyMap l = shortcuts::load(st);
    CHECK(!l.contains("take") && l.value("ftb") == QKeySequence(Qt::Key_F7) && !l.contains("output.1.black") && !l.contains("khong.co"), "cau hinh sai/trung bi bo qua khi nap");
    CHECK(l.value("preview.3") == QKeySequence(Qt::Key_B) && !l.contains("blackAll") && shortcuts::duplicates(l).isEmpty(), "phim nguoi dung dat thang mac dinh, khong bao gio con trung");

    MidiConfig c;
    c.enabled = true;
    c.device = "Pad";
    c.map["cut"] = {MidiBinding::Note, 0, 36};
    c.map["output.1.black"] = {MidiBinding::Cc, 1, 20};
    c.save(st);
    st.setValue("midi/map/lock", "note:0:50");                // khong duoc phep
    st.setValue("midi/map/take", "note:0:36");                // trung voi cut
    const MidiConfig r = MidiConfig::load(st);
    CHECK(r.enabled && r.device == "Pad" && r.map.value("output.1.black") == c.map.value("output.1.black"), "cau hinh MIDI: luu/doc lai");
    CHECK(!r.map.contains("lock") && r.map.size() == 2 && MidiConfig::duplicates(r.map).isEmpty(), "cau hinh MIDI: bo 'lock' va nut trung");
    QSettings empty(dir + "/empty.ini", QSettings::IniFormat);
    CHECK(!MidiConfig::load(empty).enabled, "MIDI mac dinh TAT");
}

static void testSystemBackend() {
    auto b = createSystemMidiBackend();
    const QStringList in = b->inputs();
    std::cout << "info: cong MIDI that tren may: " << in.size() << "\n";
    QString err;
    const bool opened = b->open("thiet bi khong ton tai", [](quint8, quint8, quint8) {}, &err);
    b->close();
    b->close();
    CHECK(!opened && !err.isEmpty(), "backend he thong: mo thiet bi khong co -> bao loi, dong nhieu lan an toan");
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
    Entitlements::setCurrent(Entitlements{});

    testActions();
    testBindingAndDecoder();
    testKeyStore(dir.path());
    testSystemBackend();

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
    p.filePath = dir.filePath("input.json");

    {   // cau hinh co san truoc khi mo cua so: phai duoc nap
        QSettings st = appSettings();
        st.setValue("shortcuts/blackAll", "F9");
        st.setValue("midi/enabled", true);
        st.setValue("midi/device", "thiet bi chua cam");
    }

    RenderEngine engine(p);
    engine.startEngine();
    QTemporaryDir plugDir;
    MainWindow w(p, engine, {plugDir.path()});
    w.setAskOnClose(false);
    w.show();
    w.activateWindow();
    w.raise();
    pump(600);

    auto* blackAll = button(&w, "BLACK ALL");
    auto* lockBtn = w.findChild<LockButton*>("lockButton");
    auto* autoBtn = w.findChild<QPushButton*>("auto");
    auto* ctlLabel = w.findChild<QLabel*>("ctlLabel");
    QDoubleSpinBox* dur = nullptr;
    for (auto* s : w.centralWidget()->findChildren<QDoubleSpinBox*>()) if (s->suffix() == " s") dur = s;      // o thoi luong tren thanh chuyen
    CHECK(blackAll && lockBtn && autoBtn && ctlLabel && dur, "tim thay dieu khien tren cua so");
    if (!blackAll || !lockBtn || !autoBtn || !ctlLabel || !dur) return 1;

    auto black = [&](int i) { return engine.output(i)->black.load(); };
    auto allBlack = [&] { return black(0) && black(1); };
    auto noBlack = [&] { return !black(0) && !black(1); };
    auto settle = [&] { waitFor([&] { return !ch->inTransition(); }, 5000); pump(80); };

    // ================= nap cau hinh luc khoi dong =================
    CHECK(w.keyMap().value("blackAll") == QKeySequence(Qt::Key_F9), "khoi dong: nap phim tat da luu");
    CHECK(w.midi().config().enabled && !w.midi().connected() && w.midi().statusText().contains("Chưa thấy"), "khoi dong: MIDI bat nhung thiet bi chua cam -> cho, khong loi");
    CHECK(ctlLabel->text().contains("MIDI: chưa nối"), "thanh trang thai bao MIDI chua noi");
    press(w, QKeySequence(Qt::Key_F9));
    CHECK(waitFor(allBlack), "phim da doi (F9) chay BLACK ALL");
    press(w, QKeySequence(Qt::Key_F9));
    CHECK(waitFor(noBlack), "F9 lan nua: tra hinh");
    press(w, QKeySequence(Qt::Key_B));
    pump(150);
    CHECK(noBlack(), "phim cu (B) khong con tac dung");

    // ================= mac dinh: y nhu truoc =================
    w.applyKeys(shortcuts::defaults());
    press(w, QKeySequence(Qt::Key_B));
    CHECK(waitFor(allBlack), "mac dinh: B = BLACK ALL");
    press(w, QKeySequence(Qt::Key_B));
    CHECK(waitFor(noBlack), "mac dinh: B lan nua tra hinh");
    press(w, QKeySequence(Qt::Key_1));
    CHECK(waitFor([&] { return ch->preview == 0; }), "mac dinh: 1 = Preview Scene 1");
    press(w, QKeySequence(Qt::Key_2));
    CHECK(waitFor([&] { return ch->preview == 1; }), "mac dinh: 2 = Preview Scene 2");
    int pg = ch->program;
    press(w, QKeySequence(Qt::Key_Return));
    CHECK(waitFor([&] { return ch->program != pg; }), "Enter = CUT");
    pg = ch->program;
    press(w, QKeySequence(Qt::Key_Enter));
    CHECK(waitFor([&] { return ch->program != pg; }), "Enter ban phim so = CUT");
    press(w, QKeySequence(Qt::Key_Space));
    CHECK(waitFor([&] { return ch->inTransition(); }), "Space = TAKE");
    settle();

    // ================= phim tu dat =================
    {
        KeyMap m = shortcuts::defaults();
        m["take"] = QKeySequence(Qt::Key_PageDown);
        m["cut"] = QKeySequence(Qt::Key_PageUp);
        m["ftb"] = QKeySequence(Qt::Key_F2);
        m["output.2.black"] = QKeySequence(Qt::Key_F3);
        m["output.1.freeze"] = QKeySequence(Qt::Key_F4);
        m["output.1.pattern"] = QKeySequence(Qt::Key_F5);
        m["output.7.black"] = QKeySequence(Qt::Key_F6);          // Output khong co: khong lam gi, khong crash
        w.applyKeys(m);
        pg = ch->program;
        press(w, QKeySequence(Qt::Key_PageUp));
        CHECK(waitFor([&] { return ch->program != pg; }), "phim phu cho CUT");
        press(w, QKeySequence(Qt::Key_PageDown));
        CHECK(waitFor([&] { return ch->inTransition(); }), "phim phu cho TAKE");
        settle();
        pg = ch->program;
        press(w, QKeySequence(Qt::Key_Return));
        CHECK(waitFor([&] { return ch->program != pg; }), "them phim phu: Enter van la CUT");
        press(w, QKeySequence(Qt::Key_F2));
        CHECK(waitFor([&] { return ch->ftbTarget.load(); }), "F2 = FTB");
        press(w, QKeySequence(Qt::Key_F2));
        CHECK(waitFor([&] { return !ch->ftbTarget.load(); }), "F2 lan nua: bo FTB");
        press(w, QKeySequence(Qt::Key_F3));
        CHECK(black(1) && !black(0), "F3 = Den rieng Output 2");
        press(w, QKeySequence(Qt::Key_F3));
        CHECK(!black(1), "F3 lan nua: Output 2 co hinh lai");
        press(w, QKeySequence(Qt::Key_F4));
        press(w, QKeySequence(Qt::Key_F5));
        CHECK(engine.output(0)->freeze.load() && engine.output(0)->testPattern.load() && !engine.output(1)->freeze.load(), "F4/F5 = Dung/Mau Output 1");
        bool buttonsFollow = false;
        for (auto* tb : w.findChildren<QToolButton*>()) if (tb->text() == "Dừng" && tb->isChecked()) buttonsFollow = true;
        CHECK(buttonsFollow, "nut Dung cua Output sang theo phim ngay");
        press(w, QKeySequence(Qt::Key_F4));
        press(w, QKeySequence(Qt::Key_F5));
        press(w, QKeySequence(Qt::Key_F6));
        pump(100);
        CHECK(noBlack() && !engine.output(0)->freeze.load() && !engine.output(0)->testPattern.load(), "gan cho Output khong ton tai: khong lam gi");
        w.applyKeys(shortcuts::defaults());
    }

    // ================= khoa van hanh voi phim =================
    {
        press(w, QKeySequence(Qt::CTRL | Qt::Key_L));
        CHECK(w.isLocked(), "Ctrl+L khoa");
        press(w, QKeySequence(Qt::CTRL | Qt::Key_L));
        CHECK(w.isLocked(), "Ctrl+L lan nua KHONG mo khoa");
        press(w, QKeySequence(Qt::Key_B));
        CHECK(waitFor(allBlack), "khoa: phim live van chay");
        press(w, QKeySequence(Qt::Key_B));
        waitFor(noBlack);
        lockBtn->setFocus();
        press(w, QKeySequence(Qt::Key_Space), lockBtn);
        CHECK(waitFor([&] { return ch->inTransition(); }) && w.isLocked() && !lockBtn->holding(), "nut khoa dang duoc chon: Space van la TAKE, khong mo khoa");
        settle();
        QAction* inputAct = nullptr;
        for (auto* m : w.menuBar()->findChildren<QMenu*>()) for (auto* act : m->actions()) if (act->text().startsWith("Phím tắt")) inputAct = act;
        CHECK(inputAct && !inputAct->isEnabled(), "khoa: khong mo duoc hop thoai Phim tat va MIDI");
        w.openInputDialog();                                     // bi tu choi ngay (khong mo hop thoai chan)
        CHECK(w.findChild<InputDialog*>() == nullptr, "khoa: goi truc tiep cung bi tu choi");
        KeyMap m = shortcuts::defaults();
        m.remove("lock");
        m["lock"] = QKeySequence(Qt::Key_F12);
        w.applyKeys(m);
        press(w, QKeySequence(Qt::Key_F12));
        CHECK(w.isLocked(), "phim khoa tu dat cung khong mo khoa");
        w.setLocked(false);
        CHECK(inputAct && inputAct->isEnabled() && lockBtn->toolTip().contains("F12"), "mo khoa: menu bat lai; goi y nut khoa ghi phim moi");
        w.applyKeys(shortcuts::defaults());
    }

    // ================= MIDI: noi, lenh, an toan =================
    auto devs = std::make_shared<FakeDevices>();
    devs->plug("Pad gia");
    w.midi().setBackend(std::make_unique<FakeBackend>(devs));
    {
        MidiConfig c;
        c.enabled = true;
        c.device = "Pad gia";
        c.map["cut"] = {MidiBinding::Note, 0, 1};
        c.map["take"] = {MidiBinding::Note, 0, 2};
        c.map["ftb"] = {MidiBinding::Note, 0, 3};
        c.map["blackAll"] = {MidiBinding::Note, 0, 4};
        c.map["preview.1"] = {MidiBinding::Note, 0, 11};
        c.map["preview.2"] = {MidiBinding::Note, 0, 12};
        c.map["preview.9"] = {MidiBinding::Note, 0, 19};
        c.map["output.1.black"] = {MidiBinding::Cc, 0, 21};
        c.map["output.2.freeze"] = {MidiBinding::Note, 0, 22};
        c.map["output.2.pattern"] = {MidiBinding::Program, 0, 23};
        c.map["output.9.black"] = {MidiBinding::Note, 0, 29};
        w.midi().apply(c);
        pump(100);
        CHECK(w.midi().connected() && ctlLabel->text().contains("MIDI: Pad gia"), "MIDI: noi thiet bi gia, hien tren thanh trang thai");

        settle();
        devs->note(11);
        CHECK(waitFor([&] { return ch->preview == 0; }), "MIDI: chon Preview 1");
        devs->note(12);
        CHECK(waitFor([&] { return ch->preview == 1; }), "MIDI: chon Preview 2");
        pg = ch->program;
        devs->note(pg == 0 ? 12 : 11);                          // (chuan bi) Preview phai khac Program thi CUT moi thay duoc
        CHECK(waitFor([&] { return ch->preview != pg; }), "(chuan bi) Preview khac Program");
        devs->note(1);
        CHECK(waitFor([&] { return ch->program != pg; }), "MIDI: CUT");
        pg = ch->program;
        devs->note(1, 0);
        devs->send(0x80, 1, 64);
        pump(150);
        CHECK(ch->program == pg, "MIDI: nha nut khong CUT them");
        devs->send(0x91, 1, 127);
        pump(150);
        CHECK(ch->program == pg, "MIDI: cung so nhung khac kenh MIDI: khong lam gi");

        dur->setValue(2.5);
        QElapsedTimer t; t.start();
        devs->note(2);
        CHECK(waitFor([&] { return ch->inTransition(); }), "MIDI: TAKE");
        const int rej0 = w.controlApi().rejectedCount();
        devs->note(11);
        CHECK(w.controlApi().rejectedCount() == rej0 + 1, "MIDI: chon Preview khi dang chuyen bi tu choi (nhu HTTP/OSC)");
        waitFor([&] { return !ch->inTransition(); }, 6000);
        CHECK(t.elapsed() > 2000, "MIDI: TAKE dung thoi luong dang chon tren thanh chuyen (2,5 s)");
        dur->setValue(0.2);

        settle();
        pg = ch->program;
        const int other = 1 - pg;
        devs->note(pg == 0 ? 11 : 12);                          // (chuan bi) Preview = Scene dang o Program
        CHECK(waitFor([&] { return ch->preview == pg; }), "(chuan bi) Preview trung Program");
        autoBtn->setChecked(true);
        pump(100);
        devs->note(other == 0 ? 11 : 12);
        CHECK(waitFor([&] { return ch->preview == other; }), "MIDI + AUTO: Preview doi");
        pump(300);
        CHECK(ch->program == pg && !ch->inTransition(), "MIDI + AUTO: chon Preview KHONG BAO GIO doi Program");
        autoBtn->setChecked(false);

        devs->note(3);
        CHECK(waitFor([&] { return ch->ftbTarget.load(); }), "MIDI: FTB");
        devs->note(3);
        CHECK(waitFor([&] { return !ch->ftbTarget.load(); }), "MIDI: FTB lan nua");
        devs->note(4);
        CHECK(waitFor(allBlack), "MIDI: BLACK ALL");
        devs->note(4);
        CHECK(waitFor(noBlack), "MIDI: BLACK ALL lan nua");
        devs->send(0xB0, 21, 127);
        CHECK(waitFor([&] { return black(0); }) && !black(1), "MIDI CC: Den Output 1");
        devs->send(0xB0, 21, 0);
        pump(150);
        CHECK(black(0), "MIDI CC: nha nut khong doi gi");
        devs->send(0xB0, 21, 127);
        CHECK(waitFor([&] { return !black(0); }), "MIDI CC: bam lan hai tra hinh");
        devs->note(22);
        devs->send(0xC0, 23, 0);
        CHECK(waitFor([&] { return engine.output(1)->freeze.load() && engine.output(1)->testPattern.load(); }), "MIDI: Dung + Mau Output 2 (Note va Program Change)");
        devs->note(22);
        devs->send(0xC0, 23, 0);
        CHECK(waitFor([&] { return !engine.output(1)->freeze.load() && !engine.output(1)->testPattern.load(); }), "MIDI: tat Dung + Mau");

        const int rej1 = w.controlApi().rejectedCount();
        devs->note(19);
        devs->note(29);
        pump(150);
        CHECK(w.controlApi().rejectedCount() == rej1 + 2 && noBlack(), "MIDI: Scene/Output khong ton tai bi tu choi + ghi log, khong crash");
        CHECK(w.midi().run(LiveAction::fromId("lock")).status == 403 && !w.isLocked(), "MIDI: khong khoa/mo khoa duoc");

        w.setLocked(true);
        pg = ch->program;
        devs->note(1);
        CHECK(waitFor([&] { return ch->program != pg; }), "khoa van hanh: MIDI CUT van chay");
        devs->note(4);
        CHECK(waitFor(allBlack) && w.isLocked(), "khoa van hanh: MIDI BLACK ALL chay, van khoa");
        devs->note(4);
        waitFor(noBlack);
        w.setLocked(false);
    }

    // ================= MIDI: rut / cam lai =================
    {
        const auto stale = devs->current();
        devs->unplug("Pad gia");
        w.midi().poll();
        CHECK(!w.midi().connected() && w.midi().statusText().contains("Mất kết nối") && devs->closes >= 1, "rut thiet bi: dong cong, bao mat ket noi");
        CHECK(ctlLabel->text().contains("MIDI: chưa nối"), "rut thiet bi: thanh trang thai cap nhat");
        pg = ch->program;
        FakeDevices::sendVia(stale, 0x90, 1, 127);               // thong diep muon tu cong da dong
        pump(200);
        CHECK(ch->program == pg, "thong diep muon tu cong cu bi bo qua");
        w.midi().poll();
        CHECK(!w.midi().connected(), "chua cam lai: tiep tuc cho");
        devs->plug("Pad gia");
        CHECK(waitFor([&] { return w.midi().connected(); }, 3000), "cam lai: tu noi trong vong vai giay (bo dem gio)");
        devs->note(1);
        CHECK(waitFor([&] { return ch->program != pg; }), "cam lai: nut hoat dong tro lai");

        // thong diep da xep hang nhung cong bi dong truoc khi UI xu ly
        pg = ch->program;
        const auto h = devs->current();
        FakeDevices::sendVia(h, 0x90, 1, 127);
        w.midi().reconnect();
        pump(200);
        CHECK(ch->program == pg && w.midi().connected(), "Ket noi lai: lenh con xep hang tu cong cu khong chay");

        devs->busy = true;
        w.midi().reconnect();
        CHECK(!w.midi().connected() && w.midi().statusText().contains("chương trình khác"), "thiet bi dang bi chuong trinh khac giu: bao ro");
        devs->busy = false;
        w.midi().poll();
        CHECK(w.midi().connected(), "duoc nha: lan do sau noi duoc");

        // rut/cam lien tuc trong khi thiet bi van dang ban thong diep: khong treo, khong crash
        std::atomic<bool> stop{false};
        std::thread spam([&] {
            while (!stop) {
                if (const auto hh = devs->current()) hh(0x90, 99, 127);          // nut khong gan
                QThread::usleep(200);
            }
        });
        for (int i = 0; i < 50; ++i) {
            devs->unplug("Pad gia");
            w.midi().poll();
            devs->plug("Pad gia");
            w.midi().poll();
            QApplication::processEvents();
        }
        stop = true;
        spam.join();
        pump(100);
        CHECK(w.midi().connected() && !engine.stalled(), "rut/cam 50 lan lien tuc: van noi, khong treo");

        Entitlements::setCurrent(Entitlements::preset("basic"));
        w.midi().apply(MidiConfig(w.midi().config()));
        CHECK(!w.midi().connected() && w.midi().statusText().contains("gói giấy phép"), "goi co ban (khong co dieu khien ngoai): khong mo cong MIDI");
        Entitlements::setCurrent(Entitlements{});
        w.midi().apply(MidiConfig(w.midi().config()));
        CHECK(w.midi().connected(), "mo lai khi goi cho phep");
    }

    // ================= hop thoai: phim =================
    {
        QSettings clean = appSettings();
        clean.remove("shortcuts");
        clean.remove("midi");
        MidiConfig c = w.midi().config();
        c.map = {{"cut", {MidiBinding::Note, 0, 1}}};
        w.midi().apply(c);
        InputDialog dlg(w.midi(), {"O1", "O2"}, w.keyMap());
        auto* err = dlg.findChild<QLabel*>("inputError");
        auto edit = [&](const QString& id) { return dlg.findChild<QKeySequenceEdit*>("key:" + id); };
        CHECK(err && edit("take") && edit("blackAll") && edit("preview.9") && edit("output.2.pattern") && edit("lock") && !edit("output.3.black"), "hop thoai: du dong cho moi lenh va moi Output");
        CHECK(edit("blackAll")->keySequence() == QKeySequence(Qt::Key_B) && edit("take")->keySequence().isEmpty() && err->text().isEmpty(), "hop thoai: hien phim hien tai");
        CHECK(!dlg.findChild<QPushButton*>("learn:lock") && dlg.findChild<QPushButton*>("learn:cut"), "hop thoai: khoa van hanh khong hoc nut MIDI duoc");
        edit("ftb")->setKeySequence(QKeySequence(Qt::Key_B));
        CHECK(err->text().contains("trùng phím"), "hop thoai: bao trung phim ngay");
        CHECK(!dlg.applyAndSave().isEmpty() && !appSettings().contains("shortcuts/ftb"), "trung phim: khong luu");
        edit("ftb")->setKeySequence(QKeySequence(Qt::Key_Space));
        CHECK(!dlg.applyAndSave().isEmpty() && err->text().contains("Space"), "gan Space cho lenh khac: tu choi");
        edit("ftb")->setKeySequence(QKeySequence(Qt::Key_F2));
        CHECK(dlg.applyAndSave().isEmpty() && err->text().isEmpty(), "het loi: luu duoc");
        QSettings st = appSettings();
        CHECK(shortcuts::load(st).value("ftb") == QKeySequence(Qt::Key_F2) && dlg.keys().value("ftb") == QKeySequence(Qt::Key_F2), "phim luu theo may (QSettings)");
        auto* restore = dlg.findChild<QPushButton*>("restoreDefaults");
        CHECK(restore != nullptr, "co nut Khoi phuc mac dinh");
        if (restore) restore->click();
        CHECK(dlg.keys() == shortcuts::defaults(), "khoi phuc mac dinh");
    }

    // ================= hop thoai: hoc nut MIDI =================
    {
        const MidiConfig before = w.midi().config();
        {
            InputDialog dlg(w.midi(), {"O1", "O2"}, w.keyMap());
            CHECK(w.midi().suspended(), "mo hop thoai: MIDI tam ngung chay lenh");
            pg = ch->program;
            devs->note(1);
            pump(150);
            CHECK(ch->program == pg, "hop thoai mo: bam nut MIDI khong CUT");
            auto* learnTake = dlg.findChild<QPushButton*>("learn:take");
            auto* label = dlg.findChild<QLabel*>("midiLabel:take");
            CHECK(learnTake && label && learnTake->isEnabled(), "co nut Hoc (thiet bi da noi)");
            learnTake->click();
            CHECK(dlg.learning() == "take", "dang hoc nut cho TAKE");
            devs->note(60, 0);
            CHECK(dlg.learning() == "take", "nha nut khong duoc hoc");
            devs->note(60);
            CHECK(dlg.learning().isEmpty() && label->text().contains("Note 60") && !ch->inTransition(), "bam tren thiet bi: gan Note 60 cho TAKE, khong chay lenh");
            dlg.findChild<QPushButton*>("learn:ftb")->click();
            devs->note(60);
            auto* err = dlg.findChild<QLabel*>("inputError");
            CHECK(err->text().contains("trùng nút MIDI") && !dlg.applyAndSave().isEmpty(), "trung nut MIDI: bao loi, khong luu");
            dlg.findChild<QPushButton*>("clearMidi:ftb")->click();
            CHECK(err->text().isEmpty(), "xoa nut trung: het loi");
            devs->unplug("Pad gia");
            w.midi().poll();
            CHECK(!learnTake->isEnabled() && dlg.learning().isEmpty(), "rut thiet bi khi hop thoai mo: tat nut Hoc, khong crash");
            devs->plug("Pad gia");
            w.midi().poll();
            CHECK(learnTake->isEnabled(), "cam lai: nut Hoc bat lai");
            dlg.reject();
        }
        CHECK(!w.midi().suspended() && w.midi().config().map == before.map, "Huy: giu bang gan nut cu, MIDI chay lai");
        {
            devs->plug("Pad hai");
            InputDialog dlg(w.midi(), {"O1", "O2"}, w.keyMap());
            auto* combo = dlg.findChild<QComboBox*>("midiDevice");
            const int k = combo->findData("Pad hai");
            CHECK(k >= 0, "danh sach thiet bi co cong moi cam");
            combo->setCurrentIndex(k);
            emit combo->activated(k);
            CHECK(w.midi().config().device == "Pad hai" && w.midi().connected(), "chon thiet bi trong hop thoai: noi ngay de hoc nut");
            dlg.reject();
            CHECK(w.midi().config().device == "Pad gia" && w.midi().connected(), "Huy: tra lai thiet bi cu");
        }
        {
            InputDialog dlg(w.midi(), {"O1", "O2"}, w.keyMap());
            dlg.findChild<QPushButton*>("learn:take")->click();
            devs->note(61);
            CHECK(dlg.applyAndSave().isEmpty(), "OK: luu nut vua hoc");
            dlg.accept();
        }
        QSettings st = appSettings();
        const MidiConfig saved = MidiConfig::load(st);
        CHECK(saved.enabled && saved.device == "Pad gia" && saved.map.value("take") == (MidiBinding{MidiBinding::Note, 0, 61}), "MIDI luu theo may (QSettings)");
        settle();
        devs->note(61);
        CHECK(waitFor([&] { return ch->inTransition(); }), "nut vua hoc chay TAKE ngay, khong can khoi dong lai");
        settle();
    }

    // ================= mo hop thoai tu cua so + khong ghi vao project =================
    {
        QTimer::singleShot(200, &w, [&] {
            if (auto* dlg = w.findChild<InputDialog*>()) {
                dlg->findChild<QKeySequenceEdit*>("key:blackAll")->setKeySequence(QKeySequence(Qt::Key_F10));
                dlg->accept();
            }
        });
        w.openInputDialog();
        CHECK(w.keyMap().value("blackAll") == QKeySequence(Qt::Key_F10), "OK tren hop thoai: phim moi ap dung ngay cho cua so");
        press(w, QKeySequence(Qt::Key_F10));
        CHECK(waitFor(allBlack), "phim moi chay");
        press(w, QKeySequence(Qt::Key_F10));
        waitFor(noBlack);

        w.saveProject(false);
        CHECK(waitFor([&] { return QFile::exists(p.filePath); }), "(chuan bi) luu project");
        pump(200);
        QFile f(p.filePath);
        const bool opened = f.open(QIODevice::ReadOnly);
        const QString text = QString::fromUtf8(f.readAll());
        CHECK(opened && !text.contains("midi", Qt::CaseInsensitive) && !text.contains("shortcut", Qt::CaseInsensitive), "phim tat/MIDI KHONG luu vao file project");
    }

    std::cout << "info: bom phim that " << g_realKeys << " lan, qua tin hieu QShortcut " << g_signalKeys << " lan\n";
    w.close();
    engine.stopEngine();
    std::cout << (g_fail ? "FAILED: " + std::to_string(g_fail) : std::string("ALL PASSED")) << "\n";
    return g_fail ? 1 : 0;
}
