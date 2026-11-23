// Test M4a: Effect chain, Transition plug-in/Stinger/easing, manifest plug-in, luu/mo Effect.
#include "core/Channel.h"
#include "core/Project.h"
#include "media/FFmpegSource.h"
#include "media/StaticSources.h"
#include "render/Compositor.h"
#include "render/EffectDef.h"
#include "render/PluginManager.h"
#include <QGuiApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QSettings>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QThread>
#include <cstdlib>
#include <iostream>

using namespace lc;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << msg << "  (" #cond ")\n"; } else std::cout << "ok:   " << msg << "\n"; } while (0)

struct Px { int r, g, b; };
static Px readPx(GL& gl, const RenderTarget& t, int x, int yFromTop) {
    uint8_t p[4];
    gl.glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    gl.glReadPixels(x, t.h - 1 - yFromTop, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    return {p[0], p[1], p[2]};
}
static bool near(Px p, int r, int g, int b, int tol = 6) {
    return std::abs(p.r - r) <= tol && std::abs(p.g - g) <= tol && std::abs(p.b - b) <= tol;
}

static QImage solid(int w, int h, QColor c) { QImage i(w, h, QImage::Format_RGBA8888); i.fill(c); return i; }

static std::shared_ptr<Scene> sceneOf(std::shared_ptr<Source> src, QSize sz, std::vector<EffectInstance> fx = {}) {
    auto s = std::make_shared<Scene>();
    s->size = sz;
    s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true, fx});
    return s;
}
static EffectInstance fxOf(const QString& id, QVariantMap params = {}) { EffectInstance e; e.effectId = id; e.params = params; return e; }

