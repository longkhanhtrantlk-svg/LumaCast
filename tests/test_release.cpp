// M6 phat hanh: co tinh nang theo goi (giay phep tach loi), ghi nhan NDI, giay phep thu vien, THIRD-PARTY-NOTICES.
#include "AboutInfo.h"
#include "control/ControlApi.h"
#include "control/ControlService.h"
#include "core/Features.h"
#include "core/Project.h"
#include "render/Engine.h"
#include "render/PluginManager.h"
#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

using namespace lc;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << std::string(msg) << "  (" #cond ")\n"; } else std::cout << "ok:   " << std::string(msg) << "\n"; } while (0)

static void pump(int ms) {
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(ms)) { QApplication::processEvents(); QThread::msleep(5); }
}
static void writeFile(const QString& path, const QByteArray& data) { QFile f(path); f.open(QIODevice::WriteOnly | QIODevice::Truncate); f.write(data); }

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc, argv);
    QTemporaryDir dir;

    // ===== Giay phep tinh nang =====
    {
        const Entitlements none = Entitlements::current();      // test chay khong co license.json: mac dinh khong khoa gi
        CHECK(!none.restricted() && none.edition == "unrestricted" && none.maxOutputs == 0, "Mac dinh (khong co file): khong khoa gi");
        for (Feature f : {Feature::Plugins, Feature::Ndi, Feature::Camera, Feature::ExternalControl, Feature::Timeline}) CHECK(none.allows(f), "Mac dinh cho phep " + Entitlements::featureName(f).toStdString());

        const Entitlements b = Entitlements::preset("basic");
        CHECK(b.maxOutputs == 2 && !b.plugins && !b.ndi && !b.camera && !b.externalControl && !b.timeline && b.restricted(), "Preset 'basic' = bang SPEC muc 5 (<=2 Output, khong plug-in/NDI/camera/dieu khien ngoai/timeline)");
        const Entitlements c = Entitlements::preset("commercial");
        CHECK(c.maxOutputs == 0 && c.plugins && c.ndi && c.camera && c.externalControl && c.timeline && !c.restricted(), "Preset 'commercial' = mo het");

        writeFile(dir.filePath("basic.json"), R"({"version":1,"edition":"basic","licensee":"Cong ty A","features":{"timeline":true}})");
        const Entitlements lb = Entitlements::load(dir.filePath("basic.json"));
        CHECK(lb.error.isEmpty() && lb.maxOutputs == 2 && lb.timeline && !lb.ndi && lb.licensee == "Cong ty A", "File: edition basic + ghi de co timeline=true");
        CHECK(lb.summary().contains("tối đa 2 Output") && lb.summary().contains("NDI") && lb.summary().contains("Cong ty A"), "summary() liet ke gioi han + ten nguoi duoc cap");
        writeFile(dir.filePath("c2.json"), R"({"edition":"commercial","maxOutputs":6,"features":{"ndi":false}})");
        const Entitlements lc2 = Entitlements::load(dir.filePath("c2.json"));
        CHECK(lc2.maxOutputs == 6 && !lc2.ndi && lc2.plugins, "File: commercial + maxOutputs 6 + tat NDI");
        // file hong -> KHONG khoa gi (khong lam hong buoi dien), co bao loi
        for (const char* bad : {"{ khong json", R"({"edition":""})", R"({"edition":"basic","version":9})", R"({"edition":"basic","maxOutputs":-3})", "[]"}) {
            writeFile(dir.filePath("bad.json"), bad);
            const Entitlements e = Entitlements::load(dir.filePath("bad.json"));
            CHECK(!e.error.isEmpty() && !e.restricted(), std::string("File hong '") + bad + "': bao loi va khong khoa gi");
        }
        CHECK(!Entitlements::load(dir.filePath("khong_co.json")).error.isEmpty(), "File khong ton tai: bao loi");
        setenv("LUMACAST_LICENSE", "/tmp/x/license.json", 1);
        CHECK(Entitlements::searchPaths().first() == "/tmp/x/license.json", "searchPaths: LUMACAST_LICENSE di dau");
        unsetenv("LUMACAST_LICENSE");

        // ---- thi hanh ----
        Entitlements::setCurrent(Entitlements::preset("basic"));
        QString err;
        SourceDef n; n.type = "ndi"; n.device = "MAY (Nguon)";
        SourceDef cam; cam.type = "camera"; cam.device = "id";
        SourceDef col; col.type = "color"; col.color = Qt::red;
        CHECK(!createSource(n, &err) && err.contains("giấy phép"), "Goi co ban: nguon NDI bi tu choi");
        CHECK(!createSource(cam, &err) && err.contains("giấy phép"), "Goi co ban: camera bi tu choi");
        CHECK(createSource(col, &err) != nullptr, "Goi co ban: nguon mau van duoc");

        {   // plug-in
            const QString root = dir.filePath("plug");
            QDir().mkpath(root + "/good");
            writeFile(root + "/good/plugin.json", R"({"id":"com.test.good","name":"T","apiVersion":1,"type":"effect","entry":"fx.frag","params":[]})");
            writeFile(root + "/good/fx.frag", "vec4 effect(vec2 uv){ return sampleInput(uv); }");
            EffectRegistry reg;
            reg.loadBuiltins();
            PluginManager pm(reg, {root});
            pm.rescan();
            CHECK(!reg.find("com.test.good") && reg.find("builtin.blur"), "Goi co ban: plug-in ngoai bi bo qua, Effect dung san van co");
            Entitlements::setCurrent(Entitlements::preset("commercial"));
            pm.rescan();
            CHECK(reg.find("com.test.good"), "Goi thuong mai: plug-in nap duoc");
        }

        {   // engine: 3 Output, goi co ban toi da 2
            Entitlements::setCurrent(Entitlements::preset("basic"));
            Project p;
            SourceDef a; a.type = "color"; a.color = Qt::red;
            auto src = p.addSource(a);
            auto s = std::make_shared<Scene>();
            s->size = {64, 36};
            s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
            p.scenes = {s};
            auto ch = std::make_shared<Channel>();
            ch->size = {64, 36}; ch->scenes = p.scenes; ch->program = 0; ch->preview = -1;
            p.channels = {ch};
            for (int i = 0; i < 3; ++i) { OutputConfig o; o.name = QString("O%1").arg(i + 1); o.size = {96, 54}; p.outputs.push_back(o); }
            RenderEngine engine(p);
            engine.startEngine();
            pump(700);
            CHECK(engine.outputCount() == 3, "Engine: Output vuot goi van giu cho (chi so/project nguyen ven)");
            CHECK(engine.output(0)->presented > 3 && engine.output(1)->presented > 3, "Engine: hai Output dau chay");
            CHECK(engine.output(2)->presented == 0, "Engine: Output thu 3 (vuot goi co ban) bi tat, khong ve");
            engine.postAndWait([&](double) { engine.syncOutputsToProject(); });
            CHECK(p.outputs.size() == 3 && p.outputs[2].name == "O3", "Luu lai: Output bi tat khong mat khoi project");
            OutputConfig nd; nd.name = "NDI"; nd.channel = 0;
            CHECK(engine.addNdiOutput(nd) == -1, "Goi co ban: khong them duoc Output NDI");

            // dieu khien ngoai
            ControlApi api(p, engine);
            ControlService svc(api);
            ControlSettings cs;
            cs.httpEnabled = true; cs.httpPort = 18877; cs.oscEnabled = true; cs.oscPort = 19877;
            const QString e1 = svc.apply(cs);
            CHECK(!e1.isEmpty() && !svc.httpRunning() && !svc.oscRunning(), "Goi co ban: HTTP/OSC khong mo duoc (khong mo cong nao)");
            Entitlements::setCurrent(Entitlements::preset("commercial"));
            const QString e2 = svc.apply(cs);
            CHECK(e2.isEmpty() && svc.httpRunning() && svc.oscRunning(), "Goi thuong mai: HTTP/OSC mo binh thuong");
            svc.stop();
            engine.stopEngine();
        }
        Entitlements::setCurrent(Entitlements());              // tra ve mac dinh cho cac phan sau
    }

    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
