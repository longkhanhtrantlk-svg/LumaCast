#include "Benchmark.h"
#include "core/Project.h"
#include "media/FFmpegSource.h"
#include "media/StaticSources.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QThread>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace lc {

namespace {

// Nguon tong hop: mot thread "giai ma" doi con tro khung theo nhip sourceFps (khong ton CPU) -> acquire() tren render thread chi con
// chi phi upload (+ doi YUV neu NV12). Do rieng chi phi upload/doi mau, khong lan voi giai ma.
class SyntheticSource : public Source {
public:
    SyntheticSource(QSize sz, double fps, bool yuv) : sz_(sz), yuv_(yuv) {
        const int n = 4;
        for (int k = 0; k < n; ++k) {
            if (yuv_) {
                auto img = std::make_shared<YuvImage>();
                img->w = sz.width(); img->h = sz.height();
                img->fmt.layout = YuvLayout::SemiPlanar;
                img->plane[0].resize(img->planeBytes(0));
                img->plane[1].resize(img->planeBytes(1));
                for (size_t i = 0; i < img->plane[0].size(); i += 64) {         // thua: du de co noi dung, tao nhanh
                    const size_t e = std::min(img->plane[0].size(), i + 64);
                    for (size_t j = i; j < e; ++j) img->plane[0][j] = uint8_t(16 + ((j * 7 + size_t(k) * 31) & 0xDF));
                }
                for (size_t i = 0; i < img->plane[1].size(); ++i) img->plane[1][i] = uint8_t(96 + ((i * 3 + size_t(k) * 17) & 0x3F));
                yuvFrames_.push_back(img);
            } else {
                std::vector<uint8_t> v(size_t(sz.width()) * sz.height() * 4);
                for (size_t i = 0; i < v.size(); ++i) v[i] = uint8_t(((i >> 2) * 5 + size_t(k) * 40 + (i & 3) * 60) & 0xFF);
                rgbaFrames_.push_back(std::move(v));
            }
        }
        period_ = std::chrono::duration<double>(1.0 / std::max(1.0, fps));
        thread_ = std::thread([this, n] {
            auto next = std::chrono::steady_clock::now();
            while (!stop_) {
                cur_ = (cur_ + 1) % n;
                seq_ = seq_ + 1;
                next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period_);
                std::this_thread::sleep_until(next);
            }
        });
    }
    ~SyntheticSource() override { stop_ = true; thread_.join(); }
    QString name() const override { return "synthetic"; }
    TexRef acquire(GL& gl) override {
        const uint64_t s = seq_;
        if (s != uploaded_ || (!yuvTex_.id() && !tex_.id())) {
            const int k = cur_;
            if (yuv_) yuvTex_.upload(gl, *yuvFrames_[size_t(k)]);
            else tex_.upload(gl, rgbaFrames_[size_t(k)].data(), sz_.width(), sz_.height());
            uploaded_ = s;
        }
        return yuv_ ? TexRef{yuvTex_.id(), yuvTex_.w(), yuvTex_.h(), 0} : TexRef{tex_.id(), tex_.w(), tex_.h(), 0};
    }
private:
    QSize sz_;
    bool yuv_;
    std::vector<std::shared_ptr<YuvImage>> yuvFrames_;
    std::vector<std::vector<uint8_t>> rgbaFrames_;
    std::atomic<bool> stop_{false};
    std::atomic<int> cur_{0};
    std::atomic<uint64_t> seq_{1};
    uint64_t uploaded_ = 0;
    std::chrono::duration<double> period_;
    std::thread thread_;
    GlTexture tex_;
    YuvTexture yuvTex_;
};

// Output "ngoai man hinh": ve Program vao FBO W x H nhu cua so that (cung duong drawToCurrent: scale + hieu chinh), khong swap.
class BenchSink : public OutputSink {
public:
    explicit BenchSink(OutputConfig c) : cfg_(std::move(c)) {}
    const OutputConfig& config() const override { return cfg_; }
    OutputConfig& mutableConfig() override { return cfg_; }
    void present(QOpenGLContext&, Compositor& comp, RenderTarget* src) override {
        GL& gl = comp.gl();
        if (!fbo_) {
            gl.glGenTextures(1, &tex_);
            gl.glBindTexture(GL_TEXTURE_2D, tex_);
            gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cfg_.size.width(), cfg_.size.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            gl.glGenFramebuffers(1, &fbo_);
            gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
            gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_, 0);
        }
        gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        if (src) comp.drawToCurrent(src->tex, src->w, src->h, cfg_, cfg_.size.width(), cfg_.size.height());
        ++presented;
    }
    void shutdown(QOpenGLContext&, Compositor& comp) override {
        if (fbo_) comp.gl().glDeleteFramebuffers(1, &fbo_);
        if (tex_) comp.gl().glDeleteTextures(1, &tex_);
        fbo_ = tex_ = 0;
    }
