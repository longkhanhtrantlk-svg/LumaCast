# LumaCast – Thiết kế kiến trúc phần mềm điều khiển màn hình LED (kiểu vMix)

Tên tạm: **LumaCast**. Nền tảng: C++20 + Qt 6 (Quick/QML cho UI) + OpenGL 4.1 core. Đầu ra: card HDMI/DP, mỗi output là một cửa sổ fullscreen trên một màn hình hệ điều hành (bộ gửi tín hiệu LED như Novastar/Colorlight nhận HDMI).

---

## 1. Mô hình khái niệm

| Khái niệm | Ý nghĩa |
|---|---|
| **Source (Nguồn)** | Thứ sinh ra frame: file video/ảnh/sequence, camera/capture, NDI-in, text/CG, web, màu trơn, generator plug-in. Xuất ra *texture GPU*. |
| **Layer** | Một Source đặt trong Scene, có vị trí/kích thước/xoay/độ mờ/crop/blend mode + chuỗi Effect. |
| **Scene** | Danh sách Layer, có độ phân giải thiết kế (authoring) riêng. Giống "Input/Scene" trong vMix. |
| **Channel (Kênh)** | Một đường Program/Preview độc lập: có Scene đang lên sóng (Program), Scene chuẩn bị (Preview) và bộ chuyển (Transition). Có thể có nhiều Channel chạy song song. |
| **Output (Đầu ra)** | Một màn hình LED vật lý: độ phân giải, tần số quét, xoay, vùng cắt, hiệu chỉnh màu. Mỗi Output nhận hình từ **một Channel** (hoặc một vùng của Channel, xem Mapping). |
| **Mapping** | Cách cắt/ghép Channel lên các Output: 1-1, một Channel dài chia nhiều Output (ví dụ 7680×1080 = 4 × 1920×1080), hoặc nhiều Output cùng nhân bản. |
| **Transition** | Shader nhận 2 texture (from, to) + tiến độ `t∈[0,1]` → texture kết quả. Cut, Fade, Wipe, Slide, Zoom, Luma… và plug-in. |
| **Effect** | Shader/bộ xử lý 1 texture vào → 1 texture ra, có tham số (chỉnh màu, blur, glow, chromakey, pixelate…). |
| **Plug-in** | Gói mở rộng bên thứ ba: Effect, Transition, Source generator, Output filter. |

Luồng vMix-style: chọn Scene vào **Preview** → bấm Cut/Fade/Transition → Scene sang **Program** → Program được render ra các Output.

---

## 2. Sơ đồ tổng thể

```
 ┌──────────────────────────── UI (Qt Quick / QML) ────────────────────────────┐
 │ Multiview · Scene editor · Transition bar · Output setup · Plugin manager   │
 └───────────────▲───────────────────────────────────────┬─────────────────────┘
                 │ signals / models                      │ lệnh (lock-free queue)
 ┌───────────────┴───────────────────────────────────────▼─────────────────────┐
 │                          Core / Project Model                                │
 │  Project · Scenes · Layers · Channels · Outputs · Mappings · Timeline/Cues   │
 └───────┬───────────────────┬──────────────────────┬──────────────────┬───────┘
         │                   │                      │                  │
 ┌───────▼──────┐   ┌────────▼────────┐   ┌─────────▼────────┐  ┌──────▼───────┐
 │ Media Engine │   │  Render Engine  │   │  Plugin Host     │  │ Control I/O  │
 │ FFmpeg decode│──▶│ GL, FBO graph,  │◀──│ load/validate/   │  │ HTTP/WS/OSC/ │
 │ HW accel,    │   │ texture pool,   │   │ sandbox, param   │  │ MIDI/hotkeys │
 │ image, NDI   │   │ compositor,     │   │ schema           │  └──────────────┘
 └──────────────┘   │ transitions     │   └──────────────────┘
                    └────────┬────────┘
                             │ texture Program của từng Channel
                    ┌────────▼────────┐
                    │ Output Manager  │  scale/crop/rotate · calib màu · pacing
                    └──┬─────┬─────┬──┘
                       ▼     ▼     ▼
                   Window  Window  Window   (fullscreen trên từng màn hình LED)
```

