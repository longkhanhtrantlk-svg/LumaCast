#include "ControlPanel.h"
#include "media/FFmpegSource.h"
#include "media/StaticSources.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
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

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(0);                 // tu dieu nhip bang master clock, khong dung vsync
    fmt.setDepthBufferSize(0);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QCommandLineParser cli;
    cli.setApplicationDescription("LumaCast M1 - dieu khien man hinh LED");
    cli.addHelpOption();
    cli.addOption({{"o", "output"}, "Output: WxH[@man_hinh][/kenh]. Lap lai de them output. Mac dinh 1280x720.", "spec"});
    cli.addOption({{"m", "media"}, "File video/anh de tao Scene. Lap lai duoc.", "file"});
    cli.addOption({"fullscreen", "Output chay toan man hinh (moi output mot man hinh)."});
    cli.addOption({"scale", "fit | fill | stretch | pixel (mac dinh fit).", "mode", "fit"});
    cli.addOption({"fps", "Nhip master (mac dinh 60).", "n", "60"});
    cli.process(app);

    Project proj;
    proj.fps = cli.value("fps").toDouble();
    const QString sm = cli.value("scale");
    const ScaleMode scale = sm == "fill" ? ScaleMode::Fill : sm == "stretch" ? ScaleMode::Stretch
                          : sm == "pixel" ? ScaleMode::Pixel : ScaleMode::Fit;

    QStringList specs = cli.values("output");
    if (specs.isEmpty()) specs << "1280x720";
    int maxChannel = 0;
    for (int i = 0; i < specs.size(); ++i) {
        OutputConfig o;
        if (!parseOutput(specs[i], i, cli.isSet("fullscreen"), scale, o)) {
            qCritical().noquote() << "Output khong hop le:" << specs[i];
            return 2;
        }
        maxChannel = std::max(maxChannel, o.channel);
        proj.outputs.push_back(o);
    }

    // Scene: test pattern, mau trơn, moi file media mot scene.
    auto addScene = [&](const QString& name, std::shared_ptr<Source> src, LayerFit fit = LayerFit::Fit) {
        auto s = std::make_shared<Scene>();
        s->name = name;
        s->layers.push_back({std::move(src), {0, 0, 1, 1}, 1.0f, fit});
        proj.scenes.push_back(s);
    };
    addScene("Test", std::make_shared<ImageSource>(makeTestPattern(1920, 1080), "test"));
    addScene("Xanh", std::make_shared<ColorSource>(QColor(20, 60, 200), "xanh"), LayerFit::Stretch);
    for (const QString& f : cli.values("media")) {
        if (kImageExt.contains(QFileInfo(f).suffix().toLower())) {
            auto img = ImageSource::fromFile(f);
            if (!img) { qWarning().noquote() << "Khong doc duoc anh:" << f; continue; }
            addScene(img->name(), img);
        } else {
            auto v = std::make_shared<FFmpegSource>(f);
            addScene(v->name(), v);
        }
    }

    for (int c = 0; c <= maxChannel; ++c) {
        auto ch = std::make_shared<Channel>();
        ch->name = QString("Channel %1").arg(c + 1);
        ch->scenes = proj.scenes;
        ch->program = 0;
        ch->preview = proj.scenes.size() > 1 ? 1 : -1;
        proj.channels.push_back(ch);
    }

    RenderEngine engine(proj);
    engine.startEngine();
    ControlPanel panel(proj, engine);
    panel.show();
    const int rc = app.exec();
    engine.stopEngine();
    if (!engine.error().isEmpty()) qCritical().noquote() << engine.error();
    return rc;
}