private:
    OutputConfig cfg_;
    GLuint fbo_ = 0, tex_ = 0;
};

BenchStage stageOf(std::vector<float> v) {
    BenchStage s;
    if (v.empty()) return s;
    double sum = 0;
    for (float x : v) sum += x;
    s.mean = sum / double(v.size());
    std::sort(v.begin(), v.end());
    auto pct = [&](double p) { return double(v[std::min(v.size() - 1, size_t(p * double(v.size() - 1) + 0.5))]); };
    s.p50 = pct(0.5); s.p99 = pct(0.99); s.max = double(v.back());
    return s;
}

QJsonObject stageJson(const BenchStage& s) {
    auto r = [](double v) { return std::round(v * 1000) / 1000; };
    return {{"p50", r(s.p50)}, {"p99", r(s.p99)}, {"max", r(s.max)}, {"mean", r(s.mean)}};
}

} // namespace

QJsonObject BenchResult::toJson() const {
    QJsonObject o;
    o["ok"] = ok;
    if (!ok) o["error"] = error;
    o["gpu"] = gpu;
    o["softwareRenderer"] = softwareRenderer;
    if (softwareRenderer) o["warning"] = "Bo render phan mem: so lieu KHONG dai dien cho GPU that";
    QJsonObject op;
    op["outputs"] = options.outputs;
    op["size"] = QString("%1x%2").arg(options.size.width()).arg(options.size.height());
    const QSize cv = options.canvas.isEmpty() ? options.size : options.canvas;
    op["canvas"] = QString("%1x%2").arg(cv.width()).arg(cv.height());
    op["channels"] = options.channels;
    op["source"] = options.source;
    op["sourceSize"] = QString("%1x%2").arg(options.sourceSize.width()).arg(options.sourceSize.height());
    op["sourceFps"] = options.sourceFps;
    op["layers"] = options.layers;
    op["transition"] = options.transition;
    op["seconds"] = options.seconds;
    op["fps"] = options.fps;
    op["pbo"] = options.pbo;
    op["gpuColor"] = options.gpuColor;
    op["syncStages"] = options.syncStages;
    o["options"] = op;
    o["frames"] = frames;
    o["measuredSeconds"] = std::round(measuredSeconds * 100) / 100;
    o["fps"] = std::round(fps * 100) / 100;
    o["dropped"] = double(dropped);
    o["overBudget"] = overBudget;
    o["budgetMs"] = std::round(budgetMs * 100) / 100;
    o["presented"] = double(presented);
    QJsonObject st;
    st["frame"] = stageJson(frame); st["upload"] = stageJson(upload); st["scene"] = stageJson(scene);
    st["transition"] = stageJson(transition); st["monitor"] = stageJson(monitor); st["output"] = stageJson(output);
    o["stagesMs"] = st;
    return o;
}

QString BenchResult::toText() const {
    QString t;
    auto row = [&](const char* n, const BenchStage& s) {
        t += QString("  %1 p50 %2  p99 %3  max %4  tb %5 ms\n").arg(n, -11).arg(s.p50, 7, 'f', 2).arg(s.p99, 7, 'f', 2).arg(s.max, 7, 'f', 2).arg(s.mean, 7, 'f', 2);
    };
    t += QString("GPU: %1\n").arg(gpu);
    if (softwareRenderer) t += "!! BO RENDER PHAN MEM (llvmpipe/softpipe): so lieu duoi day KHONG dai dien cho GPU that.\n";
    if (!ok) return t + "LOI: " + error + "\n";
    t += QString("Cau hinh: %1 Output %2x%3, %4 kenh, nguon %5, %6 layer, transition %7, PBO %8, doi mau %9\n")
             .arg(options.outputs).arg(options.size.width()).arg(options.size.height()).arg(options.channels).arg(options.source)
             .arg(options.layers).arg(options.transition ? "bat" : "tat").arg(options.pbo ? "bat" : "tat")
             .arg(options.gpuColor ? "GPU" : "CPU");
    t += QString("Ket qua: %1 khung / %2 s = %3 fps (muc tieu %4), roi khung %5, vuot han %6 khung (han %7 ms)\n")
             .arg(frames).arg(measuredSeconds, 0, 'f', 1).arg(fps, 0, 'f', 1).arg(options.fps).arg(dropped).arg(overBudget).arg(budgetMs, 0, 'f', 2);
    row("khung", frame); row("upload", upload); row("scene", scene); row("transition", transition); row("monitor", monitor); row("output", output);
    return t;
}