Nguyên tắc: **mọi thứ nặng ở GPU**, UI chỉ gửi lệnh, không bao giờ chạm vào đường render.

---

## 3. Render Engine

### 3.1 Đồ thị render mỗi khung hình (mỗi Channel)
1. **Cập nhật nguồn**: Media Engine đã đưa frame mới nhất vào texture của từng Source (double/triple buffer, không chờ).
2. **Render Scene → texture**: với mỗi Layer: lấy texture nguồn → chạy chuỗi Effect (ping-pong FBO) → vẽ quad với biến đổi + blend vào FBO của Scene. FBO ở độ phân giải của Scene.
3. **Transition** (nếu đang chạy): render Scene Program-cũ và Scene Preview-mới ra 2 texture, shader Transition trộn theo `t` → texture Program của Channel. Không chạy thì dùng thẳng texture Scene.
4. **Output**: mỗi Output lấy texture Channel, áp Mapping (vùng cắt, xoay), scale về đúng độ phân giải native, hiệu chỉnh màu → vẽ vào backbuffer cửa sổ của Output.

### 3.2 Đa độ phân giải
- Scene có độ phân giải thiết kế (vd 1920×1080) và Layer dùng toạ độ chuẩn hoá hoặc pixel; Output ở độ phân giải bất kỳ.
- Chính sách scale mỗi Output: *Fit / Fill / Stretch / Pixel-perfect (1:1)*. Lọc: nearest (LED pixel pitch) / bilinear / Lanczos (shader).
- Tuỳ chọn **render ở độ phân giải native của Output** (chất lượng cao nhất cho chữ/vector) thay vì scale từ Scene: engine chọn "render-at-output-res" khi Output có độ phân giải khác Scene nhiều.
- Texture pool theo (w,h,format) để tái sử dụng FBO, tránh cấp phát mỗi frame. Mipmap cho nguồn bị thu nhỏ mạnh.
- Màn hình rộng/ghép: Channel có "canvas" lớn (vd 7680×1080), Mapping cắt thành các Output con; bezel/khe hở (pixel gap) được bù.

### 3.3 Threading & đồng bộ
- **Render thread** duy nhất sở hữu GL context chính; các Output window dùng context *chia sẻ* (shared) để dùng chung texture. Khuyến nghị: một thread render tất cả Output tuần tự trong cùng nhịp (tránh lệch khung giữa các màn LED ghép).
- Mỗi cửa sổ `swapInterval=0`; engine tự điều nhịp theo **master clock** (vd 60 Hz, hoặc theo tần số quét thực của màn). Với nhiều màn khác tần số quét, cho phép *clock domain* riêng mỗi nhóm Output.
- Decode: mỗi Source một thread decode; ring buffer frame; upload qua **PBO bất đồng bộ**, hoặc zero-copy hw-interop (D3D11VA/NVDEC/VAAPI) khi có.
- Audio (nếu cần): luồng riêng, đồng bộ A/V theo PTS; có thể bỏ ở MVP vì LED thường chỉ hình.
- Lệnh từ UI/API đi qua queue lock-free, áp dụng ở đầu khung hình kế tiếp (thay đổi nguyên tử, không xé hình).

### 3.4 Độ tin cậy (quan trọng với LED sự kiện)
- Chế độ **Black / Freeze / Test pattern** tức thì cho từng Output.
- Mất nguồn → giữ khung cuối hoặc đưa ảnh fallback, không crash.
- Watchdog: render thread treo > N ms thì cảnh báo/khởi động lại engine, giữ Output hiện hình cuối.
- Plug-in lỗi không được làm sập chương trình (xem mục 5.4).
- Tự khôi phục project + tự mở lại output sau mất điện (autostart).

