// M6: YUV->RGB bang shader, PBO, giai ma phan cung (duong du phong), so lieu tung giai doan, benchmark.
#include "core/Channel.h"
#include "core/Project.h"
#include "media/FFmpegSource.h"
#include "media/StaticSources.h"
#include "render/Benchmark.h"
#include "render/Compositor.h"
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>

using namespace lc;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << std::string(msg) << "  (" #cond ")\n"; } else std::cout << "ok:   " << std::string(msg) << "\n"; } while (0)

static QImage readAll(GL& gl, GLuint fbo, int w, int h) {         // hang 0 cua anh = hang 0 cua texture (= hang dau cua nguon)
    QImage img(w, h, QImage::Format_RGBA8888);
    gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
    return img;
}

static QImage renderLayer(Compositor& comp, TargetPool& pool, Source& src, QSize sz) {
    pool.beginFrame();
    auto sc = std::make_shared<Scene>();
    sc->size = sz;
    sc->layers.push_back({std::shared_ptr<Source>(&src, [](Source*) {}), {0, 0, 1, 1}, 1.0f, LayerFit::Stretch});
    RenderTarget* t = pool.acquire(sz.width(), sz.height());
    comp.renderScene(*sc, *t);
    return readAll(comp.gl(), t->fbo, sz.width(), sz.height()).mirrored(false, true);      // FBO goc duoi-trai -> anh goc tren-trai
}

// Nguon thu: phat mot anh YUV co san qua YuvTexture (kiem chung duong Layer -> Scene dung chieu).
class YuvTestSource : public Source {
public:
    explicit YuvTestSource(YuvImage img) : img_(std::move(img)) {}
    QString name() const override { return "yuvtest"; }
    TexRef acquire(GL& gl) override {
        if (!done_) { ok_ = tex_.upload(gl, img_); done_ = true; }
        return ok_ ? TexRef{tex_.id(), tex_.w(), tex_.h(), 0} : TexRef{};
    }
    YuvTexture tex_;
private:
    YuvImage img_;
    bool done_ = false, ok_ = false;
};

struct Diff { double mean = 0, p99 = 0, frac6 = 0; int max = 0; };
static Diff compare(const QImage& a, const QImage& b) {
    Diff d;
    if (a.size() != b.size()) { d.mean = 1e9; return d; }
    std::vector<int> v;
    double sum = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
            const int e = std::max({std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)), std::abs(qBlue(p) - qBlue(q))});
            v.push_back(e);
            sum += e;
            d.max = std::max(d.max, e);
        }
    std::sort(v.begin(), v.end());
    d.mean = sum / double(v.size());
    d.p99 = v[size_t(0.99 * double(v.size() - 1))];
    d.frac6 = double(std::count_if(v.begin(), v.end(), [](int e) { return e > 6; })) / double(v.size());
    return d;
}