bool parseBenchOptions(const QString& spec, BenchOptions& o, QString* err) {
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    auto dims = [](const QString& v, QSize& out) {
        static const QRegularExpression re(R"(^(\d+)x(\d+)$)");
        const auto m = re.match(v);
        if (!m.hasMatch()) return false;
        out = QSize(m.captured(1).toInt(), m.captured(2).toInt());
        return out.width() > 0 && out.height() > 0;
    };
    for (const QString& kv : spec.split(',', Qt::SkipEmptyParts)) {
        const int eq = kv.indexOf('=');
        if (eq <= 0) return fail("Tuy chon benchmark khong hop le: " + kv);
        const QString k = kv.left(eq).trimmed().toLower(), v = kv.mid(eq + 1).trimmed();
        bool ok = true;
        if (k == "outputs") { o.outputs = v.toInt(&ok); ok = ok && o.outputs >= 1 && o.outputs <= 16; }
        else if (k == "size") ok = dims(v, o.size);
        else if (k == "canvas") ok = dims(v, o.canvas);
        else if (k == "srcsize") ok = dims(v, o.sourceSize);
        else if (k == "channels") { o.channels = v.toInt(&ok); ok = ok && o.channels >= 1 && o.channels <= 16; }
        else if (k == "source") { o.source = v; ok = v == "synthetic-yuv" || v == "synthetic-rgba" || v == "color" || v.startsWith("file:"); }
        else if (k == "srcfps") { o.sourceFps = v.toDouble(&ok); ok = ok && o.sourceFps > 0; }
        else if (k == "layers") { o.layers = v.toInt(&ok); ok = ok && o.layers >= 1 && o.layers <= 16; }
        else if (k == "transition") o.transition = v == "1" || v == "true";
        else if (k == "seconds") { o.seconds = v.toDouble(&ok); ok = ok && o.seconds > 0; }
        else if (k == "warmup") { o.warmup = v.toDouble(&ok); ok = ok && o.warmup >= 0; }
        else if (k == "fps") { o.fps = v.toDouble(&ok); ok = ok && o.fps > 0; }
        else if (k == "pbo") o.pbo = !(v == "0" || v == "false");
        else if (k == "gpucolor") o.gpuColor = !(v == "0" || v == "false");
        else if (k == "sync") o.syncStages = v == "1" || v == "true";
        else if (k == "hwdec") o.hwDecode = v == "1" || v == "true" || v == "auto";
        else return fail("Khoa benchmark khong biet: " + k);
        if (!ok) return fail(QString("Gia tri khong hop le: %1=%2").arg(k, v));
    }
    return true;
}

