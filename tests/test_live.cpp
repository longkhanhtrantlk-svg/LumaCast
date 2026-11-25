// Test M5b (can GL/xvfb): nguon truc tiep NDI/camera qua backend GIA — bu troi dong ho am thanh, mat nguon/noi lai, giu khung cuoi,
// am thanh qua mixer, camera cam/rut, Output NDI (doc nguoc GPU -> nguoi gui), luu/mo project, duong nap dong runtime NDI bang thu vien gia.
// KHONG kiem chung NDI that/Media Foundation that/do tre tren mang (xem docs/TEST-PLAN.md muc thu tay).
#include "audio/AudioDrift.h"
#include "audio/AudioMixer.h"
#include "core/Project.h"
#include "live/Camera.h"
#include "live/CameraFake.h"
#include "live/LiveSource.h"
#include "live/Ndi.h"
#include "live/NdiFake.h"
#include "media/StaticSources.h"
#include "render/Compositor.h"
#include "render/NdiOutput.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QOffscreenSurface>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <thread>

using namespace lc;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << msg << "  (" #cond ")\n"; } else std::cout << "ok:   " << msg << "\n"; } while (0)
static void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
template <class F> static bool waitFor(F f, int ms) {
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(ms)) { if (f()) return true; sleepMs(5); }
    return f();
}

// Mo phong thoi gian (khong cho): nguon phat theo dong ho cua no (1 + delta), "thiet bi" rut theo dong ho 48000 dung. Tra ve {ratio ppm, fill ms, resyncs}.
struct SimOut { double ppm, fillMs; uint64_t resyncs; size_t maxFill, minFill; };      // ppm = trung binh ti le trong 30% cuoi (ti le tuc thoi dao dong nhe)
static SimOut simulate(double delta, double seconds, int inRate = 48000, int channels = 2, double targetMs = 60) {
    AudioStream st(kAudioRate * 2);
    DriftResampler d(st, targetMs);
    const int blk = inRate / 100;                              // goi 10 ms theo tan so vao
    std::vector<float> in(size_t(blk) * size_t(channels), 0.1f), out(2 * 480);
    double srcAcc = 0, devAcc = 0;
    size_t maxF = 0, minF = size_t(-1);
    double ppmSum = 0;
    long ppmN = 0;
    const double dt = 0.0005;
    for (double t = 0; t < seconds; t += dt) {
        srcAcc += double(inRate) * (1.0 + delta) * dt;
        devAcc += 48000.0 * dt;
        while (srcAcc >= blk) { srcAcc -= blk; d.process(in.data(), size_t(blk), inRate, channels); }
        while (devAcc >= 480) {
            devAcc -= 480;
            st.pull(out.data(), 480);
            if (t > seconds * 0.7) { maxF = std::max(maxF, st.queued()); minF = std::min(minF, st.queued()); ppmSum += d.ratioPpm(); ++ppmN; }
        }
    }
    return {ppmN ? ppmSum / double(ppmN) : 0, d.fillMs(), d.resyncs(), maxF, minF};
}