---

## 4. Media Engine

- **FFmpeg** (libavformat/libavcodec) decode video; HW decode khi có; hỗ trợ MP4/MOV/MKV, ProRes/H.264/H.265, **HAP** (nên có – rất nhẹ CPU cho LED), image sequence, PNG/JPG/SVG, GIF.
- Loại nguồn: File, Playlist, Image, Text/CG (QML/QPainter→texture), Màu, Camera/Capture (V4L2/DirectShow/Media Foundation), **NDI in** (tuỳ chọn), Web (QtWebEngine), Screen capture.
- Loop, in/out point, tốc độ, seek chính xác, preload nguồn của Scene Preview để chuyển không giật.
- Chuyển đổi màu YUV→RGB bằng shader (BT.601/709/2020, full/limited range).

---

## 5. Hệ thống Plug-in

### 5.1 Hai tầng plug-in
1. **Shader plug-in (khuyến nghị làm trước)** – chỉ là file GLSL + manifest JSON, theo kiểu **ISF** (Interactive Shader Format). Người dùng, thiết kế viên tự viết được, không cần compile, hot-reload khi lưu file. Phủ phần lớn Effect và Transition.
2. **Native plug-in (DLL/.so)** – C ABI ổn định, dành cho thứ cần code: Source generator (particle, visualizer, dữ liệu realtime, thời tiết/tỷ số), Effect đa pass phức tạp, Output filter.

### 5.2 Manifest (`plugin.json`)
```json
{
  "id": "com.example.ripple-wipe",
  "name": "Ripple Wipe",
  "version": "1.0.0",
  "apiVersion": 1,
  "type": "transition",
  "entry": "ripple.frag",
  "params": [
    {"name": "center",   "type": "point2d", "default": [0.5, 0.5]},
    {"name": "softness", "type": "float", "min": 0, "max": 1, "default": 0.2},
    {"name": "color",    "type": "color", "default": [1,1,1,1]}
  ]
}
```
Giao diện chỉnh tham số được **sinh tự động** từ `params` (slider, picker, toggle, dropdown, point…), tham số có thể keyframe/automation.

### 5.3 C ABI cho native plug-in (phác thảo)
```c
#define LUMACAST_API_VERSION 1

typedef struct LcHost {            // host cung cấp cho plug-in
    uint32_t api_version;
    void  (*log)(void* ctx, int level, const char* msg);
    GLuint(*acquire_texture)(void* ctx, int w, int h, int fmt);  // pool
    void  (*release_texture)(void* ctx, GLuint tex);
    void* ctx;
} LcHost;

typedef struct LcFrameInfo {
    double time_sec;       // thời gian của plug-in
    double progress;       // 0..1 (transition)
    int    out_w, out_h;
    float  dt;
} LcFrameInfo;

typedef struct LcPlugin {          // plug-in trả về
    uint32_t api_version;
    int   kind;                    // EFFECT | TRANSITION | SOURCE | OUTPUT_FILTER
    const char* (*manifest_json)(void);
    void* (*create)(const LcHost*);
    void  (*destroy)(void* inst);
    void  (*set_param)(void* inst, const char* name, const void* value);
    // render: input[0..n-1] -> trả texture kết quả trong out_tex
    int   (*render)(void* inst, const LcFrameInfo*, const GLuint* in_tex,
                    int n_in, GLuint out_tex);
} LcPlugin;

LUMACAST_EXPORT const LcPlugin* lc_get_plugin(void);  // điểm vào duy nhất
```
- Chỉ dùng **C thuần** ở biên ABI (tránh vấn đề C++ ABI giữa compiler/phiên bản).
- Có `api_version`; host từ chối plug-in khác major version.
- Plug-in render trên **context GL của host** (không tự tạo context), nhận/nhả texture qua pool.

