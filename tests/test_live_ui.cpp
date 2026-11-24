// Test M5b (can GL/xvfb): giao dien + engine that voi backend NDI/camera gia — Media Pool (them nguon, trang thai, Hoan tac), hop thoai chon NDI/camera/Output NDI,
// them Output NDI luc dang chay (Program that qua GPU -> NDI), luu project, khoa van hanh, HTTP /api/sources.
#include "InspectorPanel.h"
#include "LiveDialogs.h"
#include "MainWindow.h"
#include "control/ControlApi.h"
#include "live/CameraFake.h"
#include "live/LiveSource.h"
#include "live/NdiFake.h"
#include "media/StaticSources.h"
#include "render/NdiOutput.h"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QUndoStack>
#include <iostream>

using namespace lc;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << msg << "  (" #cond ")\n"; } else std::cout << "ok:   " << msg << "\n"; } while (0)
static void pump(int ms) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { QApplication::processEvents(); QThread::msleep(5); }
}
template <class F> static bool waitFor(F f, int ms) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { QApplication::processEvents(); if (f()) return true; QThread::msleep(5); }
    return f();
}
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

    auto ndi = std::make_shared<FakeNdiBackend>();
    setNdiBackend(ndi);
    auto cam = std::make_shared<FakeCameraBackend>();
    cam->addDevice("cam:1", "Webcam A", {{640, 480, 30}, {1280, 720, 30}, {1920, 1080, 30}});
    cam->addDevice("cam:2", "Card HDMI", {{1920, 1080, 60}});
    setCameraBackend(cam);
    FakeNdiBackend::SenderCfg sc; sc.w = 160; sc.h = 90; sc.fps = 30;
    ndi->addSource("STUDIO (Cam 1)", sc);

    // ---------- hop thoai (khong can engine) ----------
    {
        NdiSourceDialog dlg;
        CHECK(waitFor([&] { return !dlg.refreshing(); }, 3000), "Hop thoai NDI: quet xong khong dong bang UI");
        auto* list = dlg.findChild<QListWidget*>("ndiList");
        CHECK(list && list->count() == 1 && list->item(0)->text() == "STUDIO (Cam 1)", "Hop thoai NDI: liet ke nguon tren mang");
        list->setCurrentRow(0);
        CHECK(dlg.name() == "STUDIO (Cam 1)" && dlg.url() == "fake://STUDIO (Cam 1)", "Hop thoai NDI: chon nguon -> ten + url");
        setNdiBackend(createUnavailableNdiBackend("Chưa cài NDI Runtime"));
        NdiSourceDialog d2;
        auto* note = d2.findChild<QLabel*>("ndiNote");
        CHECK(note && note->text().contains("Chưa cài NDI Runtime") && note->text().contains("NDI chưa dùng được"), "Hop thoai NDI khong co runtime: bao ro ly do, khong crash");
        NdiOutputDialog od({"K1", "K2"});
        CHECK(od.findChild<QLabel*>("ndiOutNote")->text().contains("Chưa cài NDI Runtime") && od.channel() == 0 && od.size() == QSize(1920, 1080) && od.audio() && od.name() == "LumaCast Program",
              "Hop thoai Output NDI: gia tri mac dinh + bao thieu runtime");
        setNdiBackend(ndi);
        CameraSourceDialog cd;
        auto* dev = cd.findChild<QComboBox*>("camDevice");
        auto* fm = cd.findChild<QComboBox*>("camFormat");
        CHECK(dev && dev->count() == 2 && fm && fm->count() == 4, "Hop thoai camera: 2 thiet bi; che do = 'De thiet bi chon' + 3 che do");
        CHECK(cd.deviceId() == "cam:1" && cd.format().w == 1920 && cd.format().fps == 30, "Hop thoai camera: mac dinh che do cao nhat");
        dev->setCurrentIndex(1);
        CHECK(cd.deviceId() == "cam:2" && cd.deviceName() == "Card HDMI" && cd.format().w == 1920 && cd.format().fps == 60, "Hop thoai camera: doi thiet bi -> doi danh sach che do");
        fm->setCurrentIndex(0);
        CHECK(cd.format().w == 0, "Hop thoai camera: 'De thiet bi chon' -> {0,0,0}");
        setCameraBackend(createUnavailableCameraBackend("Camera chưa được hỗ trợ"));
        CameraSourceDialog c2;
        CHECK(c2.findChild<QLabel*>("camNote")->text().contains("Camera chưa dùng được"), "Hop thoai camera khong ho tro: bao ly do");
        setCameraBackend(cam);
    }

    // ---------- engine + cua so chinh ----------
    auto blue = std::make_shared<ColorSource>(QColor(0, 0, 255), "xanh");
    Project p;
    auto sA = std::make_shared<Scene>();
    sA->name = "A"; sA->size = {64, 36};
    sA->layers.push_back({blue, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
    p.scenes = {sA};
    SourceDef bd; bd.id = "bl"; bd.type = "color"; bd.color = QColor(0, 0, 255); bd.label = "xanh"; bd.source = blue;
    p.sources.push_back(bd);
    auto ch = std::make_shared<Channel>();
    ch->name = "K1"; ch->size = {64, 36}; ch->scenes = {sA}; ch->program = 0; ch->preview = -1;
    p.channels = {ch};
    OutputConfig o1; o1.name = "O1"; o1.size = {96, 54}; o1.channel = 0;
    p.outputs = {o1};
    p.filePath = dir.filePath("a.json");

    RenderEngine engine(p);
    engine.startEngine();
    MainWindow w(p, engine, {});
    w.setAskOnClose(false);
    w.show();
    pump(300);
    auto* media = w.findChild<QListWidget*>("mediaList");
    auto* addNdi = w.findChild<QPushButton*>("addNdiBtn");
    auto* addCam = w.findChild<QPushButton*>("addCamBtn");
    CHECK(media && addNdi && addCam && media->count() == 1, "Media Pool: co nut Them NDI/Them camera");

    // ---------- them nguon NDI ----------
    CHECK(w.addNdiSource("STUDIO (Cam 1)", {}, "Cam sân khấu"), "Them nguon NDI");
    pump(200);
    CHECK(media->count() == 2 && p.sources.size() == 2 && p.sources[1].type == "ndi" && p.scenes.size() == 2 && p.scenes[1]->name == "Cam sân khấu", "NDI: them nguon + tao Scene nhu nhap media");
    auto* lsrc = dynamic_cast<LiveSource*>(p.sources[1].source.get());
    CHECK(lsrc != nullptr && waitFor([&] { return lsrc->state() == LiveState::Live; }, 3000), "NDI trong project: ket noi Live");
    CHECK(waitFor([&] { return media->item(1)->text().contains("Cam sân khấu") && media->item(1)->text().startsWith("●"); }, 2500), "Media Pool: hien trang thai ● cho nguon dang Live");
    CHECK(media->item(1)->toolTip().contains("NDI") && media->item(1)->toolTip().contains("160x90"), "Media Pool: tooltip ghi trang thai + do phan giai");
    ndi->setSourceAlive("STUDIO (Cam 1)", false);
    CHECK(waitFor([&] { return lsrc->state() == LiveState::Lost && media->item(1)->toolTip().contains("Mất tín hiệu"); }, 6000), "Mat tin hieu NDI: Media Pool bao 'Mat tin hieu'");
    ndi->setSourceAlive("STUDIO (Cam 1)", true);
    CHECK(waitFor([&] { return lsrc->state() == LiveState::Live; }, 6000), "NDI tro lai -> Live");
    w.undoStack()->undo();
    pump(300);
    CHECK(media->count() == 1 && p.sources.size() == 1 && p.scenes.size() == 1, "Hoan tac them nguon NDI: go nguon + Scene");
    w.undoStack()->redo();
    pump(300);
    CHECK(media->count() == 2 && p.sources.size() == 2 && p.sources[1].type == "ndi", "Lam lai: nguon NDI tro lai");

    // ---------- camera ----------
    CHECK(w.addCameraSource("cam:1", "Webcam A", {1280, 720, 30}), "Them camera");
    pump(200);
    auto* lcam = dynamic_cast<LiveSource*>(p.sources[2].source.get());
    CHECK(media->count() == 3 && p.sources[2].type == "camera" && p.sources[2].size == QSize(1280, 720) && lcam && waitFor([&] { return lcam->state() == LiveState::Live; }, 3000), "Camera: them nguon + ket noi Live");
    cam->removeDevice("cam:1");
    CHECK(waitFor([&] { return lcam->state() == LiveState::Lost; }, 4000) && waitFor([&] { return media->item(2)->toolTip().contains("Mất tín hiệu"); }, 2500), "Rut camera: Media Pool bao mat tin hieu");
    cam->addDevice("cam:1", "Webcam A", {{1280, 720, 30}});
    CHECK(waitFor([&] { return lcam->state() == LiveState::Live; }, 5000), "Cam lai camera -> tu noi lai");

    // ---------- khoa van hanh ----------
    w.setLocked(true);
    CHECK(!w.addNdiSource("X") && !w.addCameraSource("cam:2", "Card", {}) && !w.addNdiOutput("Y", 0, {64, 36}, false) && !addNdi->isEnabled() && !addCam->isEnabled() && p.sources.size() == 3 && engine.outputCount() == 1,
          "Khoa van hanh: khong them duoc nguon/Output NDI, nut bi vo hieu");
    w.setLocked(false);
    CHECK(addNdi->isEnabled() && !w.addNdiSource("   "), "Mo khoa: nut bat lai; ten NDI rong bi tu choi");

    // ---------- Output NDI luc dang chay ----------
    CHECK(w.addNdiOutput("E2E Out", 0, {128, 72}, false), "Them Output NDI luc dang chay");
    CHECK(engine.outputCount() == 2 && engine.output(1) && dynamic_cast<NdiOutputSink*>(engine.output(1)), "Engine: co Output thu 2 la NDI");
    CHECK(waitFor([&] { return ndi->hasSource("E2E Out"); }, 3000), "Nguon NDI 'E2E Out' xuat hien tren mang gia");
    auto stats = ndi->senderStats("E2E Out");
    CHECK(waitFor([&] { return stats && stats->videoFrames > 5; }, 4000), "Output NDI: Program that (qua GPU) duoc gui lien tuc");
    LiveVideoFrame lf;
    CHECK(ndi->lastSentFrame("E2E Out", lf) && lf.w == 128 && lf.h == 72 && lf.rgba[0] < 10 && lf.rgba[1] < 10 && lf.rgba[2] > 245 && lf.rgba[3] == 255,
          "Output NDI: noi dung la Program (Scene A mau xanh) dung kich thuoc cau hinh");
    auto* ndiInfo = [&]() -> QLabel* {
        for (QLabel* l : w.findChildren<QLabel*>()) if (l->text().contains("E2E Out")) return l;
        return nullptr;
    }();
    CHECK(ndiInfo != nullptr, "Panel Outputs: co hang cho Output NDI");
    CHECK(waitFor([&] { return ndiInfo && ndiInfo->text().contains("NDI:") && ndiInfo->text().contains("Đang phát"); }, 3000), "Panel Outputs: hang NDI hien trang thai 'Dang phat'");
    // den / dung hinh dung o Output NDI qua API chung
    ControlApi& api = w.controlApi();
    auto r = call(api, "POST", "/api/outputs/2/black", R"({"on":true})");
    pump(200);
    ndi->lastSentFrame("E2E Out", lf);
    CHECK(r.status == 200 && engine.output(1)->black && lf.rgba[2] < 10, "HTTP: Den Output NDI -> khung gui di la den");
    call(api, "POST", "/api/outputs/2/black", R"({"on":false})");
    // luu project co Output NDI
    w.autosaveNow();
    pump(400);
    QFile f(w.autosavePath());
    CHECK(f.open(QIODevice::ReadOnly), "Autosave ghi duoc");
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonArray outs = root["outputs"].toArray();
    CHECK(outs.size() == 2 && outs[1].toObject()["type"] == "ndi" && outs[1].toObject()["ndiName"] == "E2E Out" && outs[1].toObject()["ndiAudio"].toBool(true) == false &&
              outs[1].toObject()["width"].toInt() == 128, "Project luu Output NDI (type/ndiName/ndiAudio/kich thuoc)");
    const QJsonArray srcs = root["sources"].toArray();
    CHECK(srcs.size() == 3 && srcs[1].toObject()["type"] == "ndi" && srcs[1].toObject()["ndiName"] == "STUDIO (Cam 1)" && srcs[2].toObject()["type"] == "camera" && srcs[2].toObject()["device"] == "cam:1",
          "Project luu nguon NDI + camera");

    // ---------- HTTP ----------
    {
        const QJsonObject st = js(call(api, "GET", "/api/state"));
        const QJsonArray so = st["sources"].toArray();
        CHECK(so.size() == 2 && so[0].toObject()["kind"] == "ndi" && so[0].toObject()["index"].toInt() == 2 && so[1].toObject()["kind"] == "camera" && so[0].toObject()["state"] == "live",
              "GET /api/state: sources (chi nguon truc tiep) co kind/state, chi so 1-based theo Media Pool");
        const QJsonArray ou = st["outputs"].toArray();
        CHECK(ou[0].toObject()["kind"] == "screen" && ou[1].toObject()["kind"] == "ndi" && ou[1].toObject()["ndi"].toObject()["sending"].toBool(), "GET /api/state: outputs co kind va khoi ndi");
        const QJsonArray api2 = js(call(api, "GET", "/api/sources"))["sources"].toArray();
        CHECK(api2.size() == 2 && api2[0].toObject()["audio"].toObject().contains("fillMs") && !api2[1].toObject().contains("audio"), "GET /api/sources: nguon NDI co khoi audio (bu troi), camera khong");
        const uint64_t rc = lsrc->stats().reconnects;
        CHECK(call(api, "POST", "/api/sources/2/reconnect").status == 200 && waitFor([&] { return lsrc->stats().reconnects > rc; }, 3000), "POST /api/sources/2/reconnect: noi lai nguon");
        CHECK(call(api, "POST", "/api/sources/1/reconnect").status == 400, "reconnect nguon khong phai truc tiep -> 400");
        CHECK(call(api, "POST", "/api/sources/9/reconnect").status == 404 && call(api, "POST", "/api/sources/abc/reconnect").status == 400, "reconnect chi so sai -> 404/400");
        w.setLocked(true);
        CHECK(call(api, "POST", "/api/sources/2/reconnect").status == 423, "Khoa van hanh: reconnect bi 423");
        CHECK(call(api, "POST", "/api/outputs/2/black", R"({"on":true})").status == 200, "Khoa van hanh: Den Output NDI van chay (lenh live)");
        call(api, "POST", "/api/outputs/2/black", R"({"on":false})");
        w.setLocked(false);
    }

    engine.stopEngine();
    pump(100);
    setNdiBackend(nullptr);
    setCameraBackend(nullptr);
    std::cout << (g_fail ? "THAT BAI: " : "TAT CA DAT: ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
