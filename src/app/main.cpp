#include "MainWindow.h"
#include "core/Log.h"
#include "core/AppSettings.h"
#include "core/Startup.h"
#include "media/FFmpegSource.h"
#include "render/Benchmark.h"
#include "Theme.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
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

// "XxY" hoac "N" (ca hai chieu); 0..kMaxWallGap
static bool parseGap(const QString& s, QSize& gap) {
    static const QRegularExpression re(R"(^(\d{1,5})(?:x(\d{1,5}))?$)");
    const auto m = re.match(s);
    if (!m.hasMatch()) return false;
    const int x = m.captured(1).toInt(), y = m.captured(2).isEmpty() ? x : m.captured(2).toInt();
    gap = QSize(x, y);
    return x <= kMaxWallGap && y <= kMaxWallGap;
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
    // LUMACAST_CONFIG_DIR: dat thu muc cau hinh + log rieng (ban mang theo/kiem thu); khong dat thi dung vi tri chuan cua he dieu hanh (registry tren Windows).
    const QString cfgDir = qEnvironmentVariable("LUMACAST_CONFIG_DIR");
    if (!cfgDir.isEmpty()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, cfgDir);
    }
    initLogging(cfgDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logs" : cfgDir + "/logs");
    qInfo() << "LumaCast khoi dong";
    applyDarkTheme(app);
    QCommandLineParser cli;
    cli.setApplicationDescription("LumaCast - LED Display Controller");
    cli.addHelpOption();
    cli.addOption({{"p", "project"}, "Mo file project (.json). Bo qua cac tuy chon -o/-m/--wall.", "file"});
    cli.addOption({"save", "Luu project ra file roi chay tiep.", "file"});
    cli.addOption({{"o", "output"}, "Output: WxH[@man_hinh][/kenh]. Lap lai de them output. Mac dinh 1920x1080.", "spec"});
    cli.addOption({{"m", "media"}, "File video/anh de tao Scene. Lap lai duoc.", "file"});
    cli.addOption({"wall", "Video wall CxR: chia 1 kenh rong cho C*R output (dung cung --cell).", "CxR"});
    cli.addOption({"cell", "Kich thuoc moi o cua --wall, mac dinh 1920x1080.", "WxH", "1920x1080"});
    cli.addOption({"gap", "Bu khe giua cac man cua --wall, diem anh NGANGxDOC (mot so = ca hai). Canvas rong them phan khe.", "XxY", "0x0"});
    cli.addOption({"fullscreen", "Output chay toan man hinh (moi output mot man hinh)."});
    cli.addOption({"scale", "fit | fill | stretch | pixel (mac dinh fit).", "mode", "fit"});
    cli.addOption({"fps", "Nhip master (mac dinh 60).", "n", "60"});
    cli.addOption({"benchmark", "Chay benchmark render roi thoat. Vd: \"outputs=4,size=3840x2160,source=synthetic-yuv,seconds=10\" (xem docs/TEST-PLAN.md muc 4).", "spec"});
    cli.addOption({"bench-json", "Ghi ket qua benchmark ra file JSON.", "file"});
    cli.addOption({"hwdec", "Giai ma video phan cung: auto (Windows: D3D11VA) | off | ten thiet bi FFmpeg. Mac dinh lay tu cai dat.", "mode"});
    cli.addOption({"no-gpu-yuv", "Doi YUV->RGB tren CPU (sws_scale) thay vi shader GPU."});
    cli.addOption({"no-audio", "Khong mo thiet bi am thanh (dong ho hinh chay theo dong ho tuong)."});
    cli.process(app);

    if (cli.isSet("benchmark")) return runBenchmarkCli(cli.value("benchmark"), cli.value("bench-json"));
    QSize gap(0, 0);
    if (cli.isSet("gap") && (!cli.isSet("wall") || cli.isSet("project") || !parseGap(cli.value("gap"), gap))) {
        qCritical().noquote() << QString("--gap khong hop le: dung cung --wall (khong dung voi --project), dang XxY, 0..%1").arg(kMaxWallGap);
        return 2;
    }
    Project proj;
    QSettings cfg = appSettings();
    {   // M6: tuy chon giai ma video (uu tien: dong lenh > bien moi truong > cai dat "video/hwdec")
        VideoDecodeOptions vo = VideoDecodeOptions::defaults();
        if (cli.isSet("hwdec")) vo.hwDecode = cli.value("hwdec").toLower();
        else if (qEnvironmentVariable("LUMACAST_HWDEC").isEmpty() && cfg.contains("video/hwdec")) vo.hwDecode = cfg.value("video/hwdec").toString();
        if (cli.isSet("no-gpu-yuv")) vo.gpuColor = false;
        VideoDecodeOptions::setDefaults(vo);
    }
    QString startupNotice;
    auto askRestore = [](const QFileInfo& autosave) {
        return QMessageBox::question(nullptr, "Khôi phục project",
                                     QString("Tìm thấy bản tự động lưu mới hơn:\n%1\n(%2)\n\nKhôi phục bản này?")
                                         .arg(autosave.fileName(), autosave.lastModified().toString("dd/MM/yyyy HH:mm:ss")),
                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes;
    };
    // OPS-01: khong co --project va khong co cau hinh tu dong lenh -> tu mo lai project gan nhat (neu bat); thieu file thi chay binh thuong.
    QString projectArg = cli.value("project");
    bool autoOpened = false;
    if (!cli.isSet("project")) {
        const bool explicitCfg = cli.isSet("output") || cli.isSet("media") || cli.isSet("wall") || cli.isSet("save");
        const StartupDecision dec = decideStartupProject(cfg, explicitCfg);
        startupNotice = dec.notice;
        projectArg = dec.path;
        autoOpened = !dec.path.isEmpty();
    }
    bool loaded = false;
    if (!projectArg.isEmpty()) {
        QString err;
        // Khoi phuc: neu ban autosave moi hon file project (vd mat dien giua show) thi hoi nguoi dung.
        const QString toLoad = pickProjectToLoad(projectArg, askRestore);
        if (loadProject(proj, toLoad, &err)) {
            proj.filePath = projectArg;                  // luu lai vao file goc, ke ca khi nap tu autosave
            if (cli.isSet("fullscreen"))
                for (auto& o : proj.outputs) o.fullscreen = true;
            rememberProject(cfg, projectArg);
            if (autoOpened) qInfo().noquote() << "Tự mở lại project gần nhất:" << projectArg;
            loaded = true;
        } else if (!autoOpened) {
            qCritical().noquote() << "Khong mo duoc project:" << err;
            return 2;
        } else {                                         // tu mo that bai: khong lam hong buoi chay, chay project trong va bao
            proj = Project();
            startupNotice = QString("Không mở lại được project gần nhất (%1): %2").arg(projectArg, err);
            qWarning().noquote() << startupNotice;
        }
    }
    if (!loaded) {
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
            proj.outputs = makeWall(0, cols, rows, QSize(cw, ch), 0, cli.isSet("fullscreen"), gap);
            canvas = wallCanvas(cols, rows, QSize(cw, ch), gap);
            if (!gap.isNull() && (canvas.width() > kMaxWallCanvas || canvas.height() > kMaxWallCanvas)) {
                qCritical().noquote() << QString("--gap: canvas %1x%2 vuot qua %3 diem anh").arg(canvas.width()).arg(canvas.height()).arg(kMaxWallCanvas);
                return 2;
            }
        } else {
            QStringList specs = cli.values("output");
            if (specs.isEmpty()) specs << "1920x1080";
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
    if (!cli.isSet("no-audio")) {
        QString aerr;
        engine.audio().start(proj.audioDevice, &aerr);
        if (!aerr.isEmpty()) qWarning().noquote() << "Âm thanh:" << aerr;
    }
    MainWindow window(proj, engine);
    window.show();
    if (!startupNotice.isEmpty()) { qWarning().noquote() << startupNotice; window.showStartupNotice(startupNotice); }
    const int rc = app.exec();
    qInfo() << "LumaCast thoat, ma" << rc;
    engine.stopEngine();
    if (!engine.error().isEmpty()) qCritical().noquote() << engine.error();
    shutdownLogging();
    return rc;
}