### 5.4 An toàn & vòng đời
- Quét thư mục `plugins/`, đọc manifest, kiểm tra chữ ký/hash (tuỳ chọn), nạp bằng `QLibrary`.
- Mọi lời gọi plug-in bọc trong "khung bảo vệ": hết thời gian (frame budget), trả lỗi → tự **vô hiệu hoá plug-in đó** và bỏ qua trong khung hình (Layer vẫn hiển thị không effect).
- Tuỳ chọn nâng cao: chạy Source plug-in nặng/không tin cậy ở **process riêng** và chia sẻ texture qua shared memory/GL interop (giai đoạn sau).
- Hot-reload shader plug-in; native plug-in cần khởi động lại.

---

## 6. Hiệu ứng chuyển (Transition) – bộ có sẵn
Cut, Fade/Dissolve, Dip-to-color, Wipe (trái/phải/trên/dưới/chéo/tròn/iris), Slide/Push, Zoom, Luma wipe (ảnh xám làm mặt nạ), Blur-dissolve, Glitch, Pixelate, Cube/Flip 3D. Mỗi cái: thời lượng, easing (linear/ease-in-out/bezier), hướng, tham số riêng. Hỗ trợ **Stinger** (video có alpha chạy đè, điểm cắt cấu hình được).

Transition có thể đặt **riêng mỗi Channel** và mỗi nút chuyển nhanh (giống nút Quick Play/Transition 1–4 của vMix).

---

## 7. Cài đặt / Cấu hình

- **Output setup**: liệt kê màn hình hệ điều hành (QScreen), gán Output↔màn hình, độ phân giải & Hz, xoay 0/90/180/270, vùng cắt, offset, lật, test pattern (lưới, thanh màu, gradient, đếm pixel), nhận diện (hiện số trên màn).
- **Calibration**: brightness, contrast, gamma, nhiệt độ màu / RGB gain, LUT 3D tuỳ chọn, giới hạn độ sáng; áp ở bước cuối render.
- **Project**: lưu JSON (hoặc SQLite) có phiên bản schema + migration; media dùng đường dẫn tương đối, có "gói project" (collect files).
- **Preferences**: thư mục media/plugin, GPU chọn, FPS master, bộ đệm decode, ngôn ngữ UI (Việt/Anh), phím tắt.
- **Điều khiển ngoài**: HTTP REST + WebSocket (Companion/web remote), OSC, MIDI, phím tắt; API cho: chọn scene, chạy transition, đổi nguồn, đặt tham số.

---

## 8. UI (Qt Quick)

- **Multiview**: lưới thu nhỏ Preview/Program của từng Channel (render thumbnail từ texture, tần số thấp).
- **Scene bin**: danh sách Scene/nguồn, kéo-thả.
- **Scene editor**: canvas kéo/xoay/co giãn Layer, lớp, hiệu ứng, thuộc tính tự sinh từ param.
- **Transition bar**: T-bar + nút Cut/Fade/Auto, chọn transition + thời lượng.
- **Output panel**: trạng thái từng Output (FPS, dropped, độ trễ), Black/Freeze/Test.
- Preview trong UI dùng context chia sẻ + `QQuickFramebufferObject`/RHI, độ phân giải thấp để không ăn tài nguyên.

---

## 9. Cấu trúc mã nguồn & công cụ

```
lumacast/
 ├─ CMakeLists.txt            (CMake + vcpkg/conan)
 ├─ app/                      main, khởi động, QML entry
 ├─ ui/                       QML + view-model (C++ models)
 ├─ core/                     Project, Scene, Layer, Channel, Output, Mapping, Undo
 ├─ render/                   GLContext, TexturePool, FBOGraph, Compositor,
 │                            TransitionRunner, EffectChain, OutputWindow
 ├─ media/                    FFmpegSource, ImageSource, TextSource, NdiSource, PBO uploader
 ├─ plugin/                   Host, Loader, ManifestParser, IsfRuntime, Sandbox
 ├─ control/                  HttpServer, WsServer, Osc, Midi, Hotkeys
 ├─ sdk/                      lumacast_plugin.h (C ABI) + ví dụ plug-in
 ├─ shaders/                  GLSL nội bộ (composite, yuv2rgb, scale, calib)
 ├─ plugins_builtin/          transition/effect dạng ISF đi kèm
 └─ tests/                    unit (core, plugin ABI), render test (so ảnh golden)
```
Thư viện: Qt 6 (Core, Gui, Quick, Network, WebSockets), FFmpeg, (tuỳ chọn) NDI SDK, spdlog, nlohmann-json, GoogleTest. Build đa nền tảng Windows (ưu tiên – card capture/NDI phổ biến) + Linux.