// Anh YUV tong hop: luoi 4x3 khoi 16x16, moi khoi mot bo (Y,U,V) khac nhau (U,V hang so trong khoi -> lay mau sac xuong khong bi tron)
static YuvImage makeBlocks(int w, int h, YuvFormat f, std::vector<std::array<int, 3>>& vals) {
    YuvImage img;
    img.w = w; img.h = h; img.fmt = f;
    const int n = f.layout == YuvLayout::SemiPlanar ? 2 : 3;
    for (int i = 0; i < n; ++i) img.plane[i].assign(img.planeBytes(i), 0);
    const int bps = f.bytesPerSample(), shift = f.bits - 8;
    std::mt19937 rng(7);
    const int bw = 16;
    const int cols = (w + bw - 1) / bw, rows = (h + bw - 1) / bw;
    vals.clear();
    for (int i = 0; i < cols * rows; ++i) {
        const int ylo = f.fullRange ? 8 : 24, yhi = f.fullRange ? 247 : 230;
        vals.push_back({int((ylo + int(rng() % (yhi - ylo))) << shift), int((40 + int(rng() % 180)) << shift), int((40 + int(rng() % 180)) << shift)});
    }
    auto put = [&](int plane, size_t idx, int v) {
        if (f.msb) v <<= (16 - f.bits);
        if (bps == 1) img.plane[plane][idx] = uint8_t(v);
        else { img.plane[plane][idx * 2] = uint8_t(v & 255); img.plane[plane][idx * 2 + 1] = uint8_t(v >> 8); }
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const auto& v = vals[size_t(y / bw * cols + x / bw)];
            put(0, size_t(y) * w + x, v[0]);
            if ((x & ((1 << f.chromaShiftX) - 1)) == 0 && (y & ((1 << f.chromaShiftY) - 1)) == 0) {
                const size_t ci = size_t(y >> f.chromaShiftY) * img.chromaW() + (x >> f.chromaShiftX);
                if (f.layout == YuvLayout::SemiPlanar) { put(1, ci * 2, v[1]); put(1, ci * 2 + 1, v[2]); }
                else { put(1, ci, v[1]); put(2, ci, v[2]); }
            }
        }
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
    std::cout << "GL: " << gl.glGetString(GL_RENDERER) << "\n";

    // ===== 1. Shader YUV: moi dinh dang/ma tran/khoang, doi chieu CPU tung khoi =====
    {
        struct Case { const char* name; YuvLayout lay; int bits; bool msb; int sx, sy; YuvMatrix m; bool full; int w, h; };
        const Case cases[] = {
            {"yuv420p 709 gioi han", YuvLayout::Planar, 8, false, 1, 1, YuvMatrix::BT709, false, 64, 48},
            {"yuv420p 601 gioi han", YuvLayout::Planar, 8, false, 1, 1, YuvMatrix::BT601, false, 64, 48},
            {"yuv420p 2020 gioi han", YuvLayout::Planar, 8, false, 1, 1, YuvMatrix::BT2020, false, 64, 48},
            {"yuvj420p 601 day du", YuvLayout::Planar, 8, false, 1, 1, YuvMatrix::BT601, true, 64, 48},
            {"yuv422p 709", YuvLayout::Planar, 8, false, 1, 0, YuvMatrix::BT709, false, 64, 48},
            {"yuv444p 709", YuvLayout::Planar, 8, false, 0, 0, YuvMatrix::BT709, false, 64, 48},
            {"nv12 709", YuvLayout::SemiPlanar, 8, false, 1, 1, YuvMatrix::BT709, false, 64, 48},
            {"yuv420p10le 709", YuvLayout::Planar, 10, false, 1, 1, YuvMatrix::BT709, false, 64, 48},
            {"yuv444p10le 601 day du", YuvLayout::Planar, 10, false, 0, 0, YuvMatrix::BT601, true, 64, 48},
            {"p010 709 (bit cao)", YuvLayout::SemiPlanar, 10, true, 1, 1, YuvMatrix::BT709, false, 64, 48},
            {"yuv420p le kich thuoc (50x34)", YuvLayout::Planar, 8, false, 1, 1, YuvMatrix::BT709, false, 50, 34},
            {"nv12 le kich thuoc (37x21)", YuvLayout::SemiPlanar, 8, false, 1, 1, YuvMatrix::BT601, false, 37, 21},
        };
        for (const Case& c : cases) {
            YuvFormat f;
            f.layout = c.lay; f.bits = c.bits; f.msb = c.msb; f.chromaShiftX = c.sx; f.chromaShiftY = c.sy; f.matrix = c.m; f.fullRange = c.full;
            std::vector<std::array<int, 3>> vals;
            YuvImage img = makeBlocks(c.w, c.h, f, vals);
            YuvTexture yt;
            const bool up = yt.upload(gl, img);
            QImage out = up ? readAll(gl, yt.fbo(), c.w, c.h) : QImage();
            int bad = 0, worst = 0, checked = 0;
            const int cols = (c.w + 15) / 16;
            for (int by = 0; by < (c.h + 15) / 16 && up; ++by)
                for (int bx = 0; bx < cols; ++bx) {
                    // diem giua khoi (tranh bien khoi, noi chroma co the tron o hang/cot le)
                    const int x = std::min(c.w - 1, bx * 16 + 8), y = std::min(c.h - 1, by * 16 + 8);
                    if (x < bx * 16 + 3 || y < by * 16 + 3) continue;           // khoi bi cat qua nho
                    const auto& v = vals[size_t(by * cols + bx)];
                    double rgb[3];
                    YuvTexture::convertPixel(f, v[0], v[1], v[2], rgb);
                    const QRgb p = out.pixel(x, y);
                    const int e = std::max({std::abs(qRed(p) - int(std::lround(rgb[0] * 255))), std::abs(qGreen(p) - int(std::lround(rgb[1] * 255))),
                                            std::abs(qBlue(p) - int(std::lround(rgb[2] * 255)))});
                    worst = std::max(worst, e);
                    ++checked;
                    if (e > 2) ++bad;
                }
            CHECK(up && checked > 0 && bad == 0, std::string("Shader YUV: ") + c.name + " khop cong thuc CPU (sai so lon nhat " + std::to_string(worst) + "/255, " + std::to_string(checked) + " khoi)");
        }
        // Mau chuan: Y=16,U=V=128 (gioi han) = den; Y=235 = trang; 709 do thuan
        YuvFormat f;
        std::vector<std::array<int, 3>> v;
        double rgb[3];
        YuvTexture::convertPixel(f, 16, 128, 128, rgb);
        CHECK(rgb[0] < 0.01 && rgb[1] < 0.01 && rgb[2] < 0.01, "convertPixel: Y=16 gioi han = den");
        YuvTexture::convertPixel(f, 235, 128, 128, rgb);
        CHECK(rgb[0] > 0.99 && rgb[1] > 0.99 && rgb[2] > 0.99, "convertPixel: Y=235 gioi han = trang");
        YuvTexture::convertPixel(f, 63, 102, 240, rgb);       // do BT.709 gioi han (255,0,0)
        CHECK(rgb[0] > 0.97 && rgb[1] < 0.03 && rgb[2] < 0.03, "convertPixel: BT.709 do thuan");

        YuvImage bad;
        bad.w = 8; bad.h = 8;
        YuvTexture yt;
        CHECK(!bad.valid() && !yt.upload(gl, bad), "Anh YUV sai kich thuoc plane bi tu choi, khong crash");
    }

    // ===== 2. Dung chieu qua Layer/Scene (hang tren o tren) =====
    {
        YuvFormat f;                                                   // 420, 709
        YuvImage img;
        img.w = 32; img.h = 32; img.fmt = f;
        img.plane[0].assign(img.planeBytes(0), 0); img.plane[1].assign(img.planeBytes(1), 128); img.plane[2].assign(img.planeBytes(2), 128);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) img.plane[0][size_t(y) * 32 + x] = uint8_t(y < 16 ? 235 : 16);      // nua tren trang, nua duoi den
        YuvTestSource src(img);
        const QImage out = renderLayer(comp, pool, src, {32, 32});
        CHECK(qRed(out.pixel(16, 4)) > 240 && qRed(out.pixel(16, 28)) < 15, "Layer YUV: nua tren trang o TREN, nua duoi den o DUOI (khong lat doc)");
    }

    // ===== 3. PBO = truc tiep; vong PBO an toan khi nhieu khung lien tiep =====
    {
        auto frame = [](int k, int w, int h) {
            std::vector<uint8_t> v(size_t(w) * h * 4);
            for (size_t i = 0; i < v.size(); ++i) v[i] = uint8_t((i * 31 + size_t(k) * 57) & 255);
            return v;
        };
        const int w = 37, h = 23;                                       // le de thu pack-alignment
        bool allPbo = true, allDirect = true;
        for (int mode = 0; mode < 2; ++mode) {
            UploadConfig::setUsePbo(mode == 0);
            GlTexture tex;
            for (int k = 0; k < 6; ++k) {
                auto fr = frame(k, w, h);
                tex.upload(gl, fr.data(), w, h);
                RenderTarget* t = pool.acquire(w, h);
                pool.beginFrame();
                gl.glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
                // doc truc tiep texture qua FBO tam
                GLuint fbo = 0;
                gl.glGenFramebuffers(1, &fbo);
                gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo);
                gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex.id(), 0);
                QImage back = readAll(gl, fbo, w, h);
                gl.glDeleteFramebuffers(1, &fbo);
                const bool same = std::memcmp(back.constBits(), fr.data(), fr.size()) == 0;
                (mode == 0 ? allPbo : allDirect) = (mode == 0 ? allPbo : allDirect) && same;
            }
        }
        UploadConfig::setUsePbo(true);
        CHECK(allPbo, "Upload qua PBO (vong 2 buffer, 6 khung lien tiep, kich thuoc le): pixel dung tung khung");
        CHECK(allDirect, "Upload truc tiep (khong PBO): pixel dung");
        // doi kich thuoc giua chung
        GlTexture tex;
        auto a = frame(1, 16, 16), b = frame(2, 24, 10);
        tex.upload(gl, a.data(), 16, 16);
        tex.upload(gl, b.data(), 24, 10);
        CHECK(tex.w() == 24 && tex.h() == 10, "GlTexture: doi kich thuoc giua chung (cap lai storage) qua PBO");
        CHECK(GlProfile::uploadNs() > 0, "GlProfile: upload cong don thoi gian");
    }

    // ===== 4. Video that: duong GPU (shader) so voi duong CPU (sws_scale) =====
    QTemporaryDir dir;
    auto sh = [&](const QString& cmd) { return std::system(cmd.toLocal8Bit().constData()); };
    const bool haveFfmpeg = sh("ffmpeg -version >/dev/null 2>&1") == 0;
    if (!haveFfmpeg) std::cout << "skip: khong co ffmpeg de tao video thu\n";
    auto firstFrame = [&](FFmpegSource& v) {
        TexRef tex;
        for (int i = 0; i < 400 && !tex.id; ++i) { tex = v.acquire(gl); if (!tex.id) QThread::msleep(10); }
        return tex;
    };
    auto openBoth = [&](const QString& file, QImage& gpuImg, QImage& cpuImg, QString* info, bool* gpuFlag) {
        VideoDecodeOptions og; og.hwDecode = "off"; og.gpuColor = true;
        VideoDecodeOptions oc = og; oc.gpuColor = false;
        FFmpegSource g(file, true, og), c(file, true, oc);
        const TexRef tg = firstFrame(g), tc = firstFrame(c);
        if (!tg.id || !tc.id) return false;
        // dua ca hai ve cung khung: dung khung dau (tua ve 0) - ca hai deu o khung dau sau khi khoi dong
        gpuImg = renderLayer(comp, pool, g, {tg.w, tg.h});
        cpuImg = renderLayer(comp, pool, c, {tc.w, tc.h});
        if (info) *info = g.decodeInfo();
        if (gpuFlag) *gpuFlag = g.usesGpuColor() && !c.usesGpuColor();
        return true;
    };
    if (haveFfmpeg) {
        // Noi dung = smptebars (vung phang lon): hai duong CHI khac nhau o ranh gioi mau (sws_scale lap lai mau sac, GPU noi suy) -> do ty le diem lech > 6.
        struct V { const char* name; QString enc; double meanMax; double fracMax; };
        const V vids[] = {
            {"yuv420p mpeg4, gan 709", "-pix_fmt yuv420p -colorspace bt709 -color_primaries bt709 -color_trc bt709 -c:v mpeg4 -q:v 2", 4.5, 0.05},
            {"yuv420p mpeg4, gan 601", "-pix_fmt yuv420p -colorspace smpte170m -color_primaries smpte170m -color_trc smpte170m -c:v mpeg4 -q:v 2", 4.5, 0.05},
            {"yuv420p mpeg4, khong gan (HD -> 709)", "-pix_fmt yuv420p -c:v mpeg4 -q:v 2", 4.5, 0.05},
            {"yuvj420p mpeg4 (day du)", "-pix_fmt yuvj420p -c:v mpeg4 -q:v 2", 4.5, 0.05},
            {"yuv420p10le ffv1 (10 bit)", "-pix_fmt yuv420p10le -colorspace bt709 -c:v ffv1", 4.5, 0.05},
            {"yuv444p ffv1", "-pix_fmt yuv444p -colorspace bt709 -c:v ffv1", 0.5, 0.001},
            {"yuv422p ffv1", "-pix_fmt yuv422p -colorspace bt709 -c:v ffv1", 4.5, 0.05},
        };
        for (const V& v : vids) {
            const QString file = dir.filePath(QString("v_%1.mkv").arg(&v - vids));
            const int rc = sh(QString("ffmpeg -y -loglevel error -f lavfi -i smptebars=s=320x240:r=25:d=1 -frames:v 3 %1 \"%2\"").arg(v.enc, file));
            if (rc != 0) { std::cout << "skip: ffmpeg khong ma hoa duoc " << v.name << "\n"; continue; }
            QImage gi, ci, ref;
            QString info;
            bool flags = false;
            if (!openBoth(file, gi, ci, &info, &flags)) { CHECK(false, std::string("Mo duoc ") + v.name); continue; }
            const Diff d = compare(gi, ci);
            std::cout << "      " << v.name << ": " << info.toStdString() << " | khac GPU/CPU: tb " << d.mean << ", ty le >6: " << d.frac6 << ", max " << d.max << "\n";
            CHECK(flags && info.contains("shader"), std::string(v.name) + ": GPU dung shader, CPU dung sws_scale");
            CHECK(d.mean <= v.meanMax && d.frac6 <= v.fracMax, std::string(v.name) + ": shader khop sws_scale (tb " + std::to_string(d.mean).substr(0, 4) + ", ty le lech>6 " + std::to_string(d.frac6).substr(0, 5) + ")");
        }
        // Mau thuan 709: ground truth tu chinh ffmpeg
        {
            const QString file = dir.filePath("solid.mkv");
            sh(QString("ffmpeg -y -loglevel error -f lavfi -i color=c=0x3366cc:s=64x64:r=25:d=1 -frames:v 2 -vf scale=out_color_matrix=bt709:out_range=tv,format=yuv420p -colorspace bt709 "
                       "-color_primaries bt709 -color_trc bt709 -c:v mpeg4 -q:v 1 \"%1\"").arg(file));
            const QString png = dir.filePath("solid.png");
            sh(QString("ffmpeg -y -loglevel error -i \"%1\" -frames:v 1 -vf scale=in_color_matrix=bt709:out_color_matrix=bt709:flags=accurate_rnd+full_chroma_int \"%2\"").arg(file, png));
            QImage gi, ci;
            if (openBoth(file, gi, ci, nullptr, nullptr)) {
                const QRgb p = gi.pixel(32, 32);
                CHECK(std::abs(qRed(p) - 0x33) <= 6 && std::abs(qGreen(p) - 0x66) <= 6 && std::abs(qBlue(p) - 0xcc) <= 6,
                      "Mau thuan #3366cc (BT.709): shader GPU ra dung mau goc (" + std::to_string(qRed(p)) + "," + std::to_string(qGreen(p)) + "," + std::to_string(qBlue(p)) + ")");
                const QRgb q = ci.pixel(32, 32);
                CHECK(std::abs(qRed(q) - 0x33) <= 6 && std::abs(qGreen(q) - 0x66) <= 6 && std::abs(qBlue(q) - 0xcc) <= 6,
                      "Mau thuan #3366cc (BT.709): duong CPU du phong cung dung mau (da dat ma tran)");
            }
        }

        // snapshot (UI) cua khung YUV
        {
            const QString file = dir.filePath("red.mp4");
            sh(QString("ffmpeg -y -loglevel error -f lavfi -i color=c=red:s=320x240:d=1:r=25 -pix_fmt yuv420p -c:v mpeg4 \"%1\"").arg(file));
            VideoDecodeOptions o; o.hwDecode = "off";
            FFmpegSource v(file, true, o);
            firstFrame(v);
            const QImage s = v.snapshot(80);
            CHECK(!s.isNull() && s.width() == 80 && qRed(s.pixel(40, 30)) > 230 && qGreen(s.pixel(40, 30)) < 25, "snapshot() cua khung YUV: do, dung kich thuoc");
        }

        // ===== 5. Giai ma phan cung: khong co thiet bi -> tu ve phan mem, van chay =====
        {
            const QString file = dir.filePath("hw.mp4");
            sh(QString("ffmpeg -y -loglevel error -f lavfi -i testsrc2=s=320x240:r=25:d=2 -pix_fmt yuv420p -c:v mpeg4 \"%1\"").arg(file));
            for (const char* mode : {"d3d11va", "khong_co_thiet_bi_nay", "off", "auto"}) {
                VideoDecodeOptions o; o.hwDecode = mode;
                FFmpegSource v(file, true, o);
                const TexRef t = firstFrame(v);
                CHECK(t.id && v.error().isEmpty() && t.w == 320, std::string("hwdec=") + mode + ": van ra hinh");
                if (std::string(mode) != "auto" && std::string(mode) != "off")
                    CHECK(!v.usesHardwareDecode() && v.decodeInfo().contains("phần mềm"), std::string("hwdec=") + mode + ": khong dung duoc -> giai ma phan mem (" + v.decodeInfo().toStdString() + ")");
            }
            // chuyen canh/restart/lap giua chung van dung voi duong GPU
            VideoDecodeOptions o; o.hwDecode = "off";
            FFmpegSource v(file, true, o);
            firstFrame(v);
            v.restart();
            const TexRef t2 = firstFrame(v);
            CHECK(t2.id && v.error().isEmpty(), "restart() voi duong YUV GPU: co khung moi");
            QThread::msleep(2300);
            CHECK(v.acquire(gl).id != 0 && v.error().isEmpty(), "Lap lai file (>2s) voi duong YUV GPU van chay");
        }
    }

    // ===== 6. Tuy chon mac dinh =====
    {
        setenv("LUMACAST_HWDEC", "off", 1);
        setenv("LUMACAST_GPU_YUV", "0", 1);
        const VideoDecodeOptions e = VideoDecodeOptions::fromEnvironment();
        CHECK(e.hwDecode == "off" && !e.gpuColor, "VideoDecodeOptions::fromEnvironment doc LUMACAST_HWDEC / LUMACAST_GPU_YUV");
        unsetenv("LUMACAST_HWDEC");
        unsetenv("LUMACAST_GPU_YUV");
        const VideoDecodeOptions d = VideoDecodeOptions::fromEnvironment();
        CHECK(d.hwDecode == "auto" && d.gpuColor, "Mac dinh: hwdec=auto, doi mau tren GPU");
    }

    // ===== 7. Benchmark + so lieu tung giai doan =====
    {
        BenchOptions o;
        QString err;
        CHECK(parseBenchOptions("outputs=3,size=640x360,source=synthetic-rgba,seconds=1.5,warmup=0.3,layers=2,transition=1,pbo=0,sync=1,fps=30,channels=2", o, &err) &&
                  o.outputs == 3 && o.size == QSize(640, 360) && o.source == "synthetic-rgba" && o.layers == 2 && o.transition && !o.pbo && o.syncStages &&
                  o.fps == 30 && o.channels == 2,
              "parseBenchOptions: doc du khoa");
        BenchOptions bad;
        CHECK(!parseBenchOptions("outputs=0", bad, &err) && !parseBenchOptions("size=abc", bad, &err) && !parseBenchOptions("source=lam_gi", bad, &err) &&
                  !parseBenchOptions("khong_co=1", bad, &err) && !parseBenchOptions("outputs", bad, &err),
              "parseBenchOptions: tu choi gia tri/khoa sai");

        BenchOptions r1;
        r1.outputs = 2; r1.size = {640, 360}; r1.sourceSize = {640, 360}; r1.source = "synthetic-yuv"; r1.seconds = 1.5; r1.warmup = 0.5;
        r1.fps = 30; r1.transition = true; r1.channels = 2;
        const BenchResult r = runBenchmark(r1);
        std::cout << r.toText().toStdString();
        CHECK(r.ok && r.frames > 10 && r.presented >= uint64_t(r.frames), "runBenchmark: chay duoc, moi khung dua ra moi Output");
        CHECK(r.frame.p50 > 0 && r.frame.p99 >= r.frame.p50 && r.frame.max >= r.frame.p99, "runBenchmark: p50 <= p99 <= max");
        CHECK(r.upload.p50 > 0 && r.scene.p50 > 0 && r.output.p50 > 0 && r.monitor.max > 0, "runBenchmark: do duoc upload/scene/output/monitor");
        CHECK(r.transition.max > 0, "runBenchmark: co Transition thi do duoc giai doan transition");
        CHECK(r.frame.mean + 0.5 >= r.upload.mean + r.scene.mean + r.transition.mean + r.output.mean, "Cac giai doan cong lai khong vuot qua ca khung (+0,5 ms sai so)");
        const QJsonObject j = r.toJson();
        CHECK(j["ok"].toBool() && j["stagesMs"].toObject()["upload"].toObject().contains("p99") && j["options"].toObject()["outputs"].toInt() == 2 &&
                  j.contains("overBudget") && j.contains("dropped") && j.contains("gpu"),
              "BenchResult::toJson co du cac truong (stagesMs, options, dropped, overBudget, gpu)");
        CHECK(!QJsonDocument(j).toJson().isEmpty(), "BenchResult JSON ghi duoc");
        CHECK(!r.softwareRenderer || j.contains("warning"), "llvmpipe/software: JSON co canh bao khong dai dien");
        BenchOptions r2 = r1;
        r2.source = "file:/khong/co/file.mp4";
        const BenchResult rb = runBenchmark(r2);
        CHECK(!rb.ok && !rb.error.isEmpty(), "runBenchmark: file nguon khong co -> bao loi, khong crash");
    }

    pool.clear();
    comp.shutdown();
    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
