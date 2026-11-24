#pragma once
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QImage>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace lc {

using GL = QOpenGLFunctions_3_3_Core;

struct RenderTarget {
    GLuint fbo = 0, tex = 0;
    int w = 0, h = 0;
    bool busy = false;
};

// Ho FBO: tai su dung theo (w,h) de khong cap phat moi khung hinh.
class TargetPool {
public:
    explicit TargetPool(GL& gl) : gl_(gl) {}
    void beginFrame();                       // danh dau tat ca la ranh
    RenderTarget* acquire(int w, int h);     // hop le den het khung hinh
    void clear();                            // xoa tai nguyen GL (can context hien hanh)
private:
    GL& gl_;
    std::vector<std::unique_ptr<RenderTarget>> targets_;
};

// ---- Do thoi gian tung giai doan (M6) ----
// Bo cong don (ns) THEO THREAD: cac lop upload cong vao day, Engine/Compositor doc va dat lai moi khung. Chi tinh thoi gian CPU
// phat lenh (+ memcpy vao PBO); GPU chay bat dong bo. Dat GlProfile::setSync(true) de glFinish sau moi upload (do thoi gian GPU that, cham hon).
struct GlProfile {
    static int64_t& uploadNs();                // thread_local
    static int64_t nowNs();
    static void setSync(bool on);
    static bool sync();
};

// Tuy chon upload toan cuc. PBO bat dong bo: ghi vao PBO (vong 2 cai) roi glTexSubImage2D tu PBO -> DMA chay song song voi GPU/CPU.
// Tat bang bien moi truong LUMACAST_NO_PBO=1 hoac UploadConfig::setUsePbo(false) (benchmark so sanh, hoac driver loi).
struct UploadConfig {
    static bool usePbo();
    static void setUsePbo(bool on);
};

// Vong PBO (GL_PIXEL_UNPACK_BUFFER): begin() tra con tro ghi (da orphan buffer), end() unmap va de PBO dang gan cho glTex(Sub)Image2D ke tiep.
class PboRing {
public:
    PboRing() = default;
    ~PboRing();
    PboRing(const PboRing&) = delete;
    PboRing& operator=(const PboRing&) = delete;
    void* begin(GL& gl, size_t bytes);          // nullptr neu khong map duoc (goi tiep bang con tro CPU)
    void end(GL& gl);                           // unmap; buffer van gan GL_PIXEL_UNPACK_BUFFER cho den khi goi release()
    void release(GL& gl);                       // gan lai 0
private:
    GLuint pbo_[2] = {0, 0};
    size_t cap_[2] = {0, 0};
    int cur_ = 0;
};

// Doc ngu GPU->CPU bat dong bo (anh monitor): glReadPixels vao PBO, anh tra ve la cua LAN GOI TRUOC (tre 1 lan goi) nen CPU khong cho GPU xong khung hien tai.
// Khi tat PBO (UploadConfig) thi doc dong bo va tra ngay anh vua doc.
class AsyncReadback {
public:
    AsyncReadback() = default;
    ~AsyncReadback();
    AsyncReadback(const AsyncReadback&) = delete;
    AsyncReadback& operator=(const AsyncReadback&) = delete;
    // Goi khi FBO nguon dang duoc bind doc: doc w x h RGBA8. Tra anh (goc tren-trai) cua lan goi truoc cung kich thuoc; QImage rong neu chua co.
    QImage submit(GL& gl, int w, int h);
    void release(GL& gl);
private:
    GLuint pbo_[2] = {0, 0};
    int w_[2] = {0, 0}, h_[2] = {0, 0};
    int cur_ = 0;
};

// Texture RGBA8 cho nguon; tu giai phong neu con context hien hanh.
class GlTexture {
public:
    GlTexture() = default;
    ~GlTexture();
    GlTexture(const GlTexture&) = delete;
    GlTexture& operator=(const GlTexture&) = delete;
    void upload(GL& gl, const uint8_t* rgba, int w, int h);
    GLuint id() const { return id_; }
    int w() const { return w_; }
    int h() const { return h_; }
private:
    GLuint id_ = 0;
    int w_ = 0, h_ = 0;
    PboRing pbo_;
};

// HAP (M6, SRC-05): texture nen S3TC/DXT upload thang. YCoCg DXT5 (Hap Q) duoc doi sang RGBA bang mot pass shader.
// Ho tro S3TC duoc Compositor::init() xac dinh tu context render va ghi vao day (nguon doc de quyet dinh di duong HAP truc tiep hay giai ma FFmpeg).
struct HapSupport {
    enum State { Unknown = 0, Yes = 1, No = 2 };
    static State state();
    static void set(bool s3tc);
};
enum class HapGlFormat : uint8_t { Dxt1, Dxt5, YCoCgDxt5 };
class HapTexture {
public:
    HapTexture() = default;
    ~HapTexture();
    HapTexture(const HapTexture&) = delete;
    HapTexture& operator=(const HapTexture&) = delete;
    bool upload(GL& gl, HapGlFormat f, int w, int h, const uint8_t* data, size_t bytes);
    GLuint id() const { return ycocg_ ? out_ : tex_; }
    int w() const { return w_; }
    int h() const { return h_; }
private:
    GLuint tex_ = 0, out_ = 0, fbo_ = 0;
    int w_ = 0, h_ = 0;
    bool ycocg_ = false;
    HapGlFormat fmt_ = HapGlFormat::Dxt1;
    PboRing pbo_;
};

// Dung phang hoac nua phang cua anh YUV (khong phu thuoc FFmpeg).
enum class YuvLayout : uint8_t { Planar, SemiPlanar };       // Planar: Y,U,V rieng; SemiPlanar: Y + (U,V xen ke) - NV12/P010
enum class YuvMatrix : uint8_t { BT601, BT709, BT2020 };
struct YuvFormat {
    YuvLayout layout = YuvLayout::Planar;
    int bits = 8;                  // do sau mau that: 8 | 10 (>8 luu trong 16 bit little-endian)
    bool msb = false;              // true: gia tri nam o bit cao cua 16 bit (P010); false: bit thap (yuv420p10le)
    int chromaShiftX = 1, chromaShiftY = 1;      // 4:2:0 = (1,1); 4:2:2 = (1,0); 4:4:4 = (0,0)
    YuvMatrix matrix = YuvMatrix::BT709;
    bool fullRange = false;
    bool chromaCenter = false;     // true: mau sac nam GIUA cum diem anh (JPEG/MPEG-1); false: lech trai (MPEG-2/H.264, phan lon video) - chi anh huong 4:2:0/4:2:2
    bool operator==(const YuvFormat& o) const {
        return layout == o.layout && bits == o.bits && msb == o.msb && chromaShiftX == o.chromaShiftX &&
               chromaShiftY == o.chromaShiftY && matrix == o.matrix && fullRange == o.fullRange && chromaCenter == o.chromaCenter;
    }
    int bytesPerSample() const { return bits > 8 ? 2 : 1; }
};
// Anh YUV so huu bo nho, hang day dac (khong padding). Plane 1 (SemiPlanar) chua UV xen ke.
struct YuvImage {
    int w = 0, h = 0;
    YuvFormat fmt;
    std::vector<uint8_t> plane[3];
    int chromaW() const { return (w + (1 << fmt.chromaShiftX) - 1) >> fmt.chromaShiftX; }
    int chromaH() const { return (h + (1 << fmt.chromaShiftY) - 1) >> fmt.chromaShiftY; }
    size_t planeBytes(int i) const;            // kich thuoc ky vong cua plane i (0 neu khong ton tai)
    bool valid() const;
};

// Texture RGBA8 do GPU tao tu anh YUV: upload plane Y/U/V (hoac Y/UV) len GPU roi mot pass shader doi mau -> texture RGBA (goc tren-trai nhu GlTexture).
// Thay cho sws_scale RGBA tren CPU (SRC-04). Chi goi tren thread co context GL; chung trang thai GL: tat blend, doi FBO/program (Compositor dat lai moi Layer).
class YuvTexture {
public:
    YuvTexture();
    ~YuvTexture();
    YuvTexture(const YuvTexture&) = delete;
    YuvTexture& operator=(const YuvTexture&) = delete;
    bool upload(GL& gl, const YuvImage& img);      // false neu khong bien dich duoc shader / anh khong hop le
    GLuint id() const { return out_; }
    GLuint fbo() const { return fbo_; }            // FBO cua texture dau ra (doc lai pixel khi kiem thu)
    int w() const { return w_; }
    int h() const { return h_; }
    // He so doi mau (kiem thu): rgb tu (y,cb,cr) da chuan hoa theo cung cong thuc voi shader.
    static void convertPixel(const YuvFormat& f, double y, double u, double v, double rgb[3]);
    struct Prog;                                   // chuong trinh shader dung chung theo context (chi noi bo)
private:
    void ensureTargets(GL& gl, const YuvImage& img);
    void freeAll(GL& gl);
    GLuint planeTex_[3] = {0, 0, 0};
    int planeW_[3] = {0, 0, 0}, planeH_[3] = {0, 0, 0};
    GLuint out_ = 0, fbo_ = 0;
    int w_ = 0, h_ = 0;
    YuvFormat fmt_;
    bool haveFmt_ = false;
    PboRing pbo_;
    std::shared_ptr<Prog> prog_;
};

} // namespace lc