---

## 10. Lộ trình đề xuất

| Mốc | Nội dung |
|---|---|
| **M1 – Lõi render** | Cửa sổ Output fullscreen, texture pool, nguồn ảnh/video (FFmpeg), 1 Channel, Layer cơ bản, Cut/Fade. |
| **M2 – Nhiều kênh & độ phân giải** | Nhiều Channel/Output độc lập, Mapping + scale policy, calibration, test pattern, lưu/mở project. |
| **M3 – Transition & Effect** | Bộ transition đầy đủ, effect chain, Stinger, easing, UI Preview/Program + multiview. |
| **M4 – Plug-in** | ISF shader plug-in + hot-reload, manifest/params tự sinh UI, SDK C ABI + plug-in mẫu. |
| **M5 – Vận hành** | Điều khiển ngoài (HTTP/OSC/MIDI), watchdog, autostart, log, NDI in/out, camera/capture, âm thanh theo Channel (WASAPI). |
| **M6 – Tối ưu** | HW decode zero-copy, HAP, tối ưu GPU, process-isolated plug-in, tool đo hiệu năng. |

---

## 11. Rủi ro chính & cách giảm

1. **Đồng bộ nhiều màn khác tần số quét / xé hình** → một render thread, master clock, cho phép nhóm clock; cân nhắc card GPU có Genlock/Sync nếu ghép màn lớn chuyên nghiệp.
2. **Băng thông GPU khi nhiều Output 4K** → render-at-output-res chọn lọc, texture pool, HAP/hw decode, giám sát tải GPU.
3. **Ổn định plug-in bên thứ ba** → shader plug-in là đường chính, native có budget/timeout + vô hiệu hoá tự động, process riêng ở giai đoạn sau.
4. **Chênh lệch driver GPU** → giữ GLSL 330/OpenGL 4.1 core, bộ test render so ảnh trên NVIDIA/AMD/Intel; để ngỏ chuyển sang Qt RHI/Vulkan sau.
5. **Phạm vi quá rộng** → bám MVP: M1–M3 đã đủ dùng được trong sự kiện nhỏ.

---

## 12. Quyết định đã chốt
| Hạng mục | Quyết định |
|---|---|
| Âm thanh | **Có**, bật/tắt được theo từng Channel (mục 13) |
| Output | Mục tiêu **4 Output**; độ phân giải chuẩn **1920×1080 và 3840×2160**, có tuỳ chọn scale cho vừa màn LED (mục 3.2) |
| Nền tảng | **Chỉ Windows** (D3D11VA decode, WASAPI audio, DirectShow/Media Foundation capture) |
| NDI / Camera | **Có** (NDI in/out + webcam/card capture), làm ở M5 |
| Giấy phép | Đề xuất: lõi đóng + SDK plug-in mở MIT; FFmpeg LGPL, Qt LGPL liên kết động (chờ xác nhận) |