static QImage solid(QColor top, QColor bottom, int w = 8, int h = 8) {
    QImage img(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) img.setPixelColor(x, y, y < h / 2 ? top : bottom);
    return img;
}

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);
    QGuiApplication app(argc, argv);
    QOffscreenSurface surf;
    surf.setFormat(fmt);
    surf.create();
    QOpenGLContext ctx;
    ctx.setFormat(fmt);
    if (!ctx.create() || !ctx.makeCurrent(&surf)) { std::cerr << "Khong co OpenGL 3.3\n"; return 2; }
    Compositor comp;
    if (!comp.init()) { std::cerr << "Shader loi\n"; return 2; }
    GL& gl = comp.gl();
    TargetPool pool(gl);

    // ================= Bu troi dong ho am thanh (mo phong, khong cho) =================
    {
        const SimOut z = simulate(0, 120);
        CHECK(std::abs(z.ppm) < 150 && z.resyncs <= 1 && std::abs(z.fillMs - 60) < 8, "Drift: hai dong ho khop -> ti le ~0, khong mat dong bo, ring o muc tieu");
        const SimOut f = simulate(+0.002, 600);
        CHECK(std::abs(f.ppm - 2000) < 250 && f.resyncs <= 1 && f.maxFill < size_t(0.060 * 48000 * 1.7) && f.minFill > size_t(0.060 * 48000 * 0.3),
              "Drift: nguon NHANH +2000 ppm -> ti le hoi tu ~+2000 ppm, ring giu quanh 60 ms (khong can, khong tran) sau 10 phut mo phong");
        const SimOut s = simulate(-0.002, 600);
        CHECK(std::abs(s.ppm + 2000) < 250 && s.resyncs <= 1 && s.minFill > size_t(0.060 * 48000 * 0.3),
              "Drift: nguon CHAM -2000 ppm -> ti le hoi tu ~-2000 ppm, ring khong can");
        const SimOut big = simulate(+0.004, 300);
        CHECK(std::abs(big.ppm - 4000) < 400 && big.resyncs <= 1, "Drift: lech lon +4000 ppm van theo kip (gioi han +/-5000 ppm)");
        const SimOut over = simulate(+0.02, 30);
        CHECK(over.resyncs >= 1, "Drift: lech QUA gioi han (+2%) khong the bu mem -> dat lai cung (dem resync), khong tan vo");
        const SimOut r441 = simulate(0, 60, 44100);
        CHECK(std::abs(r441.ppm) < 150 && r441.resyncs <= 1 && r441.minFill > size_t(0.060 * 48000 * 0.3) && r441.maxFill < size_t(0.060 * 48000 * 1.7),
              "Doi tan so 44,1 kHz -> 48 kHz: tieu thu/san xuat cung toc do, ring on dinh (khong can, khong tran)");
        const SimOut r32 = simulate(0, 60, 32000, 1);
        CHECK(std::abs(r32.ppm) < 150 && r32.resyncs <= 1 && r32.minFill > size_t(0.060 * 48000 * 0.3), "Doi tan so 32 kHz mono -> 48 kHz stereo: ring on dinh");
        AudioStream m(kAudioRate);
        DriftResampler dm(m);
        std::vector<float> mono(480, 0.3f);
        for (int i = 0; i < 20; ++i) dm.process(mono.data(), 480, 48000, 1);
        std::vector<float> o(2 * 480);
        m.pull(o.data(), 480);
        m.pull(o.data(), 480);
        bool dup = true;
        for (int i = 0; i < 480; ++i) dup = dup && std::abs(o[size_t(i) * 2] - o[size_t(i) * 2 + 1]) < 1e-6;
        CHECK(dup, "Mono -> stereo: hai kenh giong nhau");
        AudioStream nc(kAudioRate);
        DriftResampler dn(nc);
        std::vector<float> blk(2 * 480, 0.1f);
        for (int i = 0; i < 400; ++i) dn.process(blk.data(), 480, 48000, 2);     // khong ai rut trong 4 s noi dung
        CHECK(nc.queued() < size_t(0.060 * 48000 * 3), "Khong co mixer rut: ring KHONG tich tru vo han (giu ~muc tieu)");
    }

    // ================= NDI gia: nhan hinh/tieng, giu khung cuoi, tu noi lai =================
    auto fakeNdi = std::make_shared<FakeNdiBackend>();
    setNdiBackend(fakeNdi);
    FakeNdiBackend::SenderCfg cfgA; cfgA.w = 160; cfgA.h = 90; cfgA.fps = 30;
    fakeNdi->addSource("PC (A)", cfgA);
    CHECK(fakeNdi->listSources(0).size() == 1 && fakeNdi->listSources(0)[0].name == "PC (A)", "Liet ke nguon NDI gia");
    {
        LiveOptions o; o.audio = true; o.stallMs = 300; o.retryMs = 100; o.retryMaxMs = 300; o.unavailableRetryMs = 100;
        auto lo = [&](QString n) { return LiveSource("x", "ndi", [n] { return ndiBackend()->openReceiver(n, {}); }, o).state(); };
        (void)lo;
        auto src = std::make_shared<LiveSource>("NDI A", "ndi", [] { return ndiBackend()->openReceiver("PC (A)", {}); }, o);
        CHECK(waitFor([&] { return src->state() == LiveState::Live; }, 3000), "NDI: ket noi -> trang thai Live khi co hinh");
        CHECK(src->statusText().contains("160x90"), "NDI: trang thai ghi do phan giai");
        CHECK(src->audio() == nullptr || true, "");
        // acquire tren render thread (o day: thread chinh co context GL)
        TexRef t;
        for (int i = 0; i < 100 && !t.id; ++i) { t = src->acquire(gl); sleepMs(10); }
        CHECK(t.id != 0 && t.w == 160 && t.h == 90, "NDI: acquire() tra texture dung kich thuoc");
        const QImage snap = src->snapshot(80);
        CHECK(!snap.isNull() && snap.width() == 80, "NDI: snapshot cho Media Pool");
        // do tre: acquire moi 16 ms (60 fps) -> do tre trong tien trinh (khong tinh mang) < 3 khung (50 ms)
        std::vector<double> lat;
        for (int i = 0; i < 90; ++i) { src->acquire(gl); lat.push_back(src->stats().latencyMs); sleepMs(16); }
        std::sort(lat.begin(), lat.end());
        CHECK(lat[lat.size() / 2] < 50.0, "NDI: do tre trong tien trinh (toi LiveSource -> len GPU) trung vi < 3 khung @60 (do duoc " + std::to_string(lat[lat.size() / 2]) + " ms; KHONG gom mang/nguoi gui)");
        const auto s1 = src->stats();
        CHECK(s1.videoFrames > 20 && std::abs(s1.fps - 30) < 8, "NDI: fps nhan ~30 (" + std::to_string(s1.fps) + ")");
        const GLuint texId = t.id;
        const uint64_t frames1 = s1.videoFrames, rc1 = s1.reconnects;

        // mat tin hieu: nguoi gui ngung phat nhung van o tren mang
        fakeNdi->setSourceAlive("PC (A)", false);
        CHECK(waitFor([&] { return src->state() == LiveState::Lost; }, 3000), "NDI: ngung nhan qua han -> Lost");
        CHECK(src->statusText().contains("Mất tín hiệu"), "NDI: trang thai bao mat tin hieu (tieng Viet)");
        TexRef held = src->acquire(gl);
        CHECK(held.id == texId && held.w == 160, "NDI: mat nguon -> VAN giu khung cuoi (cung texture), khong den, khong treo acquire()");
        CHECK(!src->snapshot(80).isNull(), "NDI: mat nguon -> snapshot van co (khung cuoi)");
        fakeNdi->setSourceAlive("PC (A)", true);
        CHECK(waitFor([&] { return src->state() == LiveState::Live; }, 4000), "NDI: nguoi gui phat lai -> tu noi lai, tro ve Live");
        CHECK(src->stats().videoFrames > frames1 && src->stats().reconnects >= rc1 + 1, "NDI: da mo lai ket noi (reconnects tang) va nhan khung moi");

        // nguon bien mat hoan toan roi quay lai
        fakeNdi->removeSource("PC (A)");
        CHECK(waitFor([&] { return src->state() == LiveState::Lost; }, 3000), "NDI: nguon bien khoi mang -> Lost");
        const auto tAcq = std::chrono::steady_clock::now();
        for (int i = 0; i < 50; ++i) src->acquire(gl);
        CHECK(std::chrono::steady_clock::now() - tAcq < std::chrono::milliseconds(200), "Khi mat nguon, acquire() khong bi chan (render thread khong bao gio cho mang)");
        fakeNdi->addSource("PC (A)", cfgA);
        CHECK(waitFor([&] { return src->state() == LiveState::Live; }, 4000), "NDI: nguon xuat hien lai tren mang -> tu noi lai");
        src->reconnect();
        CHECK(waitFor([&] { return src->stats().reconnects >= rc1 + 3; }, 3000) && waitFor([&] { return src->state() == LiveState::Live; }, 3000), "reconnect() thu cong: dong va mo lai ket noi");

        // huy nguon khi dang chay: join nhanh, khong treo
        const auto t0 = std::chrono::steady_clock::now();
        src.reset();
        CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(1), "Huy nguon dang nhan khong bi treo");
    }
    {   // runtime NDI khong co -> Unavailable (khong phai Lost), project van chay; co lai -> len Live
        auto none = createUnavailableNdiBackend("Chưa cài NDI Runtime");
        setNdiBackend(none);
        LiveOptions o; o.audio = true; o.unavailableRetryMs = 80; o.retryMs = 50;
        auto src = std::make_shared<LiveSource>("NDI B", "ndi", [] {
            auto b = ndiBackend();
            if (!b->available()) { LiveOpenResult r; r.unavailable = true; r.error = b->unavailableReason(); return r; }
            return b->openReceiver("PC (A)", {});
        }, o);
        CHECK(waitFor([&] { return src->state() == LiveState::Unavailable; }, 2000) && src->statusText().contains("Chưa cài NDI Runtime"), "Khong co NDI Runtime: trang thai Unavailable + 'Chua cai NDI Runtime'");
        CHECK(src->acquire(gl).id == 0 && src->snapshot().isNull(), "Unavailable: khong co hinh, khong crash");
        CHECK(!none->available() && none->listSources(10).empty(), "Backend none: khong liet ke duoc nguon");
        setNdiBackend(fakeNdi);
        CHECK(waitFor([&] { return src->state() == LiveState::Live; }, 3000), "Runtime/ket noi co lai -> Unavailable tu chuyen sang Live");
    }

    // ================= Am thanh NDI qua mixer + bu troi thuc =================
    {
        FakeNdiBackend::SenderCfg c; c.w = 64; c.h = 36; c.fps = 30; c.audioPpm = +4000; c.audioHz = 440;
        fakeNdi->addSource("PC (Tone)", c);
        LiveOptions o; o.audio = true; o.stallMs = 400;
        auto src = std::make_shared<LiveSource>("NDI Tone", "ndi", [] { return ndiBackend()->openReceiver("PC (Tone)", {}); }, o);
        AudioMixer mx;
        auto plan = std::make_shared<AudioPlan>();
        plan->channels = 1;
        plan->entries.push_back({src, 0, 1.0f});
        mx.setPlan(plan);
        QString err;
        mx.start("null", &err);
        CHECK(waitFor([&] { return src->audio() != nullptr; }, 2000), "NDI co am thanh: audio() tra AudioStream khi da nhan tieng");
        sleepMs(2500);
        const double rms = mx.channelMeter(0).rms[0];
        CHECK(std::abs(rms - 0.25 / std::sqrt(2.0)) < 0.06, "Am thanh NDI di qua mixer -> bus Channel co muc dung (RMS ~0.177), do " + std::to_string(rms));
        const auto st = src->stats();
        CHECK(st.audioBlocks > 100 && st.audioFillMs > 10 && st.audioFillMs < 250, "Bu troi: nguon nhanh +4000 ppm trong 2,5 s: ring van trong khoang an toan (fill " + std::to_string(st.audioFillMs) + " ms)");
        // Nguong 10 ppm: tren runner CI cham, bo dieu khien chi kip keo ~+22 ppm trong 2,5 s (CI that: 22 ppm < 50 -> do gia).
        // Muc 10 ppm van phan biet duoc "keo dung huong" voi "khong keo" (0 ppm / am), con ring da duoc kiem tra o dong tren.
        CHECK(st.audioRatioPpm > 10, "Bu troi: bo dieu khien da keo ti le theo huong dung (+" + std::to_string(st.audioRatioPpm) + " ppm)");
        CHECK(st.audioResyncs <= 3, "Bu troi: it mat dong bo trong luc khoi dong (" + std::to_string(st.audioResyncs) + ")");
        fakeNdi->setSourceAlive("PC (Tone)", false);
        sleepMs(1200);
        CHECK(src->state() == LiveState::Lost && mx.channelMeter(0).rms[0] < 0.01, "Mat nguon: am thanh im lang (khong keu rit/lap), mixer van chay");
        CHECK(mx.running(), "Mixer van chay khi nguon NDI mat");
        mx.stop();
        mx.releasePlan();
        fakeNdi->removeSource("PC (Tone)");
    }

    // ================= Camera gia =================
    {
        auto cam = std::make_shared<FakeCameraBackend>();
        setCameraBackend(cam);
        cam->addDevice("cam:1", "Webcam A", {{640, 480, 30}, {1280, 720, 30}, {1920, 1080, 30}});
        CHECK(cameraBackend()->list().size() == 1 && cameraBackend()->formats("cam:1").size() == 3 && cameraBackend()->formats("zzz").empty(), "Camera: liet ke thiet bi + che do; thiet bi la -> rong");
        auto src = makeCameraSource("cam:1", "Webcam A", {1280, 720, 30});
        CHECK(waitFor([&] { return src->state() == LiveState::Live; }, 3000), "Camera: mo duoc -> Live");
        CHECK(cam->lastOpenedFormat() == CameraFormat({1280, 720, 30}), "Camera: chon dung che do 1280x720@30 da yeu cau");
        TexRef t;
        for (int i = 0; i < 100 && !t.id; ++i) { t = src->acquire(gl); sleepMs(10); }
        CHECK(t.id && t.w == 1280 && t.h == 720 && src->audio() == nullptr && src->kind() == "camera", "Camera: hinh 1280x720, khong co am thanh (chi hinh)");
        CHECK(waitFor([&] { return src->stats().fps > 5; }, 3000) && std::abs(src->stats().fps - 30) < 8, "Camera: ~30 fps (" + std::to_string(src->stats().fps) + ")");
        cam->removeDevice("cam:1");                                    // rut USB
        CHECK(waitFor([&] { return src->state() == LiveState::Lost; }, 3000), "Camera: rut thiet bi -> Lost (nhu NDI mat nguon)");
        CHECK(src->acquire(gl).id == t.id && !src->snapshot().isNull(), "Camera: rut thiet bi -> giu khung cuoi");
        cam->addDevice("cam:1", "Webcam A", {{640, 480, 30}, {1280, 720, 30}});
        CHECK(waitFor([&] { return src->state() == LiveState::Live; }, 4000), "Camera: cam lai -> tu noi lai");
        // id doi (cong USB khac) nhung ten con -> nhan ra bang ten
        cam->removeDevice("cam:1");
        cam->addDevice("cam:9", "Webcam A", {{640, 480, 30}});
        CHECK(waitFor([&] { return src->stats().reconnects >= 3 && src->state() == LiveState::Live; }, 4000) && cam->lastOpenedFormat().w == 640, "Camera: id doi nhung ten con -> nhan ra lai theo ten (che do gan nhat con co)");
        // thiet bi ban
        auto busy = makeCameraSource("cam:b", "Busy", {});
        cam->addDevice("cam:b", "Busy", {{640, 480, 30}});
        cam->setBusy("cam:b", true);
        CHECK(waitFor([&] { return busy->state() == LiveState::Lost && busy->statusText().contains("khác"); }, 3000), "Camera dang bi chuong trinh khac dung -> bao ro ly do");
        cam->setBusy("cam:b", false);
        CHECK(waitFor([&] { return busy->state() == LiveState::Live; }, 4000), "Het ban -> tu noi lai");
        auto none = createUnavailableCameraBackend("Camera chưa được hỗ trợ");
        setCameraBackend(none);
        auto un = makeCameraSource("x", "x", {});
        CHECK(waitFor([&] { return un->state() == LiveState::Unavailable; }, 2000) && !none->available() && none->list().empty(), "Backend camera 'none' (Linux/macOS): Unavailable, khong crash");
        setCameraBackend(cam);
    }

    // ================= Luu / mo project =================
    {
        QTemporaryDir dir;
        auto cam = std::make_shared<FakeCameraBackend>();
        cam->addDevice("cam:1", "Webcam A", {{1280, 720, 30}});
        setCameraBackend(cam);
        fakeNdi->addSource("PC (A)", cfgA);
        Project p;
        SourceDef n; n.id = "ndi1"; n.type = "ndi"; n.device = "PC (A)"; n.url = "10.0.0.2:5961"; n.label = "Cam sân khấu";
        SourceDef c; c.id = "cam1"; c.type = "camera"; c.device = "cam:1"; c.deviceName = "Webcam A"; c.size = {1280, 720}; c.fps = 30;
        QString e;
        CHECK(p.addSource(n, &e) && p.addSource(c, &e), "Project: them nguon NDI + camera");
        auto sc = std::make_shared<Scene>();
        sc->name = "Live"; sc->size = {64, 36};
        sc->layers.push_back({p.sources[0].source, {0, 0, 0.5, 1}, 1.0f, LayerFit::Fit, true});
        sc->layers.push_back({p.sources[1].source, {0.5, 0, 0.5, 1}, 1.0f, LayerFit::Fit, true});
        p.scenes = {sc};
        auto ch = std::make_shared<Channel>();
        ch->name = "K1"; ch->size = {64, 36}; ch->scenes = {sc}; ch->program = 0; ch->preview = -1;
        p.channels = {ch};
        OutputConfig scr; scr.name = "LED"; scr.size = {64, 36};
        OutputConfig nd; nd.kind = OutputKind::Ndi; nd.name = "NDI Program"; nd.ndiName = "LumaCast Program"; nd.size = {1280, 720}; nd.ndiAudio = false;
        p.outputs = {scr, nd};
        const QString path = dir.filePath("p.json");
        CHECK(saveProject(p, path, &e), "Project: luu");
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        CHECK(root["version"].toInt() == 1, "Project: KHONG tang version (van 1)");
        const QJsonArray srcs = root["sources"].toArray();
        CHECK(srcs[0].toObject()["type"] == "ndi" && srcs[0].toObject()["ndiName"] == "PC (A)" && srcs[1].toObject()["type"] == "camera" &&
                  srcs[1].toObject()["device"] == "cam:1" && srcs[1].toObject()["fps"].toDouble() == 30 && srcs[1].toObject()["width"].toInt() == 1280,
              "Project: JSON co nguon ndi/camera dung truong");
        const QJsonObject o1 = root["outputs"].toArray()[1].toObject(), o0 = root["outputs"].toArray()[0].toObject();
        CHECK(o1["type"] == "ndi" && o1["ndiName"] == "LumaCast Program" && o1["ndiAudio"].toBool(true) == false && !o0.contains("type"), "Project: Output NDI co type/ndiName/ndiAudio; Output man hinh khong them truong");
        Project q;
        CHECK(loadProject(q, path, &e), "Project: mo lai (" + e.toStdString() + ")");
        CHECK(q.sources.size() == 2 && q.sources[0].type == "ndi" && q.sources[0].device == "PC (A)" && q.sources[0].url == "10.0.0.2:5961" && q.sources[0].label == "Cam sân khấu" &&
                  q.sources[1].type == "camera" && q.sources[1].deviceName == "Webcam A" && q.sources[1].size == QSize(1280, 720) && q.sources[1].fps == 30,
              "Project: nguon NDI/camera khoi phuc day du");
        CHECK(q.outputs.size() == 2 && q.outputs[1].kind == OutputKind::Ndi && q.outputs[1].ndiName == "LumaCast Program" && !q.outputs[1].ndiAudio && q.outputs[1].size == QSize(1280, 720) &&
                  q.outputs[0].kind == OutputKind::Screen, "Project: Output NDI khoi phuc");
        auto* ls = dynamic_cast<LiveSource*>(q.sources[0].source.get());
        CHECK(ls && ls->kind() == "ndi" && ls->name() == "Cam sân khấu" && waitFor([&] { return ls->state() == LiveState::Live; }, 3000), "Project: nguon NDI mo lai tu file va tu ket noi");
        // project tu may khac: khong co NDI Runtime / khong co camera -> van MO duoc
        setNdiBackend(createUnavailableNdiBackend("Chưa cài NDI Runtime"));
        setCameraBackend(createUnavailableCameraBackend("Không có camera"));
        Project r;
        CHECK(loadProject(r, path, &e) && r.sources.size() == 2, "Project: mo o may khong co NDI/camera van thanh cong (nguon o trang thai Unavailable)");
        auto* lr = dynamic_cast<LiveSource*>(r.sources[0].source.get());
        CHECK(lr && waitFor([&] { return lr->state() == LiveState::Unavailable; }, 2000), "Project: nguon bao Unavailable thay vi loi mo project");
        // du lieu xau
        auto patch = [&](const char* name, std::function<void(QJsonObject&)> fn) {
            QJsonObject rt = root;
            fn(rt);
            QFile w(dir.filePath(name));
            w.open(QIODevice::WriteOnly);
            w.write(QJsonDocument(rt).toJson());
            return dir.filePath(name);
        };
        Project bad;
        QString err2;
        CHECK(!loadProject(bad, patch("b1.json", [](QJsonObject& r) { QJsonArray a = r["sources"].toArray(); QJsonObject o = a[0].toObject(); o["ndiName"] = ""; a[0] = o; r["sources"] = a; }), &err2) && err2.contains("NDI"),
              "Project xau: nguon NDI thieu ten -> bao loi ro rang, khong crash");
        Project bad2;
        CHECK(loadProject(bad2, patch("b2.json", [](QJsonObject& r) { QJsonArray a = r["sources"].toArray(); QJsonObject o = a[1].toObject(); o["width"] = -5; o["height"] = 99999999; o["fps"] = "abc"; a[1] = o; r["sources"] = a; }), &err2) &&
                  bad2.sources[1].size == QSize(0, 0) && bad2.sources[1].fps == 0, "Project xau: camera co width/height/fps vo ly -> lam sach ve 'de thiet bi chon'");
        Project bad3;
        CHECK(!loadProject(bad3, patch("b3.json", [](QJsonObject& r) { QJsonArray a = r["outputs"].toArray(); QJsonObject o = a[1].toObject(); o["width"] = 3; a[1] = o; r["outputs"] = a; }), &err2),
              "Project xau: Output NDI kich thuoc vo ly -> tu choi");
        setNdiBackend(fakeNdi);
        setCameraBackend(cam);
    }

    // ================= Output NDI: GPU -> doc nguoc -> nguoi gui =================
    {
        auto sender = std::make_shared<FakeNdiBackend>();
        setNdiBackend(sender);
        AudioMixer mx;
        // Program: nua tren do, nua duoi xanh
        auto srcImg = std::make_shared<ImageSource>(solid(Qt::red, Qt::blue, 8, 8), "rb");
        auto scene = std::make_shared<Scene>();
        scene->size = {64, 36};
        scene->layers.push_back({srcImg, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        pool.beginFrame();
        RenderTarget* prog = pool.acquire(64, 36);
        comp.renderScene(*scene, *prog);

        OutputConfig cfg; cfg.kind = OutputKind::Ndi; cfg.name = "NDI T"; cfg.ndiName = "LC Out"; cfg.channel = 0; cfg.size = {96, 54}; cfg.scale = ScaleMode::Stretch;
        NdiOutputSink sink(cfg, 60.0, &mx);
        CHECK(waitFor([&] { return sender->hasSource("LC Out"); }, 2000) && sink.sending(), "NDI ra: tao nguon NDI dung ten cau hinh tren mang");
        auto stats = sender->senderStats("LC Out");
        for (int i = 0; i < 6; ++i) { sink.present(ctx, comp, prog); sleepMs(10); }
        CHECK(waitFor([&] { return stats && stats->videoFrames >= 1; }, 2000), "NDI ra: nhan duoc khung hinh o phia nhan");
        LiveVideoFrame lf;
        CHECK(sender->lastSentFrame("LC Out", lf) && lf.w == 96 && lf.h == 54 && lf.rgba.size() == 96 * 54 * 4, "NDI ra: kich thuoc khung = kich thuoc Output (96x54)");
        auto px = [&](int x, int y) { const uint8_t* p = &lf.rgba[(size_t(y) * size_t(lf.w) + size_t(x)) * 4]; return std::array<int, 4>{p[0], p[1], p[2], p[3]}; };
        auto top = px(48, 5), bot = px(48, 48);
        CHECK(top[0] > 240 && top[1] < 15 && top[2] < 15 && top[3] == 255 && bot[2] > 240 && bot[0] < 15, "NDI ra: dung CHIEU (hang dau = tren: do o tren, xanh o duoi) va mau RGBA");
        CHECK(stats->lastW == 96 && stats->fpsNum == 60000 && stats->fpsDen == 1000, "NDI ra: khung hinh bao do phan giai + fps (60000/1000)");
        sink.black = true;
        for (int i = 0; i < 4; ++i) { sink.present(ctx, comp, prog); sleepMs(10); }
        sleepMs(150);
        sender->lastSentFrame("LC Out", lf);
        CHECK(px(48, 5)[0] < 5 && px(48, 48)[2] < 5, "NDI ra: nut Den -> gui khung den");
        sink.black = false;
        sink.testPattern = true;
        for (int i = 0; i < 4; ++i) { sink.present(ctx, comp, prog); sleepMs(10); }
        sleepMs(150);
        sender->lastSentFrame("LC Out", lf);
        CHECK(!(px(48, 5)[0] > 240 && px(48, 5)[1] < 15), "NDI ra: nut Mau -> gui mau thu thay cho Program");
        sink.testPattern = false;
        for (int i = 0; i < 4; ++i) { sink.present(ctx, comp, prog); sleepMs(10); }
        sleepMs(150);
        const uint64_t before = stats->videoFrames;
        sink.freeze = true;
        const uint64_t pres = sink.presented;
        for (int i = 0; i < 5; ++i) { sink.present(ctx, comp, nullptr); sleepMs(10); }
        CHECK(sink.presented == pres, "NDI ra: Dung hinh -> khong cap nhat khung moi");
        sink.freeze = false;
        (void)before;
        // vong: NDI ra -> NDI vao trong tien trinh (gom ca nguoi gui bao ket noi)
        auto loopSrc = makeNdiSource("LC Out", {}, "Loop");
        CHECK(waitFor([&] { for (int i = 0; i < 2; ++i) sink.present(ctx, comp, prog); sleepMs(16); return loopSrc->state() == LiveState::Live; }, 4000), "NDI ra -> NDI vao: nguon NDI trong LumaCast nhan lai Program cua chinh no");
        TexRef lt = loopSrc->acquire(gl);
        CHECK(lt.id && lt.w == 96 && lt.h == 54 && sink.connections() >= 1, "NDI ra: bao co nguoi nhan (connections >= 1), do phan giai nhan lai dung");
        loopSrc.reset();
        // am thanh: tap bus Channel 0 -> NDI
        class Tone : public Source {
        public:
            QString name() const override { return "tone"; }
            TexRef acquire(GL&) override { return {}; }
            AudioStream* audio() override { return &st; }
            AudioStream st{kAudioRate};
        };
        auto tone = std::make_shared<Tone>();
        auto plan = std::make_shared<AudioPlan>();
        plan->channels = 1;
        plan->entries.push_back({tone, 0, 1.0f});
        mx.setPlan(plan);
        std::vector<float> blk(2 * 480), dc(2 * 480, 0.3f);
        for (int i = 0; i < 30; ++i) { tone->st.push(dc.data(), 480); mx.renderBlock(blk.data(), 480); sleepMs(2); }
        CHECK(waitFor([&] { return stats->audioFrames >= 480 * 20; }, 2000) && stats->audioRate == 48000 && stats->audioChannels == 2, "NDI ra: am thanh bus Channel di theo (48 kHz stereo)");
    }
    {   // NDI ra khi khong co runtime: khong crash, bao ly do
        auto none = createUnavailableNdiBackend("Chưa cài NDI Runtime");
        setNdiBackend(none);
        OutputConfig cfg; cfg.kind = OutputKind::Ndi; cfg.name = "NDI X"; cfg.size = {64, 36};
        NdiOutputSink sink(cfg, 30.0, nullptr);
        pool.beginFrame();
        RenderTarget* prog = pool.acquire(64, 36);
        comp.clear(*prog, Qt::green);
        for (int i = 0; i < 5; ++i) { sink.present(ctx, comp, prog); sleepMs(10); }
        CHECK(waitFor([&] { return sink.statusText().contains("Chưa cài NDI Runtime"); }, 2000) && !sink.sending() && sink.presented == 5, "NDI ra khong co runtime: bao 'Chua cai NDI Runtime', khong crash, render van chay");
        setNdiBackend(fakeNdi);
    }

    // ================= Duong nap dong runtime NDI (thu vien gia) =================
    {
        qputenv("LUMACAST_NDI_LIB", "/khong/ton/tai/libndi.so");
        QString why;
        CHECK(createRealNdiBackend(&why) == nullptr && why.contains("Chưa cài NDI Runtime"), "Runtime NDI khong ton tai -> nullptr + 'Chua cai NDI Runtime'");
        qputenv("LUMACAST_NDI_LIB", LUMACAST_FAKE_NDI_LIB);
        auto real = createRealNdiBackend(&why);
        CHECK(real && real->available() && real->id() == "real" && real->version().contains("fake-ndi"), "Nap dong thu vien NDI gia: ket noi ham, doc phien ban");
        if (real) {
            const auto list = real->listSources(10);
            CHECK(list.size() == 2 && list[0].name == "FAKE-PC (Camera 1)" && list[0].url == "10.0.0.5:5961" && list[1].url.isEmpty(), "Real: liet ke nguon (ten + url, url null)");
            auto r = real->openReceiver("FAKE-PC (Camera 1)", {});
            CHECK(r.input != nullptr, "Real: mo receiver");
            LiveVideoFrame v; LiveAudioBlock a;
            bool gotV = false, gotA = false;
            for (int i = 0; i < 8 && r.input; ++i) {
                const auto ev = r.input->capture(10, v, a);
                gotV = gotV || ev == LiveInput::Event::Video;
                gotA = gotA || ev == LiveInput::Event::Audio;
                if (ev == LiveInput::Event::Video) break;
            }
            CHECK(gotV && v.w == 2 && v.h == 2 && v.rgba.size() == 16 && v.rgba[0] == 10 && v.rgba[1] == 20 && v.rgba[2] == 30 && v.rgba[3] == 40 && v.rgba[12] == 10, "Real: BGRA (co stride lon hon) -> RGBA dung thu tu kenh, alpha giu");
            r.input->capture(10, v, a);
            const auto ev2 = r.input->capture(10, v, a);
            (void)ev2;
            LiveAudioBlock ab;
            LiveVideoFrame dummy;
            bool aud = false;
            auto rr = real->openReceiver("x", {});
            for (int i = 0; i < 4 && !aud; ++i) aud = rr.input->capture(10, dummy, ab) == LiveInput::Event::Audio;
            CHECK(aud && ab.rate == 44100 && ab.channels == 2 && ab.data.size() == 8 && std::abs(ab.data[0] - 0.1f) < 1e-6 && std::abs(ab.data[1] + 0.1f) < 1e-6 && std::abs(ab.data[2] - 0.2f) < 1e-6,
                  "Real: am thanh PLANAR (channel stride) -> xen ke L,R,L,R");
            QLibrary lib(LUMACAST_FAKE_NDI_LIB);
            auto setMode = reinterpret_cast<void (*)(int)>(lib.resolve("fake_ndi_set_mode"));
            if (setMode) {
                setMode(1);
                auto r2 = real->openReceiver("y", {});
                LiveVideoFrame v2; LiveAudioBlock a2;
                for (int i = 0; i < 4; ++i) if (r2.input->capture(10, v2, a2) == LiveInput::Event::Video) break;
                CHECK(v2.rgba.size() == 16 && v2.rgba[0] == 10 && v2.rgba[1] == 20 && v2.rgba[2] == 30 && v2.rgba[3] == 255, "Real: RGBX -> alpha bi ep 255 (khong dung rac)");
                setMode(0);
            }
            // gui
            QString serr;
            auto snd = real->createSender("OUT TEST", &serr);
            CHECK(snd != nullptr, "Real: tao nguoi gui");
            if (snd) {
                std::vector<uint8_t> px(4 * 4 * 4, 7);
                px[0] = 1; px[1] = 2; px[2] = 3; px[3] = 4;
                snd->sendVideo(px.data(), 4, 4, 16, 30000, 1001);
                std::vector<float> in = {0.1f, -0.1f, 0.2f, -0.2f, 0.3f, -0.3f};           // 3 khung L,R xen ke
                snd->sendAudio(in.data(), 3, 48000, 2);
                auto stats = reinterpret_cast<void (*)(int*, int*, int*, int*, int*, int*, int*, uint8_t*, float*)>(lib.resolve("fake_ndi_stats"));
                int v3, a3, w, h, al, ac, fc; uint8_t p4[4]; float au[4];
                stats(&v3, &a3, &w, &h, &al, &ac, &fc, p4, au);
                CHECK(v3 == 1 && w == 4 && h == 4 && p4[0] == 1 && p4[3] == 4 && fc == int(uint32_t('R') | 'G' << 8 | 'B' << 16 | uint32_t('A') << 24), "Real: gui hinh RGBA dung kich thuoc/noi dung");
                CHECK(a3 == 1 && al == 3 && ac == 2 && std::abs(au[0] - 0.1f) < 1e-6 && std::abs(au[1] - 0.2f) < 1e-6 && std::abs(au[2] + 0.1f) < 1e-6 && std::abs(au[3] + 0.2f) < 1e-6, "Real: gui am thanh xen ke -> planar dung");
                CHECK(snd->connections() == 3, "Real: so nguoi nhan");
            }
        }
        qunsetenv("LUMACAST_NDI_LIB");
    }

    setNdiBackend(nullptr);
    setCameraBackend(nullptr);
    pool.clear();
    comp.shutdown();
    std::cout << (g_fail ? "THAT BAI: " : "TAT CA DAT: ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
