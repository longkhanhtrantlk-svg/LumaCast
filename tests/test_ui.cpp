// Test UI: dieu khien cac widget cua ControlPanel nhu nguoi dung, kiem tra ket qua o render thread.
#include "app/ControlPanel.h"
#include "media/StaticSources.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

using namespace lc;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << msg << "  (" #cond ")\n"; } else std::cout << "ok:   " << msg << "\n"; } while (0)

static void pump(int ms) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { QApplication::processEvents(); QThread::msleep(5); }
}
template <class W> static W* byText(QWidget* root, const QString& text) {
    for (auto* b : root->findChildren<W*>()) if (b->text() == text) return b;
    return nullptr;
}
static QDoubleSpinBox* byPrefix(QWidget* root, const QString& prefix) {
    for (auto* s : root->findChildren<QDoubleSpinBox*>()) if (s->prefix() == prefix) return s;
    return nullptr;
}

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc, argv);

    Project p;
    SourceDef a; a.type = "color"; a.color = Qt::red; a.label = "do";
    SourceDef b; b.type = "color"; b.color = Qt::blue; b.label = "xanh";
    auto sa = p.addSource(a), sb = p.addSource(b);
    for (auto src : {sa, sb}) {
        auto s = std::make_shared<Scene>();
        s->name = src->name(); s->size = {64, 36};
        s->layers.push_back({src});
        p.scenes.push_back(s);
    }
    auto ch = std::make_shared<Channel>();
    ch->name = "K"; ch->size = {64, 36}; ch->scenes = p.scenes; ch->program = 0; ch->preview = 1;
    p.channels = {ch};
    OutputConfig oc; oc.name = "O"; oc.size = {160, 90};
    p.outputs = {oc};

    QTemporaryDir dir;
    p.filePath = dir.filePath("ui.json");

    RenderEngine engine(p);
    engine.startEngine();
    ControlPanel panel(p, engine);
    panel.show();
    pump(1300);                                   // fps cap nhat moi 1 giay
    CHECK(engine.error().isEmpty() && engine.fps() > 0, "Engine chay cung ControlPanel");

    // Layer: doi alpha
    auto* alpha = byPrefix(&panel, "alpha ");
    CHECK(alpha != nullptr, "Co o chinh alpha cua Layer");
    alpha->setValue(0.5);
    pump(200);
    CHECK(std::abs(p.scenes[0]->layers[0].opacity - 0.5f) < 1e-4, "Doi alpha tren UI -> Scene.layers[0].opacity = 0.5");

    // Them layer / xoa layer
    auto* add = byText<QPushButton>(&panel, "Them layer");
    add->click();
    pump(200);
    CHECK(p.scenes[0]->layers.size() == 2, "Bam 'Them layer' -> Scene co 2 layer");
    pump(50);
    QPushButton* del = nullptr;                   // xoa layer VUA THEM (nut Xoa cuoi cung)
    for (auto* b : panel.findChildren<QPushButton*>()) if (b->text() == "Xoa") del = b;
    CHECK(del != nullptr, "Nut Xoa xuat hien sau khi them layer");
    del->click();
    pump(200);
    CHECK(p.scenes[0]->layers.size() == 1, "Bam 'Xoa' -> con 1 layer");

    // Output: hieu chinh mau
    auto* bright = byPrefix(&panel, "sang ");
    bright->setValue(0.5);
    pump(200);
    CHECK(std::abs(engine.output(0)->config().cal.brightness - 0.5f) < 1e-4, "Doi 'sang' tren UI -> Output.cal.brightness = 0.5");

    // Luu project tu UI
    byText<QPushButton>(&panel, "Luu project")->click();
    pump(400);
    Project q;
    QString err;
    CHECK(loadProject(q, p.filePath, &err), "Project luu tu UI mo lai duoc: " + err.toStdString());
    CHECK(!q.outputs.empty() && std::abs(q.outputs[0].cal.brightness - 0.5f) < 1e-4,
          "File luu chua hieu chinh mau MOI (lay tu render thread)");
    CHECK(!q.scenes.empty() && std::abs(q.scenes[0]->layers[0].opacity - 0.5f) < 1e-4, "File luu chua alpha MOI cua Layer");

    engine.stopEngine();
    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
