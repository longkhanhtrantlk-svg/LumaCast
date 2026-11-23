#include "MainWindow.h"
#include "Theme.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSurfaceFormat>

using namespace lc;

static const QStringList kImageExt = {"png", "jpg", "jpeg", "bmp", "gif", "webp"};

// "WxH[@man_hinh][/kenh]"  vd: 1920x1080@1/1
static bool parseOutput(const QString& s, int index, bool fullscreen, ScaleMode scale, OutputConfig& o) {
    static const QRegularExpression re(R"(^(\d+)x(\d+)(?:@(\d+))?(?:/(\d+))?$)");
    const auto m = re.match(s);
    if (!m.hasMatch()) return false;
    o.name = QString("Output %1").arg(index + 1);
    o.size = QSize(m.captured(1).toInt(), m.captured(2).toInt());
    o.fullscreen = fullscreen;
    o.screenIndex = m.captured(3).isEmpty() ? index : m.captured(3).toInt();
    o.channel = m.captured(4).isEmpty() ? 0 : m.captured(4).toInt() - 1;
    o.scale = scale;
    return o.size.width() > 0 && o.size.height() > 0;
}

static bool parseDims(const QString& s, int& a, int& b) {
    static const QRegularExpression re(R"(^(\d+)x(\d+)$)");
    const auto m = re.match(s);
    if (!m.hasMatch()) return false;
    a = m.captured(1).toInt();
    b = m.captured(2).toInt();
    return a > 0 && b > 0;
}

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(0);                 // tu dieu nhip bang master clock, khong dung vsync
    fmt.setDepthBufferSize(0);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setApplicationName("LumaCast");
    applyDarkTheme(app);
    QCommandLineParser cli;
    cli.setApplicationDescription("LumaCast - LED Display Controller");
    cli.addHelpOption();
    cli.addOption({{"p", "project"}, "Mo file project (.json). Bo qua cac tuy chon -o/-m/--wall.", "file"});
    cli.addOption({"save", "Luu project ra file roi chay tiep.", "file"});
    cli.addOption({{"o", "output"}, "Output: WxH[@man_hinh][/kenh]. Lap lai de them output. Mac dinh 1280x720.", "spec"});
    cli.addOption({{"m", "media"}, "File video/anh de tao Scene. Lap lai duoc.", "file"});
    cli.addOption({"wall", "Video wall CxR: chia 1 kenh rong cho C*R output (dung cung --cell).", "CxR"});
    cli.addOption({"cell", "Kich thuoc moi o cua --wall, mac dinh 1920x1080.", "WxH", "1920x1080"});
    cli.addOption({"fullscreen", "Output chay toan man hinh (moi output mot man hinh)."});
    cli.addOption({"scale", "fit | fill | stretch | pixel (mac dinh fit).", "mode", "fit"});
    cli.addOption({"fps", "Nhip master (mac dinh 60).", "n", "60"});
    cli.process(app);

    Project proj;
    if (cli.isSet("project")) {
        QString err;
        QString toLoad = cli.value("project");
        // Khoi phuc: neu ban autosave moi hon file project (vd mat dien giua show) thi hoi nguoi dung.
        const QFileInfo main(toLoad), autosave(toLoad + ".autosave");
        if (autosave.exists() && (!main.exists() || autosave.lastModified() > main.lastModified()) &&
            QMessageBox::question(nullptr, "Khoi phuc project",
                                  QString("Tim thay ban tu dong luu moi hon:\n%1\n(%2)\n\nKhoi phuc ban nay?")
                                      .arg(autosave.fileName(), autosave.lastModified().toString("dd/MM/yyyy HH:mm:ss")),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
            toLoad = autosave.filePath();
        if (!loadProject(proj, toLoad, &err)) {
            qCritical().noquote() << "Khong mo duoc project:" << err;
            return 2;
        }
        proj.filePath = cli.value("project");           // luu lai vao file goc, ke ca khi nap tu autosave
        if (cli.isSet("fullscreen"))
            for (auto& o : proj.outputs) o.fullscreen = true;
    } else {
        proj.fps = cli.value("fps").toDouble();
        const QString sm = cli.value("scale");
        const ScaleMode scale = sm == "fill" ? ScaleMode::Fill : sm == "stretch" ? ScaleMode::Stretch
                              : sm == "pixel" ? ScaleMode::Pixel : ScaleMode::Fit;

        QSize canvas(1920, 1080);        // do phan giai Scene/Channel
        if (cli.isSet("wall")) {
            int cols, rows, cw, ch;
            if (!parseDims(cli.value("wall"), cols, rows) || !parseDims(cli.value("cell"), cw, ch)) {
                qCritical() << "--wall/--cell khong hop le (dung CxR, WxH)";
                return 2;
            }
            proj.outputs = makeWall(0, cols, rows, QSize(cw, ch), 0, cli.isSet("fullscreen"));
            canvas = QSize(cols * cw, rows * ch);
        } else {
            QStringList specs = cli.values("output");
            if (specs.isEmpty()) specs << "1280x720";
            for (int i = 0; i < specs.size(); ++i) {
                OutputConfig o;
                if (!parseOutput(specs[i], i, cli.isSet("fullscreen"), scale, o)) {
                    qCritical().noquote() << "Output khong hop le:" << specs[i];
                    return 2;
                }
                proj.outputs.push_back(o);
            }
        }
        int maxChannel = 0;
        for (const auto& o : proj.outputs) maxChannel = std::max(maxChannel, o.channel);

        auto addScene = [&](const QString& name, std::shared_ptr<Source> src, LayerFit fit) {
            auto s = std::make_shared<Scene>();
            s->name = name;
            s->size = canvas;
            s->layers.push_back({std::move(src), {0, 0, 1, 1}, 1.0f, fit, true});
            proj.scenes.push_back(s);
        };
        SourceDef t; t.type = "testpattern"; t.size = canvas; t.label = "Test";
        addScene("Test", proj.addSource(t), LayerFit::Stretch);
        SourceDef c; c.type = "color"; c.color = QColor(20, 60, 200); c.label = "Xanh";
        addScene("Xanh", proj.addSource(c), LayerFit::Stretch);
        for (const QString& f : cli.values("media")) {
            SourceDef d;
            d.path = QFileInfo(f).absoluteFilePath();
            d.type = kImageExt.contains(QFileInfo(f).suffix().toLower()) ? "image" : "video";
            QString err;
            auto src = proj.addSource(d, &err);
            if (!src) { qWarning().noquote() << err; continue; }
            addScene(src->name(), src, LayerFit::Fit);
        }
        for (int i = 0; i <= maxChannel; ++i) {
            auto ch = std::make_shared<Channel>();
            ch->name = QString("Channel %1").arg(i + 1);
            ch->size = canvas;
            ch->scenes = proj.scenes;
            ch->program = 0;
            ch->preview = proj.scenes.size() > 1 ? 1 : -1;
            proj.channels.push_back(ch);
        }
    }

    if (cli.isSet("save")) {
        QString err;
        if (!saveProject(proj, cli.value("save"), &err)) { qCritical().noquote() << "Luu loi:" << err; return 2; }
        proj.filePath = cli.value("save");
    }

    RenderEngine engine(proj);
    engine.startEngine();
    MainWindow window(proj, engine);
    window.show();
    const int rc = app.exec();
    engine.stopEngine();
    if (!engine.error().isEmpty()) qCritical().noquote() << engine.error();
    return rc;
}