## 13. Âm thanh
- Mỗi Channel có **Audio bus** riêng: công tắc Mute/On, volume, (tuỳ chọn) delay bù đồng bộ hình.
- Mỗi Source có audio của riêng nó (file, NDI, capture); Layer/Scene quyết định nguồn nào vào bus. Transition cũng crossfade âm thanh theo `t` của Program cũ/mới.
- Mỗi Channel chọn **thiết bị âm thanh đầu ra** (WASAPI, shared hoặc exclusive cho độ trễ thấp), vì mỗi Output LED có thể đi cùng một hệ loa khác nhau; có thể gộp nhiều Channel vào một thiết bị.
- Đồng hồ chủ là **audio clock**; hình đồng bộ theo PTS của âm thanh để tránh lệch tiếng.
- Mixer thread riêng (resample, mix, limiter), tách khỏi render thread; meter mức âm hiển thị trên UI.
- Với NDI/capture: bù trôi đồng hồ bằng resample thích ứng.

## 14. Mo rong ngoai LED
Loi (Source/Scene/Channel/Compositor) khong phu thuoc LED. Dich den la giao dien `OutputSink` (`src/render/OutputSink.h`):
hien co `OutputWindow` (man hinh LED/projector/LCD qua cua so fullscreen); sau nay them NDI out, ghi file/RTMP/SRT,
Art-Net/sACN/DMX. Tinh nang rieng tung loai man hinh (hieu chinh mau LED, edge blending + warp cho projector) di vao
module rieng, cam vao truoc buoc `present()`. Ten San pham: **LumaCast** (phu de hien tai: "LED Display Controller").

## 15. Trang thai M2
- Project JSON phien ban 1 (`core/Project.cpp`): sources (id on dinh), scenes (layers), channels, outputs (crop, calibration). Ghi nguyen tu (QSaveFile); mo file loi tra thong bao ro rang, khong crash.
- Hieu chinh mau o shader Output cuoi: `out = pow(clamp(((c-0.5)*contrast+0.5)*brightness*gain), 1/gamma)`.
- Wall = nhieu Output cung 1 Channel, moi cai crop 1 o cua canvas (`makeWall`). Chua bu khe/bezel.
- Mo hinh luong: moi sua doi tu UI di qua `RenderEngine::post()`, UI giu ban sao Layer/Output (cache) de khong doc du lieu render thread dang ghi.

## 16. Quyet dinh san pham (23/11/2026)
- Ten san pham: **LumaCast** (mo ta: "LED Display Controller"). Tai lieu "LED Control Studio Product Concept v1.0" la dac ta UI/UX tham chieu.
- Nen tang: **Windows truoc**. **macOS can nhung chua phai luc nay.** Giu OpenGL 3.3; `Compositor` + `GlHelpers` + phan upload texture cua nguon la
  ranh gioi GPU duy nhat (~600 dong) -> khi lam macOS se thay bang RHI (QRhi/Metal) o day, khong dong vao UI hay loi.
- UI: Qt Widgets (dock panel), giao dien toi. Khong dung Qt Quick.
- M3a (UI Live Control): monitor Preview/Program, Scene list, Media Pool, thanh chuyen (CUT/TAKE), BLACK ALL, FTB, Outputs (Black/Freeze/trang thai),
  Inspector (Layer, Output), menu File (Open/Save/Recent), autosave + khoi phuc, layout luu/khoi phuc, xac nhan khi thoat.
- M3b: Layers panel hoan chinh, Undo/Redo, T-bar, timeline/keyframe (Phase 3 cua tai lieu concept).

## 17. Trang thai M3b
- Undo/Redo: `QUndoStack` trong MainWindow; moi chinh sua tren Inspector la mot `LayersCmd`/`OutputCmd` (luu truoc/sau), cac chinh sua cung mot truong
  gop thanh mot buoc (mergeWith theo id). Thao tac live (CUT/TAKE/FTB/Black) KHONG nam trong undo. Nhap media chua hoan tac duoc.
- T-bar: `Channel::setManual(type, t)` (render thread); tien do truc tiep, khong ease; bi bo qua khi dang chay transition tu dong.
- AUTO: cong tac UI (khong phai trang thai engine): bat -> bam Scene = Preview + startTransition.
- Timeline/keyframe tach sang M4: can scene clock + thuoc tinh Layer theo thoi gian (track/keyframe) trong engine, khong chi la UI.
