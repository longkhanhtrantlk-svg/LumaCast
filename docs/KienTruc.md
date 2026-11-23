# LumaCast – Thiết kế kiến trúc phần mềm điều khiển màn hình LED (kiểu vMix)

Tên sản phẩm: **LumaCast** (phụ đề: LED Display Controller). Nền tảng: C++20 + Qt 6 (Widgets cho UI) + OpenGL 3.3 core, Windows trước. Đầu ra: card HDMI/DP, mỗi output là một cửa sổ fullscreen trên một màn hình hệ điều hành (bộ gửi tín hiệu LED như Novastar/Colorlight nhận HDMI).

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
 ┌──────────────────────────── UI (Qt Widgets)   ────────────────────────────┐
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

## 8. Giao diện (Qt Widgets) — đã triển khai

Cửa sổ chính (`MainWindow`) dạng dock, giao diện tối; bố cục lưu/khôi phục bằng `QSettings`.
- **Trung tâm:** monitor Preview (viền xanh lá) và Program (viền đỏ, nhãn ON AIR); thanh chuyển: CUT, TAKE, AUTO, kiểu và thời lượng transition, T-bar, BLACK ALL, FTB.
- **Dock:** Scenes (đánh dấu PGM/PVW), Media Pool (ảnh thu nhỏ, nhập media), Outputs (đèn trạng thái, FPS thật, Đen/Dừng từng Output), Inspector (tab Scene/Layer, tab Output).
- **Menu:** File (mở, lưu, lưu thành, gần đây, nhập media), Edit (hoàn tác/làm lại), View (bật/tắt dock, đặt lại bố cục), Help.
- **Mô hình luồng:** UI chạy trên thread chính, render trên một thread riêng. Mọi thao tác gửi qua `RenderEngine::post()` (hàng đợi có khoá, thực thi ở đầu khung kế tiếp).
  UI giữ **bản sao (cache)** Layer/Output/tên Scene để không đọc dữ liệu render thread đang ghi; sau thay đổi cấu trúc (thêm Scene/nguồn) engine báo UI qua tín hiệu xếp hàng.
- **Monitor:** engine render ảnh thu nhỏ (tối đa 480 px rộng) của Program và Preview mỗi ~4 khung, đọc về CPU, UI lấy bản sao có khoá.
- **Chưa có (xem SPEC):** multiview nhiều Channel dạng lưới, Operator Mode, timeline, kéo-thả Output trên canvas, wizard tạo project.

---

## 9. Cấu trúc mã nguồn và công cụ

```
lumacast/
 ├─ CMakeLists.txt        CMake; FFmpeg qua pkg-config (Linux) hoặc vcpkg (Windows)
 ├─ docs/                 SPEC, PLAN, KienTruc, PROJECT-FORMAT, TEST-PLAN, TEST-WINDOWS
 ├─ src/
 │   ├─ core/             Types, Scene/Layer, Channel (Program/Preview/transition/T-bar/FTB), Project (JSON, wall)
 │   ├─ render/           Compositor (toàn bộ GL), GlHelpers (FBO/texture), Engine (thread render, monitor),
 │   │                    OutputSink (giao diện đích), OutputWindow (cửa sổ LED)
 │   ├─ media/            Source (giao diện), StaticSources (ảnh/màu/test pattern), FFmpegSource (video)
 │   └─ app/              main (CLI), MainWindow, InspectorPanel (Layer/Output + undo), MonitorWidget, Theme
 └─ tests/                test_render.cpp (lõi/engine), test_ui.cpp (giao diện)
```
**Dự kiến thêm:** `plugin/` (Host, Loader, ManifestParser), `sdk/` (header C ABI + plug-in mẫu), `control/` (HTTP/WS/OSC/MIDI),
`audio/` (mixer, WASAPI), `shaders/` (GLSL tách khỏi mã).

**Thư viện đang dùng:** Qt 6 (Core, Gui, Widgets, OpenGL), FFmpeg (avformat, avcodec, swscale, avutil). **Dự kiến:** NDI SDK, thư viện âm thanh.
**Build:** C++20, CMake ≥ 3.21, Ninja; Windows: MSVC 2022 + vcpkg (FFmpeg) + Qt 6.x (đã dùng 6.4 và 6.8.3).

---

## 10. Lộ trình
Xem **`PLAN.md`** (mốc M3c, M4a/b, M5a/b/c, M6, M7 kèm nhiệm vụ, nghiệm thu, rủi ro). Yêu cầu và trạng thái: **`SPEC.md`**.

---

## 11. Rủi ro chính & cách giảm

1. **Đồng bộ nhiều màn khác tần số quét / xé hình** → một render thread, master clock, cho phép nhóm clock; cân nhắc card GPU có Genlock/Sync nếu ghép màn lớn chuyên nghiệp.
2. **Băng thông GPU khi nhiều Output 4K** → render-at-output-res chọn lọc, texture pool, HAP/hw decode, giám sát tải GPU.
3. **Ổn định plug-in bên thứ ba** → shader plug-in là đường chính, native có budget/timeout + vô hiệu hoá tự động, process riêng ở giai đoạn sau.
4. **Chênh lệch driver GPU** → giữ GLSL 330/OpenGL 3.3 core, bộ test render so ảnh trên NVIDIA/AMD/Intel; để ngỏ chuyển sang Qt RHI/Vulkan sau.
5. **Phạm vi quá rộng** → bám MVP: M1–M3 đã đủ dùng được trong sự kiện nhỏ.