// Nguon dem so lan restart() (Stinger)
class CountingSource : public ImageSource {
public:
    using ImageSource::ImageSource;
    void restart() override { ++restarts; }
    int restarts = 0;
};

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);
    QGuiApplication app(argc, argv);
    QTemporaryDir cfgDir;
    QSettings::setDefaultFormat(QSettings::IniFormat);                  // khong lam ban cau hinh that
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, cfgDir.path());

    // ---------- manifest (khong can GL) ----------
    {
        auto parse = [](const QString& json, const QString& glsl, QString* err = nullptr) {
            EffectDef d;
            return parseManifest(QJsonDocument::fromJson(json.toUtf8()).object(), glsl, d, err) ? d : EffectDef{};
        };
        const QString ok = R"({"id":"com.test.fx","name":"Thu","apiVersion":1,"type":"effect","version":"2.1.0","author":"A",
            "params":[{"name":"amount","type":"float","min":0,"max":10,"default":3},
                      {"name":"on","type":"bool","default":true},
                      {"name":"tint","type":"color","default":[1,0,0,1]},
                      {"name":"center","type":"point","default":[0.25,0.75]},
                      {"name":"mode","type":"choice","choices":["a","b","c"],"default":"b"},
                      {"name":"n","type":"int","min":1,"max":5,"default":9}]})";
        QString err;
        EffectDef d = parse(ok, "vec4 effect(vec2 uv){return sampleInput(uv);}", &err);
        CHECK(d.id == "com.test.fx" && d.params.size() == 6 && d.kind == EffectDef::Effect && d.version == "2.1.0", "Manifest hop le: doc id/kieu/tham so");
        CHECK(d.params[0].def.toDouble() == 3 && d.params[1].def.toBool() && d.params[2].def.value<QColor>() == QColor(255, 0, 0) &&
                  d.params[3].def.toPointF() == QPointF(0.25, 0.75) && d.params[4].def.toInt() == 1 && d.params[5].def.toInt() == 5,
              "Gia tri mac dinh: float, bool, mau, diem, choice theo ten, int bi kep ve max");
        const QString src = buildFragmentSource(d);
        CHECK(src.contains("uniform float amount;") && src.contains("uniform bool on;") && src.contains("uniform vec4 tint;") &&
                  src.contains("uniform vec2 center;") && src.contains("uniform int mode;") && src.contains("#line 1") && src.contains("uniform sampler2D uInput;"),
              "buildFragmentSource: khai bao uniform tu tham so + #line 1");

        struct Bad { const char* json; const char* want; const char* glsl; };
        const Bad bads[] = {
            {R"({"id":"Bad ID","apiVersion":1,"type":"effect"})", "id", "x"},
            {R"({"id":"a.b","apiVersion":2,"type":"effect"})", "apiVersion", "x"},
            {R"({"id":"a.b","apiVersion":1,"type":"video"})", "type", "x"},
            {R"({"id":"a.b","apiVersion":1,"type":"effect"})", "shader", ""},
            {R"({"id":"a.b","apiVersion":1,"type":"effect","params":[{"name":"x"},{"name":"x"}]})", "trung", "x"},
            {R"({"id":"a.b","apiVersion":1,"type":"effect","params":[{"name":"uTime"}]})", "du phong", "x"},
            {R"({"id":"a.b","apiVersion":1,"type":"effect","params":[{"name":"1bad"}]})", "ten", "x"},
            {R"({"id":"a.b","apiVersion":1,"type":"effect","params":[{"name":"c","type":"choice","choices":["only"]}]})", "choice", "x"},
            {R"({"id":"a.b","apiVersion":1,"type":"effect","params":[{"name":"f","type":"float","min":5,"max":1}]})", "min", "x"},
            {R"({"id":"a.b","apiVersion":1,"type":"effect","params":[{"name":"f","type":"matrix"}]})", "type", "x"},
        };
        int rejected = 0;
        for (const Bad& b : bads) {
            EffectDef dd;
            QString e;
            const bool r = parseManifest(QJsonDocument::fromJson(b.json).object(), b.glsl, dd, &e);
            if (!r && !e.isEmpty()) ++rejected; else std::cerr << "  khong tu choi: " << b.json << "\n";
        }
        CHECK(rejected == int(sizeof(bads) / sizeof(bads[0])), "10 manifest sai deu bi tu choi kem thong bao (khong crash)");
        const QString dup = R"({"id":"a.b","apiVersion":1,"type":"effect","params":[{"name":"x"},{"name":"x"}]})";
        QString e2;
        parse(dup, "x", &e2);
        CHECK(!e2.isEmpty(), "Thong bao loi duoc tra ve");
        CHECK(sanitizeParam(d.params[0], 99).toDouble() == 10 && sanitizeParam(d.params[4], 7).toInt() == 2 && sanitizeParam(d.params[2], "xyz").value<QColor>() == QColor(255, 0, 0),
              "sanitizeParam: kep khoang, kep choice, mau sai -> mac dinh");
        CHECK(applyEasing(Easing::Linear, 0.3) == 0.3 && applyEasing(Easing::EaseIn, 0.5) == 0.25 && applyEasing(Easing::EaseOut, 0.5) == 0.75 &&
                  applyEasing(Easing::EaseInOut, 0.5) == 0.5 && applyEasing(Easing::EaseInOut, 2) == 1,
              "Easing: tuyen tinh, vao, ra, hai dau, kep [0,1]");
    }


    // ---------- PluginManager (khong can GL) ----------
    {
        QTemporaryDir dir;
        auto write = [&](const QString& root, const QString& sub, const QString& manifest, const QString& file, const QString& shader) {
            QDir().mkpath(root + "/" + sub);
            QFile m(root + "/" + sub + "/plugin.json"); m.open(QIODevice::WriteOnly | QIODevice::Truncate); m.write(manifest.toUtf8()); m.close();
            if (!file.isEmpty()) { QFile f(root + "/" + sub + "/" + file); f.open(QIODevice::WriteOnly | QIODevice::Truncate); f.write(shader.toUtf8()); f.close(); }
        };
        const QString good = R"({"id":"com.test.good","name":"Tot","apiVersion":1,"type":"effect","entry":"fx.frag","params":[{"name":"a","type":"float"}]})";
        const QString sh1 = "vec4 effect(vec2 uv){ return sampleInput(uv); }";
        const QString root = dir.path();
        write(root, "good", good, "fx.frag", sh1);
        write(root, "badjson", "{ nope", "", "");
        write(root, "oldapi", R"({"id":"com.test.old","apiVersion":2,"type":"effect","entry":"fx.frag"})", "fx.frag", sh1);
        write(root, "evil", R"({"id":"builtin.evil","apiVersion":1,"type":"effect","entry":"fx.frag"})", "fx.frag", sh1);
        write(root, "traversal", R"({"id":"com.test.trav","apiVersion":1,"type":"effect","entry":"../outside.frag"})", "", "");
        write(root, "missing", R"({"id":"com.test.miss","apiVersion":1,"type":"effect","entry":"nofile.frag"})", "", "");
        QFile outside(root + "/outside.frag"); outside.open(QIODevice::WriteOnly); outside.write(sh1.toUtf8()); outside.close();

        EffectRegistry reg;
        reg.loadBuiltins();
        PluginManager pm(reg, {root});
        pm.rescan();
        auto infoOf = [&](const QString& id) { for (const auto& i : reg.infos()) if (i.id == id) return i; return PluginInfo{}; };
        int bad = 0;
        for (const auto& i : reg.infos()) bad += (!i.ok && !i.builtin && !i.error.isEmpty());
        CHECK(reg.find("com.test.good") && infoOf("com.test.good").ok, "PluginManager: nap plug-in hop le");
        CHECK(bad == 5, "5 plug-in loi (JSON hong, apiVersion sai, id 'builtin.', entry ra ngoai thu muc, thieu file) bi tu choi kem thong bao");
        CHECK(!reg.find("builtin.evil") && !reg.find("com.test.trav"), "Plug-in doc hai khong duoc nap (khong ghi de Effect dung san, khong doc file ngoai thu muc)");
        CHECK(reg.find("builtin.blur") != nullptr, "Effect dung san van con");

        const int rev1 = reg.find("com.test.good")->revision;
        pm.rescan();
        CHECK(reg.find("com.test.good")->revision == rev1, "Nap lai khi noi dung khong doi: giu revision (khong bien dich lai)");
        write(root, "good", good, "fx.frag", sh1 + "\n// sua");
        pm.rescan();
        CHECK(reg.find("com.test.good")->revision > rev1, "Sua file shader: revision tang");

        pm.setEnabled("com.test.good", false);
        CHECK(!reg.find("com.test.good") && !infoOf("com.test.good").enabled, "Tat plug-in: khong con trong kho, van liet ke (enabled = false)");
        pm.setEnabled("com.test.good", true);
        CHECK(reg.find("com.test.good") && infoOf("com.test.good").enabled, "Bat lai plug-in");

        // thu muc sau ghi de thu muc truoc (cung id)
        QTemporaryDir dir2;
        write(dir2.path(), "good2", QString(good).replace("\"Tot\"", "\"Ban ghi de\""), "fx.frag", sh1);
        PluginManager pm2(reg, {root, dir2.path()});
        pm2.rescan();
        CHECK(reg.find("com.test.good")->name == "Ban ghi de", "Thu muc cua nguoi dung ghi de plug-in cung id o thu muc di kem");

        // nap lai nong khi file doi (QFileSystemWatcher)
        PluginManager pm3(reg, {root});
        pm3.rescan();
        pm3.watch(true);
        const int scans = pm3.scanCount(), revBefore = reg.find("com.test.good")->revision;
        write(root, "good", good, "fx.frag", sh1 + "\n// hot reload");
        QElapsedTimer tm; tm.start();
        while (pm3.scanCount() == scans && tm.elapsed() < 4000) { QCoreApplication::processEvents(); QThread::msleep(10); }
        CHECK(pm3.scanCount() > scans && reg.find("com.test.good")->revision > revBefore, "Nap lai nong: sua file shader -> tu quet lai, revision tang");
        pm3.watch(false);

        QDir(root + "/good").removeRecursively();
        pm3.rescan();
        CHECK(!reg.find("com.test.good"), "Xoa thu muc plug-in -> bien mat khoi kho sau khi quet lai");
    }

    // ---------- GL ----------
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
    EffectRegistry reg;
    reg.loadBuiltins();
    comp.setEffectContext(&reg, &pool);
    comp.setTime(0);

    auto renderLayer = [&](std::shared_ptr<Source> src, QSize sz, std::vector<EffectInstance> fx, double time = 0) {
        pool.beginFrame();
        comp.setTime(time);
        auto sc = sceneOf(src, sz, fx);
        auto* t = pool.acquire(sz.width(), sz.height());
        comp.renderScene(*sc, *t);
        return t;
    };

    // ---------- moi Effect/Transition dung san phai bien dich duoc ----------
    {
        int total = 0, okc = 0;
        for (const auto& d : reg.all(EffectDef::Effect)) {
            ++total;
            renderLayer(std::make_shared<ImageSource>(solid(8, 8, Qt::gray), "g"), {8, 8}, {fxOf(d->id)});
            okc += reg.compileError(d->id).isEmpty();
        }
        CHECK(total >= 6 && okc == total, QString("Tat ca %1 Effect dung san bien dich duoc").arg(total).toStdString());
        Channel ch; ch.size = {8, 8};
        ch.scenes = {sceneOf(std::make_shared<ColorSource>(Qt::red, "r"), {8, 8}), sceneOf(std::make_shared<ColorSource>(Qt::blue, "b"), {8, 8})};
        ch.program = 0; ch.preview = 1;
        int tt = 0, tok = 0;
        for (const auto& d : reg.all(EffectDef::Transition)) {
            ++tt;
            TransitionSpec sp; sp.type = TransitionType::Plugin; sp.pluginId = d->id;
            ch.setManual(sp, 0.5f);
            pool.beginFrame();
            ch.render(comp, pool, 0);
            tok += reg.compileError(d->id).isEmpty();
            ch.setManual(sp, 0.0f);
        }
        CHECK(tt >= 4 && tok == tt, QString("Tat ca %1 Transition dung san bien dich duoc").arg(tt).toStdString());
    }

    // ---------- Effect ----------
    {
        // huong anh khong bi lat sau khi qua chuoi Effect (nua tren do, nua duoi xanh)
        QImage img(16, 16, QImage::Format_RGBA8888); img.fill(Qt::blue);
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 16; ++x) img.setPixelColor(x, y, Qt::red);
        auto src = std::make_shared<ImageSource>(img, "i");
        auto* t = renderLayer(src, {16, 16}, {fxOf("builtin.colorgrade")});
        CHECK(near(readPx(gl, *t, 8, 2), 255, 0, 0) && near(readPx(gl, *t, 8, 13), 0, 0, 255),
              "Effect khong lat nguoc anh (tren do, duoi xanh) - huong FBO dung");
        t = renderLayer(src, {16, 16}, {fxOf("builtin.colorgrade"), fxOf("builtin.colorgrade")});
        CHECK(near(readPx(gl, *t, 8, 2), 255, 0, 0) && near(readPx(gl, *t, 8, 13), 0, 0, 255), "Chuoi 2 Effect: huong van dung");

        auto red = std::make_shared<ColorSource>(Qt::red, "r");
        t = renderLayer(red, {8, 8}, {fxOf("builtin.colorgrade", {{"saturation", 0.0}})});
        const Px g = readPx(gl, *t, 4, 4);
        CHECK(std::abs(g.r - g.g) <= 3 && std::abs(g.g - g.b) <= 3 && std::abs(g.r - 76) <= 4, "Chinh mau: bao hoa 0 -> xam (luma cua do ~76)");
        t = renderLayer(std::make_shared<ColorSource>(Qt::black, "k"), {8, 8}, {fxOf("builtin.colorgrade", {{"brightness", 0.5}})});
        CHECK(near(readPx(gl, *t, 4, 4), 128, 128, 128, 4), "Chinh mau: do sang +0.5 tren den -> xam 128");
        t = renderLayer(red, {8, 8}, {fxOf("builtin.colorgrade", {{"hue", 120.0}})});
        const Px h = readPx(gl, *t, 4, 4);
        CHECK(h.g > h.r + 80 && h.g > h.b + 80, "Chinh mau: xoay sac do 120 -> do thanh xanh la");
        t = renderLayer(red, {8, 8}, {fxOf("builtin.colorgrade")});
        CHECK(near(readPx(gl, *t, 4, 4), 255, 0, 0, 2), "Chinh mau mac dinh = khong doi");

        // blur: bien do-xanh bi tron
        QImage rb(64, 64, QImage::Format_RGBA8888); rb.fill(Qt::blue);
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 32; ++x) rb.setPixelColor(x, y, Qt::red);
        auto rbS = std::make_shared<ImageSource>(rb, "rb");
        t = renderLayer(rbS, {64, 64}, {fxOf("builtin.blur", {{"radius", 10.0}})});
        const Px edge = readPx(gl, *t, 33, 32), far = readPx(gl, *t, 5, 32);
        CHECK(edge.r > 40 && edge.b > 40 && near(far, 255, 0, 0, 4), "Lam mo: bien do/xanh bi tron, xa bien van nguyen");
        t = renderLayer(rbS, {64, 64}, {fxOf("builtin.blur", {{"radius", 0.0}})});
        CHECK(near(readPx(gl, *t, 33, 32), 0, 0, 255, 2), "Lam mo ban kinh 0 = khong doi");

        // pixelate: hai diem anh trong cung o co cung mau tren nen gradient
        QImage grad(64, 8, QImage::Format_RGBA8888);
        for (int x = 0; x < 64; ++x) for (int y = 0; y < 8; ++y) grad.setPixelColor(x, y, QColor(x * 4, 0, 0));
        t = renderLayer(std::make_shared<ImageSource>(grad, "gr"), {64, 8}, {fxOf("builtin.pixelate", {{"size", 16.0}})});
        CHECK(readPx(gl, *t, 17, 4).r == readPx(gl, *t, 30, 4).r && readPx(gl, *t, 17, 4).r != readPx(gl, *t, 40, 4).r,
              "Pixelate: cung o -> cung mau, o khac -> khac mau");

        // RGB split: canh trang|den, ben trang cach canh 4px -> (0,255,255)
        QImage wb(64, 8, QImage::Format_RGBA8888); wb.fill(Qt::black);
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 32; ++x) wb.setPixelColor(x, y, Qt::white);
        t = renderLayer(std::make_shared<ImageSource>(wb, "wb"), {64, 8}, {fxOf("builtin.rgbsplit", {{"amount", 8.0}, {"angle", 0.0}})});
        CHECK(near(readPx(gl, *t, 28, 4), 0, 255, 255, 8), "Tach RGB: kenh do lech khoi canh -> cyan o ben trang");

        // noise doi theo thoi gian; amount 0 thi khong
        auto gray = std::make_shared<ColorSource>(QColor(128, 128, 128), "g");
        const int n0 = readPx(gl, *renderLayer(gray, {16, 16}, {fxOf("builtin.noise", {{"amount", 0.5}})}, 0.0), 3, 3).r;
        const int n1 = readPx(gl, *renderLayer(gray, {16, 16}, {fxOf("builtin.noise", {{"amount", 0.5}})}, 1.0), 3, 3).r;
        CHECK(n0 != n1, "Nhieu hat: doi theo uTime");
        CHECK(readPx(gl, *renderLayer(gray, {16, 16}, {fxOf("builtin.noise", {{"amount", 0.0}})}, 0.3), 3, 3).r == 128, "Nhieu hat cuong do 0 = khong doi");

        // glow: ben ngoai o sang nhan them anh sang
        QImage dark(48, 48, QImage::Format_RGBA8888); dark.fill(Qt::black);
        for (int y = 20; y < 28; ++y) for (int x = 20; x < 28; ++x) dark.setPixelColor(x, y, Qt::white);
        auto darkS = std::make_shared<ImageSource>(dark, "d");
        const int before = readPx(gl, *renderLayer(darkS, {48, 48}, {}), 33, 24).r;
        const int after = readPx(gl, *renderLayer(darkS, {48, 48}, {fxOf("builtin.glow", {{"intensity", 3.0}, {"radius", 14.0}, {"threshold", 0.5}})}), 33, 24).r;
        CHECK(before == 0 && after > 8, "Glow: ben ngoai vung sang duoc cong them anh sang");

        // tat Effect / thieu plug-in: khong doi, khong crash
        EffectInstance off = fxOf("builtin.colorgrade", {{"saturation", 0.0}}); off.enabled = false;
        CHECK(near(readPx(gl, *renderLayer(red, {8, 8}, {off}), 4, 4), 255, 0, 0, 2), "Effect bi tat: khong tac dung");
        CHECK(near(readPx(gl, *renderLayer(red, {8, 8}, {fxOf("com.khong.ton.tai")}), 4, 4), 255, 0, 0, 2), "Effect thieu plug-in: bo qua, hinh khong doi");
        CHECK(near(readPx(gl, *renderLayer(red, {8, 8}, {fxOf("builtin.push")}), 4, 4), 255, 0, 0, 2), "Dung Transition lam Effect: bo qua an toan");
    }

    // ---------- Shader plug-in loi: tat Effect, ghi loi, khong crash ----------
    {
        EffectDef bad;
        QString err;
        const QString manifest = R"({"id":"com.test.bad","apiVersion":1,"type":"effect","params":[{"name":"k","type":"float","min":0,"max":1,"default":1}]})";
        parseManifest(QJsonDocument::fromJson(manifest.toUtf8()).object(), "vec4 effect(vec2 uv) { return khong_phai_ham(uv); }", bad, &err);
        bad.revision = 1;
        std::vector<EffectDef> defs = builtinEffects();
        defs.push_back(bad);
        reg.setDefs(defs, {});
        auto red = std::make_shared<ColorSource>(Qt::red, "r");
        auto* t = renderLayer(red, {8, 8}, {fxOf("com.test.bad")});
        CHECK(near(readPx(gl, *t, 4, 4), 255, 0, 0, 2) && !reg.compileError("com.test.bad").isEmpty(), "Shader loi: Layer van hien nguyen ban, loi duoc ghi lai");

        // sua shader (revision moi) -> bien dich lai va chay: hot reload
        EffectDef good = bad;
        good.glsl = "vec4 effect(vec2 uv) { vec4 c = sampleInput(uv); return vec4(c.r * (1.0 - k), c.g, c.b, c.a); }";
        good.revision = 2;
        defs.back() = good;
        reg.setDefs(defs, {});
        t = renderLayer(red, {8, 8}, {fxOf("com.test.bad", {{"k", 1.0}})});
        CHECK(reg.compileError("com.test.bad").isEmpty() && near(readPx(gl, *t, 4, 4), 0, 0, 0, 2), "Sua shader (revision moi) -> bien dich lai, ap dung ngay");
        reg.loadBuiltins();
    }

    // ---------- Transition plug-in ----------
    {
        auto makeCh = [&](Channel& ch) {
            ch.size = {64, 36};
            ch.scenes = {sceneOf(std::make_shared<ColorSource>(Qt::red, "r"), {64, 36}), sceneOf(std::make_shared<ColorSource>(Qt::blue, "b"), {64, 36})};
            ch.program = 0; ch.preview = 1;
        };
        auto at = [&](Channel& ch, TransitionSpec sp, float t) {
            ch.setManual(sp, t);
            pool.beginFrame();
            auto* r = ch.render(comp, pool, 0);
            return r;
        };
        auto mk = [](const char* id, QVariantMap p = {}) { TransitionSpec s; s.type = TransitionType::Plugin; s.pluginId = id; s.params = p; return s; };

        Channel c1; makeCh(c1);
        auto* t = at(c1, mk("builtin.push", {{"direction", 0}}), 0.5f);
        CHECK(near(readPx(gl, *t, 5, 18), 255, 0, 0) && near(readPx(gl, *t, 60, 18), 0, 0, 255), "Push trai 50%: nua trai la hinh cu, nua phai la hinh moi");
        c1.setManual(mk("x"), 0.0f);
        t = at(c1, mk("builtin.push", {{"direction", 1}}), 0.5f);
        CHECK(near(readPx(gl, *t, 5, 18), 0, 0, 255) && near(readPx(gl, *t, 60, 18), 255, 0, 0), "Push phai 50%: dao huong");
        c1.setManual(mk("x"), 0.0f);
        t = at(c1, mk("builtin.push", {{"direction", 2}}), 0.5f);
        CHECK(near(readPx(gl, *t, 32, 3), 255, 0, 0) && near(readPx(gl, *t, 32, 32), 0, 0, 255), "Push len 50%: hinh cu o tren, hinh moi o duoi");
        c1.setManual(mk("x"), 0.0f);

        // Push dich chuyen NOI DUNG (khac Slide): hinh cu la anh co gradient
        {
            QImage g(64, 8, QImage::Format_RGBA8888);
            for (int x = 0; x < 64; ++x) for (int y = 0; y < 8; ++y) g.setPixelColor(x, y, QColor(x * 4, 0, 0));
            Channel c; c.size = {64, 8};
            c.scenes = {sceneOf(std::make_shared<ImageSource>(g, "g"), {64, 8}), sceneOf(std::make_shared<ColorSource>(Qt::blue, "b"), {64, 8})};
            c.program = 0; c.preview = 1;
            auto* push = at(c, mk("builtin.push", {{"direction", 0}}), 0.5f);
            const int pushR = readPx(gl, *push, 2, 4).r;          // diem x=2 hien noi dung hinh cu o x ~34 (da bi day sang trai)
            c.setManual(mk("x"), 0.0f);
            auto* slide = at(c, mk("builtin.slide", {{"direction", 0}}), 0.5f);
            const int slideR = readPx(gl, *slide, 2, 4).r;        // hinh cu dung yen: x=2 van la gradient o x=2
            CHECK(pushR > 100 && slideR < 20, "Push day hinh cu di; Slide de hinh cu dung yen");
        }

        Channel c2; makeCh(c2);
        t = at(c2, mk("builtin.luma", {{"pattern", 1}, {"softness", 0.1}}), 0.5f);
        CHECK(near(readPx(gl, *t, 4, 18), 0, 0, 255) && near(readPx(gl, *t, 60, 18), 255, 0, 0), "Luma wipe ngang 50%: tu trai sang hinh moi, phai con hinh cu");
        c2.setManual(mk("x"), 0.0f);
        t = at(c2, mk("builtin.luma", {{"pattern", 0}}), 0.3f);
        CHECK(near(readPx(gl, *t, 32, 18), 0, 0, 255, 20) && near(readPx(gl, *t, 1, 1), 255, 0, 0, 20), "Luma xuyen tam 30%: tam da doi, goc con hinh cu");
        c2.setManual(mk("x"), 0.0f);
        t = at(c2, mk("builtin.zoom", {{"strength", 1.0}}), 0.5f);
        const Px z = readPx(gl, *t, 32, 18);
        CHECK(z.r > 80 && z.b > 80, "Zoom 50%: dang hoa tron giua hai hinh");
        c2.setManual(mk("x"), 0.0f);
        t = at(c2, mk("com.khong.ton.tai"), 0.5f);
        CHECK(near(readPx(gl, *t, 32, 18), 128, 0, 127, 8), "Transition plug-in thieu: roi ve Fade, khong crash");
        c2.setManual(mk("x"), 0.0f);

        // easing + thoi gian
        Channel c3; makeCh(c3);
        TransitionSpec sp = mk("builtin.luma", {{"pattern", 1}}); sp.duration = 1.0; sp.easing = Easing::Linear;
        c3.startTransition(sp, 10.0);
        pool.beginFrame();
        auto* r = c3.render(comp, pool, 10.5);
        const bool linMid = near(readPx(gl, *r, 4, 18), 0, 0, 255) && near(readPx(gl, *r, 60, 18), 255, 0, 0);
        pool.beginFrame();
        c3.render(comp, pool, 11.2);
        CHECK(linMid && !c3.inTransition() && c3.program == 1, "startTransition voi Plugin + easing tuyen tinh: chay theo thoi gian roi hoan tat");
    }

    // ---------- Stinger ----------
    {
        auto st = std::make_shared<CountingSource>(solid(8, 8, QColor(0, 255, 0, 128)), "stinger");
        Channel ch; ch.size = {16, 16};
        ch.scenes = {sceneOf(std::make_shared<ColorSource>(Qt::red, "r"), {16, 16}), sceneOf(std::make_shared<ColorSource>(Qt::blue, "b"), {16, 16})};
        ch.program = 0; ch.preview = 1;
        TransitionSpec sp; sp.type = TransitionType::Stinger; sp.duration = 1.0; sp.cutPoint = 0.5; sp.stinger = st;
        ch.startTransition(sp, 100.0);
        CHECK(st->restarts == 1, "Stinger: restart() duoc goi o luc bat dau");
        pool.beginFrame();
        auto* t = ch.render(comp, pool, 100.25);
        CHECK(near(readPx(gl, *t, 8, 8), 127, 128, 0, 6), "Stinger truoc diem cat: hinh cu + stinger (alpha 50%) de len");
        pool.beginFrame();
        t = ch.render(comp, pool, 100.75);
        CHECK(near(readPx(gl, *t, 8, 8), 0, 128, 127, 6), "Stinger sau diem cat: da doi sang hinh moi, stinger van con");
        pool.beginFrame();
        t = ch.render(comp, pool, 101.1);
        CHECK(ch.program == 1 && !ch.inTransition() && near(readPx(gl, *t, 8, 8), 0, 0, 255, 2), "Stinger xong: hoan tat, khong con stinger de len");
        TransitionSpec nos; nos.type = TransitionType::Stinger;
        ch.startTransition(nos, 200.0);
        CHECK(ch.inTransition(), "Stinger thieu video: roi ve Fade (van chay)");
        ch.cut();

        const char* path = "/tmp/lumacast_stinger_test.mov";
        if (std::system("ffmpeg -y -loglevel error -f lavfi -i color=c=green:s=64x64:d=1:r=25 -pix_fmt yuv420p -c:v mpeg4 /tmp/lumacast_stinger_test.mov") == 0) {
            FFmpegSource v(path);
            TexRef tex;
            for (int i = 0; i < 300 && !tex.id; ++i) { tex = v.acquire(gl); if (!tex.id) QThread::msleep(10); }
            v.restart();
            TexRef after;
            for (int i = 0; i < 300 && !after.id; ++i) { after = v.acquire(gl); if (!after.id) QThread::msleep(10); }
            CHECK(tex.id && after.id && after.w == 64, "FFmpegSource::restart(): sau khi phat lai van co khung hinh moi");
        } else {
            std::cout << "skip: khong co ffmpeg de thu restart() video\n";
        }
    }


    // ---------- Plug-in mau di kem (plugins/) ----------
    {
        EffectRegistry reg2;
        reg2.loadBuiltins();
        PluginManager pm(reg2, {LUMACAST_PLUGINS_DIR});
        pm.rescan();
        comp.setEffectContext(&reg2, &pool);
        int ok = 0;
        for (const char* id : {"lumacast.example.vignette", "lumacast.example.mirror", "lumacast.example.ripple", "lumacast.example.spinwipe"})
            ok += reg2.find(id) != nullptr;
        CHECK(ok == 4, "Nap duoc 4 plug-in mau (vignette, mirror, ripple, spin wipe)");

        auto render = [&](std::shared_ptr<Source> src, QSize sz, std::vector<EffectInstance> fx, double time = 0) {
            pool.beginFrame();
            comp.setTime(time);
            auto sc = sceneOf(src, sz, fx);
            auto* t = pool.acquire(sz.width(), sz.height());
            comp.renderScene(*sc, *t);
            return t;
        };
        auto white = std::make_shared<ImageSource>(solid(64, 64, Qt::white), "w");     // anh that 64x64 (Effect chay theo do phan giai nguon)
        auto* t = render(white, {64, 64}, {fxOf("lumacast.example.vignette", {{"strength", 1.0}, {"radius", 0.3}})});
        CHECK(readPx(gl, *t, 32, 32).r > 240 && readPx(gl, *t, 1, 1).r < 70, "Vignette: tam sang, goc toi");
        t = render(white, {64, 64}, {fxOf("lumacast.example.vignette", {{"strength", 1.0}, {"radius", 0.3}, {"tint", QColor(255, 0, 0)}})});
        CHECK(readPx(gl, *t, 1, 1).r > 200 && readPx(gl, *t, 1, 1).g < 60, "Vignette: doi mau vien (tham so mau)");

        QImage lr(64, 8, QImage::Format_RGBA8888); lr.fill(Qt::blue);
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 32; ++x) lr.setPixelColor(x, y, Qt::red);
        t = render(std::make_shared<ImageSource>(lr, "lr"), {64, 8}, {fxOf("lumacast.example.mirror", {{"mode", 0}})});
        CHECK(near(readPx(gl, *t, 5, 4), 255, 0, 0) && near(readPx(gl, *t, 60, 4), 255, 0, 0), "Mirror ngang: nua phai lat theo nua trai (deu do)");
        t = render(std::make_shared<ImageSource>(lr, "lr"), {64, 8}, {fxOf("lumacast.example.mirror", {{"mode", 1}})});
        CHECK(near(readPx(gl, *t, 5, 4), 255, 0, 0) && near(readPx(gl, *t, 60, 4), 0, 0, 255), "Mirror doc: khong doi theo chieu ngang");

        QImage ck(64, 64, QImage::Format_RGBA8888);
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) ck.setPixelColor(x, y, ((x / 4) + (y / 4)) % 2 ? Qt::white : Qt::black);
        auto ckS = std::make_shared<ImageSource>(ck, "ck");
        std::vector<int> base, zero, wavy;                                // doc het truoc khi render lan sau (pool dung lai FBO)
        auto sample = [&](RenderTarget* r) { std::vector<int> v; for (int i = 0; i < 20; ++i) v.push_back(readPx(gl, *r, 5 + i * 2, 20 + (i % 5)).r); return v; };
        base = sample(render(ckS, {64, 64}, {}));
        zero = sample(render(ckS, {64, 64}, {fxOf("lumacast.example.ripple", {{"amplitude", 0.0}})}, 0));
        wavy = sample(render(ckS, {64, 64}, {fxOf("lumacast.example.ripple", {{"amplitude", 0.08}, {"frequency", 40.0}, {"speed", 5.0}})}, 0.5));
        int diff = 0;
        for (size_t i = 0; i < base.size(); ++i) diff += wavy[i] != base[i];
        CHECK(zero == base && diff > 3, "Ripple: bien do 0 = khong doi; bien do lon lam meo hinh (dung uTime/tham so diem)");

        Channel ch; ch.size = {64, 64};
        ch.scenes = {sceneOf(std::make_shared<ColorSource>(Qt::red, "r"), {64, 64}), sceneOf(std::make_shared<ColorSource>(Qt::blue, "b"), {64, 64})};
        ch.program = 0; ch.preview = 1;
        TransitionSpec sp; sp.type = TransitionType::Plugin; sp.pluginId = "lumacast.example.spinwipe";
        ch.setManual(sp, 0.3f);
        pool.beginFrame();
        auto* tr = ch.render(comp, pool, 0);
        CHECK(near(readPx(gl, *tr, 50, 14), 0, 0, 255) && near(readPx(gl, *tr, 14, 50), 255, 0, 0) && near(readPx(gl, *tr, 14, 14), 255, 0, 0),
              "Spin wipe theo kim dong ho 30%: goc tren-phai da doi, duoi-trai va tren-trai con hinh cu");
        ch.setManual(sp, 0.9f);
        pool.beginFrame();
        tr = ch.render(comp, pool, 0);
        CHECK(near(readPx(gl, *tr, 14, 14), 0, 0, 255), "Spin wipe 90%: goc tren-trai (cuoi cung) da doi");
        ch.setManual(sp, 0.0f);
        comp.setEffectContext(&reg, &pool);
    }

    // ---------- luu / mo Effect ----------
    {
        QTemporaryDir dir;
        Project p;
        SourceDef c; c.type = "color"; c.color = Qt::red; c.label = "do";
        auto src = p.addSource(c);
        auto s = std::make_shared<Scene>();
        s->name = "S"; s->size = {64, 36};
        EffectInstance e1 = fxOf("builtin.blur", {{"radius", 12.5}});
        EffectInstance e2 = fxOf("com.vi.du.plugin", {{"flag", true}, {"tint", QColor(10, 20, 30, 40)}, {"center", QPointF(0.2, 0.8)}, {"mode", 2}});
        e2.enabled = false;
        s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true, {e1, e2}});
        p.scenes = {s};
        auto ch = std::make_shared<Channel>();
        ch->scenes = p.scenes; ch->program = 0; ch->preview = -1;
        p.channels = {ch};
        OutputConfig o; o.name = "O";
        p.outputs = {o};
        const QString file = dir.filePath("fx.json");
        QString err;
        CHECK(saveProject(p, file, &err), "Luu project co Effect");
        Project q;
        CHECK(loadProject(q, file, &err), "Mo lai project co Effect (plug-in 'com.vi.du.plugin' khong ton tai van mo duoc)");
        const auto& fx = q.scenes[0]->layers[0].effects;
        CHECK(fx.size() == 2 && fx[0].effectId == "builtin.blur" && std::abs(fx[0].params["radius"].toDouble() - 12.5) < 1e-9 && fx[0].enabled,
              "Round-trip: Effect dung san + tham so");
        CHECK(fx[1].effectId == "com.vi.du.plugin" && !fx[1].enabled && fx[1].params["flag"].toBool() &&
                  fx[1].params["tint"].value<QColor>() == QColor(10, 20, 30, 40) && fx[1].params["center"].toPointF() == QPointF(0.2, 0.8) &&
                  fx[1].params["mode"].toInt() == 2,
              "Round-trip: Effect cua plug-in thieu van giu du lieu (bool, mau, diem, choice)");
    }

    pool.clear();
    comp.shutdown();
    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
