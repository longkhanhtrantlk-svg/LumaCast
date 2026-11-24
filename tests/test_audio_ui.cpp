// Test M5a (can GL/xvfb): am thanh qua RenderEngine thuc (ke hoach tron moi khung) — mute/am luong/crossfade/T-bar/FTB nghe qua meter,
// dieu khien ngoai (HTTP handler + OSC) cho am thanh, panel Am thanh va Inspector tren MainWindow, khoa van hanh.
#include "AudioPanel.h"
#include "InspectorPanel.h"
#include "MainWindow.h"
#include "control/ControlApi.h"
#include "media/StaticSources.h"
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QListWidget>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <QUndoStack>
#include <iostream>
#include <thread>

using namespace lc;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << msg << "  (" #cond ")\n"; } else std::cout << "ok:   " << msg << "\n"; } while (0)
static bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }
static void pump(int ms) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { QApplication::processEvents(); QThread::msleep(5); }
}

// Nguon hang so (DC) co am thanh, ring luon duoc nap day boi feeder -> muc RMS cua bus = gia tri x he so, de do qua meter.
class DcSource : public Source {
public:
    explicit DcSource(float v) : v_(v), st(kAudioRate) {}
    QString name() const override { return "dc"; }
    TexRef acquire(GL&) override { return {}; }
    AudioStream* audio() override { return &st; }
    void feed() {
        const size_t room = st.writable();
        if (room < 960) return;
        std::vector<float> d(room * 2, v_);
        st.push(d.data(), room);
    }
    float v_;
    AudioStream st;
};