---

## 12. Quyết định đã chốt

| Hạng mục | Quyết định |
|---|---|
| Tên | **LumaCast** (phụ đề "LED Display Controller"); "LED Control Studio Product Concept v1.0" là tài liệu UI/UX tham chiếu |
| Nền tảng | **Windows trước**; macOS cần nhưng chưa gấp (M7) |
| GPU | OpenGL 3.3 core; `Compositor` + `GlHelpers` + phần upload texture của nguồn là ranh giới GPU duy nhất (~600 dòng) để thay bằng RHI (QRhi/Metal) sau |
| UI | Qt Widgets (dock), giao diện tối; **không dùng Qt Quick** (Qt Widgets chín hơn cho bàn điều khiển dày đặc, ghép ảnh thu nhỏ đơn giản, ít rủi ro) |
| Output | Mục tiêu 4 Output; độ phân giải chuẩn 1920×1080 và 3840×2160; có scale cho vừa màn LED |
| Âm thanh | Có, bật/tắt theo từng Channel (mục 13); làm ở M5a |
| NDI / Camera | Có; làm ở M5b |
| Giấy phép | Dùng cá nhân; nếu bán có gói Cơ bản và Thương mại (SPEC mục 5). Lõi đóng, SDK plug-in mở (đề xuất); FFmpeg LGPL, Qt LGPL liên kết động — **chờ xác nhận** |

## 13. Âm thanh (thiết kế, chưa triển khai)
- Mỗi Channel có **Audio bus** riêng: công tắc Mute/On, âm lượng, (tuỳ chọn) trễ bù đồng bộ hình.
- Mỗi Source có âm thanh riêng (file, NDI, capture); Layer/Scene quyết định nguồn nào vào bus. Transition crossfade âm thanh theo tiến độ của Program cũ/mới.
- Mỗi Channel chọn **thiết bị âm thanh đầu ra** (WASAPI, shared hoặc exclusive cho độ trễ thấp); có thể gộp nhiều Channel vào một thiết bị.
- Đồng hồ chủ là **đồng hồ âm thanh**; hình đồng bộ theo PTS để tránh lệch tiếng.
- Mixer thread riêng (resample, mix, limiter) tách khỏi render thread; meter mức âm hiển thị trên UI.
- Với NDI/capture: bù trôi đồng hồ bằng resample thích ứng.

## 14. Mở rộng ngoài LED
Lõi (Source/Scene/Channel/Compositor) không phụ thuộc LED. Đích đến là giao diện `OutputSink` (`src/render/OutputSink.h`): hiện có `OutputWindow`
(màn hình LED/projector/LCD qua cửa sổ toàn màn hình); sau này thêm NDI ra, ghi file/RTMP/SRT, Art-Net/sACN/DMX. Tính năng riêng từng loại màn hình
(hiệu chỉnh màu LED, edge-blending + warp cho máy chiếu) đi vào module riêng, cắm trước bước `present()`.

## 15. Chi tiết đã triển khai

**Project (`core/Project.cpp`).** JSON phiên bản 1, ghi nguyên tử; chi tiết ở `PROJECT-FORMAT.md`. Mở file lỗi trả thông báo rõ, không crash.

**Hiệu chỉnh màu** ở shader cuối của Output: `out = pow(clamp(((c−0.5)·contrast+0.5)·brightness·gain), 1/gamma)`.

**Video wall** = nhiều Output cùng một Channel, mỗi cái crop một ô của canvas (`makeWall`). Chưa bù khe/bezel.

**Video xoay.** `FFmpegSource` đọc display matrix (luồng và khung hình) → `TexRef.rotation` (0/90/180/270 theo chiều kim đồng hồ); `Compositor` xoay layer trên GPU,
Fit/Fill tính theo tỉ lệ sau xoay; ảnh theo EXIF khi nạp.

**Monitor, Dừng hình, FTB.** Engine tạo ảnh thu nhỏ (`Compositor::thumbnail`); Dừng hình = Output bỏ qua cả vẽ lẫn hoán đổi bộ đệm nên màn hình giữ khung cuối
(Đen luôn thắng); FTB = `Compositor::fadeToBlack` phủ đen với độ mờ tăng/giảm theo thời gian (rủi ro R1 trong PLAN: Dừng hình chưa xác nhận trên Windows).

**Hoàn tác/làm lại.** `QUndoStack` trong `MainWindow`; mỗi chỉnh sửa Inspector là `LayersCmd`/`OutputCmd` (lưu trước/sau); lần `redo()` đầu bị bỏ qua vì UI đã áp dụng;
các chỉnh sửa liên tiếp cùng một trường gộp bằng `mergeWith` theo `id`. Thao tác live (CUT/TAKE/FTB/Đen) không nằm trong undo; nhập media chưa hoàn tác được.

**T-bar và AUTO.** `Channel::setManual(type, t)` chạy trên render thread, tiến độ trực tiếp (không ease), bị bỏ qua khi đang chạy transition tự động; kéo hết = hoàn tất,
về 0 = huỷ, CUT giữa chừng hoàn tất ngay. AUTO là công tắc giao diện: bật thì bấm Scene = đặt Preview + `startTransition`.

**Pacing.** Một render thread, nhịp `fps` chủ (ngủ gần hết rồi quay vòng 1 ms cuối); khung hoàn tất sau hạn chót được đếm là rơi (`droppedFrames`).