BenchResult runBenchmark(const BenchOptions& opt) {
    BenchResult r;
    r.options = opt;
    r.budgetMs = 1000.0 / opt.fps;
    const QSize canvas = opt.canvas.isEmpty() ? opt.size : opt.canvas;

    const bool oldPbo = UploadConfig::usePbo();
    UploadConfig::setUsePbo(opt.pbo);
    const VideoDecodeOptions oldDec = VideoDecodeOptions::defaults();
    VideoDecodeOptions dec = oldDec;
    dec.gpuColor = opt.gpuColor;
    dec.hwDecode = opt.hwDecode ? "auto" : "off";
    VideoDecodeOptions::setDefaults(dec);
    struct Restore { bool pbo; VideoDecodeOptions d; ~Restore() { UploadConfig::setUsePbo(pbo); VideoDecodeOptions::setDefaults(d); GlProfile::setSync(false); } } restore{oldPbo, oldDec};

    Project proj;
    proj.fps = opt.fps;
    auto makeSrc = [&]() -> std::shared_ptr<Source> {
        if (opt.source == "synthetic-yuv") return std::make_shared<SyntheticSource>(opt.sourceSize, opt.sourceFps, true);
        if (opt.source == "synthetic-rgba") return std::make_shared<SyntheticSource>(opt.sourceSize, opt.sourceFps, false);
        if (opt.source == "color") return std::make_shared<ColorSource>(QColor(30, 90, 200), "bench");
        SourceDef d;
        d.type = "video";
        d.path = QFileInfo(opt.source.mid(5)).absoluteFilePath();
        QString e;
        auto s = proj.addSource(d, &e);
        if (!s) r.error = e;
        return s;
    };
    for (int sc = 0; sc < 2; ++sc) {           // A: nguon + layer; B: nen mau (cho transition)
        auto s = std::make_shared<Scene>();
        s->name = sc == 0 ? "A" : "B";
        s->size = canvas;
        if (sc == 0) {
            for (int l = 0; l < opt.layers; ++l) {
                auto src = makeSrc();
                if (!src) { r.error = r.error.isEmpty() ? "Khong tao duoc nguon" : r.error; return r; }
                Layer ly;
                ly.source = src;
                ly.fit = LayerFit::Stretch;
                ly.opacity = opt.layers > 1 ? 0.6f : 1.0f;
                s->layers.push_back(ly);
            }
        } else {
            Layer ly;
            ly.source = std::make_shared<ColorSource>(QColor(200, 40, 40), "B");
            ly.fit = LayerFit::Stretch;
            s->layers.push_back(ly);
        }
        proj.scenes.push_back(s);
    }
    for (int c = 0; c < opt.channels; ++c) {
        auto ch = std::make_shared<Channel>();
        ch->name = QString("Channel %1").arg(c + 1);
        ch->size = canvas;
        ch->scenes = proj.scenes;
        ch->program = 0;
        ch->preview = 1;
        proj.channels.push_back(ch);
    }

    RenderEngine engine(proj);
    engine.startEngine();
    std::vector<BenchSink*> sinks;
    for (int i = 0; i < opt.outputs; ++i) {
        OutputConfig cfg;
        cfg.name = QString("Bench %1").arg(i + 1);
        cfg.channel = i % opt.channels;
        cfg.size = opt.size;
        cfg.scale = ScaleMode::Fit;
        auto s = std::make_unique<BenchSink>(cfg);
        sinks.push_back(s.get());
        if (engine.addSink(std::move(s)) < 0) { r.error = "Khong them duoc Output benchmark"; engine.stopEngine(); return r; }
    }
    engine.setProfileSync(opt.syncStages);

    auto sleepMs = [](double s) { QThread::msleep(quint32(s * 1000)); };
    auto toggleTransition = [&] {
        for (auto& ch : proj.channels) {
            Channel* c = ch.get();
            engine.post([c](double now) {
                if (c->inTransition() || c->manualActive()) return;
                TransitionSpec sp;
                sp.type = TransitionType::Fade;
                sp.duration = 0.8;
                c->startTransition(sp, now);
            });
        }
    };
    // warm-up (co the chay transition de bien dich shader)
    const auto w0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - w0).count() < opt.warmup) {
        if (opt.transition) toggleTransition();
        sleepMs(0.1);
    }
    if (!engine.error().isEmpty()) { r.error = engine.error(); engine.stopEngine(); return r; }
    r.gpu = engine.gpuName();
    r.softwareRenderer = r.gpu.contains("llvmpipe", Qt::CaseInsensitive) || r.gpu.contains("softpipe", Qt::CaseInsensitive) ||
                         r.gpu.contains("software", Qt::CaseInsensitive) || r.gpu.contains("swrast", Qt::CaseInsensitive);

    uint64_t p0 = 0;
    for (auto* s : sinks) p0 += s->presented;
    const uint64_t d0 = engine.droppedFrames();
    engine.startRecording();
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < opt.seconds) {
        if (opt.transition) toggleTransition();
        sleepMs(0.1);
    }
    r.measuredSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    auto rec = engine.takeRecording();
    r.dropped = engine.droppedFrames() - d0;
    for (auto* s : sinks) r.presented += s->presented;
    r.presented -= p0;
    r.error = engine.error();
    engine.stopEngine();

    r.frames = int(rec.size());
    std::vector<float> a, b, c, d, e, f;
    for (const auto& s : rec) {
        a.push_back(s.total); b.push_back(s.upload); c.push_back(s.scene); d.push_back(s.transition); e.push_back(s.monitor); f.push_back(s.output);
        if (s.total > float(r.budgetMs)) ++r.overBudget;
    }
    r.frame = stageOf(a); r.upload = stageOf(b); r.scene = stageOf(c); r.transition = stageOf(d); r.monitor = stageOf(e); r.output = stageOf(f);
    r.fps = r.measuredSeconds > 0 ? double(r.frames) / r.measuredSeconds : 0;
    r.ok = r.error.isEmpty() && r.frames > 0;
    return r;
}

int runBenchmarkCli(const QString& spec, const QString& jsonPath) {
    BenchOptions o;
    QString err;
    if (!parseBenchOptions(spec, o, &err)) {
        std::cerr << "Loi: " << err.toStdString() << "\n";
        return 2;
    }
    const BenchResult r = runBenchmark(o);
    std::cout << r.toText().toStdString();
    if (!jsonPath.isEmpty()) {
        QFile f(jsonPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(r.toJson()).toJson());
        else std::cerr << "Khong ghi duoc " << jsonPath.toStdString() << "\n";
    }
    return r.ok ? 0 : 1;
}

} // namespace lc