static HttpResponse call(ControlApi& api, const QByteArray& method, const QByteArray& path, const QByteArray& body = {}) {
    HttpRequest r;
    r.method = method;
    r.path = path;
    r.headers["host"] = "127.0.0.1:8080";
    if (!body.isEmpty()) r.headers["content-type"] = "application/json";
    r.body = body;
    r.peer = QHostAddress::LocalHost;
    return api.handleHttp(r);
}
static QJsonObject js(const HttpResponse& r) { return QJsonDocument::fromJson(r.body).object(); }

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc, argv);
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.filePath("cfg"));

    auto A = std::make_shared<DcSource>(0.4f), B = std::make_shared<DcSource>(0.2f), C = std::make_shared<DcSource>(0.1f);
    Project p;
    auto mkScene = [&](const char* name, std::shared_ptr<Source> src) {
        auto s = std::make_shared<Scene>();
        s->name = name; s->size = {64, 36};
        s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        p.scenes.push_back(s);
        return s;
    };
    auto sA = mkScene("A", A), sB = mkScene("B", B), sC = mkScene("C", C);
    auto ch1 = std::make_shared<Channel>(), ch2 = std::make_shared<Channel>();
    ch1->name = "K1"; ch1->size = {64, 36}; ch1->scenes = {sA, sB}; ch1->program = 0; ch1->preview = 1;
    ch2->name = "K2"; ch2->size = {64, 36}; ch2->scenes = {sC}; ch2->program = 0; ch2->preview = -1;
    p.channels = {ch1, ch2};
    OutputConfig o1; o1.name = "O1"; o1.size = {96, 54}; o1.channel = 0;
    OutputConfig o2; o2.name = "O2"; o2.size = {96, 54}; o2.channel = 1;
    p.outputs = {o1, o2};
    p.filePath = dir.filePath("a.json");

    RenderEngine engine(p);
    engine.startEngine();
    std::atomic<bool> feeding{true};
    std::thread feeder([&] { while (feeding) { A->feed(); B->feed(); C->feed(); QThread::msleep(5); } });
    QString aerr;
    engine.audio().start("null", &aerr);
    MainWindow w(p, engine, {});
    w.setAskOnClose(false);
    w.show();
    pump(900);
    AudioMixer& mx = engine.audio();
    auto rms = [&](int c) { return double(mx.channelMeter(c).rms[0]); };

    // ---------- tieng dung muc, bus doc lap ----------
    CHECK(near(rms(0), 0.4, 0.01) && near(rms(1), 0.1, 0.01), "Engine -> mixer: bus Channel 1 = Scene A (0.4), bus Channel 2 = Scene C (0.1); Preview khong ra tieng");
    CHECK(near(mx.masterMeter().rms[0], 0.5, 0.02), "Master = tong hai bus");

    // ---------- API: mute / am luong ----------
    ControlApi api(p, engine);
    bool locked = false;
    api.setLockProvider([&] { return locked; });
    {
        auto r = call(api, "POST", "/api/channels/1/mute", "{}");
        pump(200);
        CHECK(r.status == 200 && ch1->mute && near(rms(0), 0.0, 0.002) && near(rms(1), 0.1, 0.01), "HTTP mute (khong tham so = bat/tat qua lai): Channel 1 im, Channel 2 giu nguyen");
        CHECK(A->st.queued() > 0 && A->st.underruns() == 0, "Channel mute van rut nguon A deu (khong underrun) de dong ho/hinh khong dung");
        r = call(api, "POST", "/api/channels/1/mute", R"({"on":false})");
        pump(200);
        CHECK(r.status == 200 && !ch1->mute && near(rms(0), 0.4, 0.01), "HTTP mute on=false: co tieng tro lai dung muc");
        r = call(api, "POST", "/api/channels/1/volume", R"({"volume":0.5})");
        pump(200);
        CHECK(r.status == 200 && near(ch1->volume, 0.5, 1e-6) && near(rms(0), 0.2, 0.01) && near(ch2->volume, 1.0, 1e-6), "HTTP volume 0.5: bus Channel 1 giam mot nua, Channel 2 khong doi");
        r = call(api, "POST", "/api/channels/1/volume", R"({"db":-6})");
        pump(200);
        CHECK(r.status == 200 && near(ch1->volume, 0.501, 0.002), "HTTP volume theo dB: -6 dB = 0.501");
        CHECK(call(api, "POST", "/api/channels/1/volume", R"({"volume":3})").status == 400 && call(api, "POST", "/api/channels/1/volume", R"({"volume":"a"})").status == 400 &&
                  call(api, "POST", "/api/channels/1/volume", R"({"db":-70})").status == 400 && call(api, "POST", "/api/channels/1/volume", "{}").status == 400,
              "HTTP volume: gia tri sai (ngoai khoang, sai kieu, thieu) -> 400, khong doi");
        CHECK(call(api, "POST", "/api/channels/9/mute", "{}").status == 404 && call(api, "POST", "/api/channels/abc/mute", "{}").status == 400 &&
                  call(api, "POST", "/api/channels/1/mute", R"({"on":"x"})").status == 400, "HTTP mute: kenh khong co 404, chi so xau 400, 'on' sai kieu 400");
        call(api, "POST", "/api/channels/1/volume", R"({"volume":1})");
        pump(150);
        const QJsonObject st = js(call(api, "GET", "/api/state"));
        const QJsonObject a1 = st["channels"].toArray()[0].toObject()["audio"].toObject();
        CHECK(near(a1["volume"].toDouble(), 1.0, 1e-6) && a1["mute"].toBool() == false, "GET /api/state: moi Channel co audio {volume, mute}");
        const QJsonObject au = js(call(api, "GET", "/api/audio"));
        CHECK(au["running"].toBool() && au["device"].toString().contains("Null") && au["channels"].toArray().size() == 2 &&
                  au["channels"].toArray()[0].toObject()["rms"].toArray()[0].toDouble() > 0.3 && au["master"].toObject().contains("peak"),
              "GET /api/audio: thiet bi + meter tung Channel va master");
        // OSC
        OscMessage m;
        m.address = "/lumacast/channel/2/mute";
        m.args = {1};
        CHECK(api.handleOsc(m).ok(), "OSC mute 1 chap nhan");
        pump(200);
        CHECK(ch2->mute && near(rms(1), 0.0, 0.002) && !ch1->mute, "OSC /channel/2/mute 1: chi Channel 2 im");
        m.args = {0};
        api.handleOsc(m);
        m.address = "/lumacast/channel/2/volume";
        m.args = {0.5};
        CHECK(api.handleOsc(m).ok(), "OSC volume float");
        pump(200);
        CHECK(!ch2->mute && near(ch2->volume, 0.5, 1e-6) && near(rms(1), 0.05, 0.01), "OSC /channel/2/volume 0.5 va mute 0: Channel 2 con 0.05");
        m.args = {QString("x")};
        CHECK(api.handleOsc(m).status == 400, "OSC volume sai kieu -> 400");
        m.args = {9.0};
        CHECK(api.handleOsc(m).status == 400 && near(ch2->volume, 0.5, 1e-6), "OSC volume ngoai khoang -> 400, khong doi");
        m.address = "/lumacast/channel/7/mute";
        m.args = {1};
        CHECK(api.handleOsc(m).status == 404, "OSC kenh khong ton tai -> 404");
        m.address = "/lumacast/channel/2/volume"; m.args = {1.0};
        api.handleOsc(m);
        // khoa van hanh: mute/am luong la lenh LIVE -> van chay
        locked = true;
        auto r2 = call(api, "POST", "/api/channels/1/mute", R"({"on":true})");
        pump(150);
        CHECK(r2.status == 200 && ch1->mute, "Khoa van hanh: mute Channel (lenh live) van chay qua HTTP");
        call(api, "POST", "/api/channels/1/mute", R"({"on":false})");
        locked = false;
        pump(150);
    }

    // ---------- crossfade theo transition ----------
    {
        // Fade tuyen tinh 1.2 s: A (0.4) -> B (0.2). Muc phai doi muot, don dieu, khong vuot khoang [0.2, 0.4].
        auto r = call(api, "POST", "/api/channels/1/take", R"({"type":"fade","duration":1.2,"easing":"linear"})");
        CHECK(r.status == 200, "Take Fade qua API");
        std::vector<double> v;
        QElapsedTimer t; t.start();
        while (t.elapsed() < 1700) { pump(10); v.push_back(rms(0)); }
        double mn = 1, mx2 = 0, maxStep = 0;
        for (size_t i = 0; i < v.size(); ++i) { mn = std::min(mn, v[i]); mx2 = std::max(mx2, v[i]); if (i) maxStep = std::max(maxStep, std::abs(v[i] - v[i - 1])); }
        CHECK(mn > 0.19 && mx2 < 0.41 && maxStep < 0.03, "Fade: muc doi muot tu 0.4 xuong 0.2 (khong tut/vot, buoc toi da " + std::to_string(maxStep) + ")");
        CHECK(!ch1->inTransition() && ch1->program == 1 && near(rms(0), 0.2, 0.01), "Fade xong: Program = B, am thanh = B");
        CHECK(B->st.underruns() == 0 && A->st.underruns() == 0, "Crossfade: ca hai nguon duoc rut deu, khong underrun");

        // T-bar: dung theo vi tri thanh keo (Program=B 0.2, Preview=A 0.4)
        TransitionSpec sp;
        sp.type = TransitionType::Fade;
        engine.post([&, sp](double) { ch1->setManual(sp, 0.5f); });
        pump(250);
        CHECK(near(rms(0), 0.3, 0.015), "T-bar 50%: am thanh = tron 50/50 (0.3)");
        engine.post([&, sp](double) { ch1->setManual(sp, 0.25f); });
        pump(250);
        CHECK(near(rms(0), 0.25, 0.015), "T-bar keo ve 25%: am thanh theo ngay (0.25), khong phu thuoc thoi gian");
        engine.post([&, sp](double) { ch1->setManual(sp, 0.0f); });
        pump(250);
        CHECK(near(rms(0), 0.2, 0.01) && ch1->program == 1, "T-bar huy (ve 0): tro lai Program");

        // Dip to black: giua chung im lang
        r = call(api, "POST", "/api/channels/1/take", R"({"type":"dip","duration":1.2,"easing":"linear"})");
        double lo = 1;
        QElapsedTimer t2; t2.start();
        while (t2.elapsed() < 1700) { pump(10); lo = std::min(lo, rms(0)); }
        CHECK(lo < 0.03 && near(rms(0), 0.4, 0.01), "Dip to black: tieng xuong ~0 o giua roi len lai Scene moi (A = 0.4)");

        // FTB keo ca tieng
        call(api, "POST", "/api/channels/1/ftb", R"({"on":true,"duration":0.5})");
        pump(900);
        CHECK(rms(0) < 0.005 && near(rms(1), 0.1, 0.01), "FTB: Channel 1 im het, Channel 2 khong anh huong");
        call(api, "POST", "/api/channels/1/ftb", R"({"on":false,"duration":0.5})");
        pump(900);
        CHECK(near(rms(0), 0.4, 0.01), "Tat FTB: tieng tro lai");
    }

    // ---------- UI ----------
    {
        auto* panel = w.audioPanel();
        CHECK(panel && w.findChild<QWidget*>("dockAudio") != nullptr, "MainWindow co dock 'Âm thanh'");
        auto* mute0 = panel->findChild<QToolButton*>("muteBtn0");
        auto* vol0 = panel->findChild<QSlider*>("volSlider0");
        auto* meter0 = panel->findChild<LevelMeter*>("meter0");
        CHECK(mute0 && vol0 && meter0 && panel->findChild<QToolButton*>("muteBtn1") && panel->findChild<LevelMeter*>("meterMaster"), "Moi Channel co nut tat tieng, thanh am luong, meter; co meter master");
        pump(300);
        CHECK(meter0->peak(0) > 0.2f, "Meter tren UI nhan duoc muc that cua mixer");
        mute0->click();
        pump(250);
        CHECK(ch1->mute && near(rms(0), 0.0, 0.002), "Bam nut tat tieng -> Channel.mute + bus im");
        mute0->click();
        pump(250);
        CHECK(!ch1->mute && near(rms(0), 0.4, 0.01), "Bam lai -> co tieng");
        vol0->setValue(50);
        pump(250);
        CHECK(near(ch1->volume, 0.5, 1e-6) && near(rms(0), 0.2, 0.01), "Keo thanh am luong 50% -> Channel.volume 0.5, bus giam mot nua");
        engine.post([&](double) { ch1->volume = 1.0f; ch1->mute = true; });      // doi tu ben ngoai (HTTP/OSC) -> UI cap nhat
        pump(400);
        CHECK(vol0->value() == 100 && mute0->isChecked(), "Doi tu dieu khien ngoai: nut/thanh tren UI cap nhat theo (khong ghi de nguoc)");
        mute0->click();
        pump(200);

        // khoa van hanh: tat tieng/am luong van dung duoc, doi thiet bi bi khoa
        w.setLocked(true);
        auto* dev = panel->findChild<QComboBox*>("audioDevice");
        CHECK(dev && !dev->isEnabled() && mute0->isEnabled() && vol0->isEnabled(), "Khoa van hanh: khoa chon thiet bi, GIU tat tieng + am luong (lenh live)");
        w.setLocked(false);
        CHECK(dev->isEnabled(), "Mo khoa: chon thiet bi dung lai");

        // chon thiet bi
        CHECK(panel->applyDevice("null").isEmpty() && p.audioDevice == "null" && mx.running() && mx.deviceName().contains("Null"), "Chon thiet bi: luu vao project va mo lai mixer");
        panel->applyDevice(QString());
        CHECK(p.audioDevice.isEmpty() && mx.running(), "Ve thiet bi mac dinh");
        pump(300);
        CHECK(near(rms(0), 0.4, 0.01), "Doi thiet bi giua chung: mixer giu ke hoach, am thanh tiep tuc");

        // Inspector: am luong Layer, hoan tac
        auto* scenes = w.findChild<QListWidget*>("sceneList");
        emit scenes->itemDoubleClicked(scenes->item(0));
        pump(300);
        auto* insp = w.inspector();
        insp->setCurrentIndex(0);
        auto* lv = insp->findChild<QDoubleSpinBox*>("layerVolume");
        CHECK(lv && near(lv->value(), 100, 1e-6), "Inspector: o 'tieng' cua Layer, mac dinh 100%");
        lv->setValue(50);
        pump(300);
        CHECK(near(p.scenes[0]->layers[0].volume, 0.5, 1e-5) && near(rms(0), 0.2, 0.01), "Doi tieng Layer 50% -> Layer.volume 0.5 va bus giam");
        w.undoStack()->undo();
        pump(300);
        CHECK(near(p.scenes[0]->layers[0].volume, 1.0, 1e-5) && near(lv->value(), 100, 1e-6) && near(rms(0), 0.4, 0.01), "Hoan tac am luong Layer: ve 100%, am thanh theo");
    }

    feeding = false;
    feeder.join();
    engine.stopEngine();
    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
