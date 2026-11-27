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
 ┌──────────────────────────── UI (Qt Widgets)     ────────────────────────────┐
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
- Decode: mỗi Source một thread decode; ring buffer frame; upload qua **PBO bất đồng bộ** (đã làm ở M6, mục 3.5), hoặc zero-copy hw-interop (D3D11VA/NVDEC/VAAPI) khi có (**chưa làm**: M6 chép NV12/P010 từ VRAM về RAM rồi đẩy lên GPU, xem mục 4).
- Audio (nếu cần): luồng riêng, đồng bộ A/V theo PTS; có thể bỏ ở MVP vì LED thường chỉ hình.
- Lệnh từ UI/API đi qua queue lock-free, áp dụng ở đầu khung hình kế tiếp (thay đổi nguyên tử, không xé hình).

### 3.4 Độ tin cậy (quan trọng với LED sự kiện)
- Chế độ **Black / Freeze / Test pattern** tức thì cho từng Output.
- Mất nguồn → giữ khung cuối hoặc đưa ảnh fallback, không crash.
- Watchdog: render thread treo > N ms thì cảnh báo/khởi động lại engine, giữ Output hiện hình cuối.
- Plug-in lỗi không được làm sập chương trình (xem mục 5.4).
- Tự khôi phục project + tự mở lại output sau mất điện (autostart).

### 3.5 Hiệu năng (M6)
**Upload nguồn lên GPU** (`GlHelpers`: `GlTexture`, `YuvTexture`, `HapTexture`, `PboRing`) — mọi nguồn video/live đi qua đây, luôn trên render thread:
- `PboRing`: 2 PBO (`GL_PIXEL_UNPACK_BUFFER`), mỗi lần *orphan* (`glBufferData(NULL)`) rồi `glMapBufferRange(WRITE|INVALIDATE)`, `memcpy`, unmap, `glTex(Sub)Image2D(offset 0)`. DMA của khung trước đọc buffer cũ nên không chờ; map không trả được thì rơi về con trỏ CPU. Tắt: `LUMACAST_NO_PBO=1` hoặc `UploadConfig::setUsePbo(false)`.
- `YuvTexture`: texture `R8/RG8` (hoặc `R16/RG16` cho 10 bit) cho Y, U, V (hoặc Y + UV) → **một pass** vẽ quad vào FBO RGBA8 (`gl_VertexID`, không VBO) → trả texture RGBA cho Compositor như nguồn thường (`Compositor` không biết YUV). Shader: bù/hệ số khoảng (giới hạn/đầy đủ theo số bit), ma trận theo (Kr, Kb) của BT.601/709/2020, chroma lấy mẫu bilinear **lệch 1/4 điểm sắc** cho nội dung "chroma lệch trái" (MPEG-2/H.264; JPEG/MPEG-1 "giữa"). Hàng 0 của texture = hàng trên cùng của ảnh (cùng quy ước `GlTexture`). Chương trình shader dùng chung theo context (`gProgs`).
  `FFmpegSource` chọn ma trận từ `AVFrame::colorspace` (không khai báo: ≥ 720 dòng → 709, ngược lại 601) và khoảng từ `color_range`/`yuvj*`; định dạng ngoài danh sách (RGB, gray, có alpha, 12/16 bit...) → `sws_scale` RGBA như cũ, **cũng đặt cùng ma trận/khoảng** (trước M6 mặc định 601 giới hạn).
- `HapTexture`: texture S3TC (`COMPRESSED_RGB_S3TC_DXT1` / `RGBA_S3TC_DXT5`) upload thẳng khối DXT (`glCompressedTex(Sub)Image2D`); Hap Q (YCoCg) thêm một pass shader. `src/media/Hap.*` tách khung HAP (không phụ thuộc GL/FFmpeg) và giải nén Snappy tự viết; kiểm tra mọi kích thước, khung hỏng bị từ chối.
- `Compositor::thumbnailAsync` + `AsyncReadback`: ảnh monitor đọc ngược qua `GL_PIXEL_PACK_BUFFER`, trả ảnh **của lần gọi trước** (trễ thêm một nhịp monitor ~67 ms) để CPU không chờ GPU xong khung hiện tại.
- `Compositor::drawToCurrent`: Output không hiệu chỉnh (mặc định) bỏ qua phần contrast/gain/`pow` trong shader cuối (`uIdentity`).

**Đo** (`RenderEngine::stages()`, `Compositor::timing()`, `GlProfile`): mỗi khung ghi thời gian CPU của upload (cộng dồn theo thread trong `GlProfile::uploadNs()`), Scene (trừ upload), Transition, monitor, Output; `FrameStats` p50/p99 trên 300 khung cho UI/HTTP (`engine.stages`); `startRecording()/takeRecording()` ghi từng khung cho benchmark (`Benchmark.*`, `lumacast --benchmark`). Mặc định chỉ là thời gian CPU phát lệnh (GPU chạy bất đồng bộ); `setProfileSync(true)` thêm `glFinish` cuối mỗi giai đoạn để ra thời gian GPU thật.

**Có cần render theo nhóm Output / nhiều thread render?** Chưa có số liệu để kết luận. Trên llvmpipe, giai đoạn *output* của 4 Output 4K là phần lớn thời gian khung nhưng đó là chi phí rasterizer phần mềm. Cần số liệu GPU thật (T-PERF, B3/B4, `sync=1`): nếu thời gian *output* + *scene* vượt hạn khung trong khi GPU còn trống, các hướng theo thứ tự rẻ → đắt: (1) Output cùng Channel/cùng crop/scale/hiệu chỉnh/kích thước dùng chung một lần vẽ rồi blit (nay mỗi Output vẽ lại) ; (2) bỏ `glFinish`/swap tuần tự bằng `swapBuffers` song song; (3) nhóm Output theo bộ chuyển đổi/tần số quét, mỗi nhóm một thread + context chia sẻ (đánh đổi: nguy cơ lệch khung giữa các nhóm ghép, rủi ro R2). **Không làm trước khi có số đo** (R2/R3).

---

## 4. Media Engine

- **FFmpeg** (libavformat/libavcodec) decode video; HW decode khi có; hỗ trợ MP4/MOV/MKV, ProRes/H.264/H.265, **HAP** (nên có – rất nhẹ CPU cho LED), image sequence, PNG/JPG/SVG, GIF.
- Loại nguồn: File, Playlist, Image, Text/CG (QML/QPainter→texture), Màu, Camera/Capture (V4L2/DirectShow/Media Foundation), **NDI in** (tuỳ chọn), Web (QtWebEngine), Screen capture.
- Loop, in/out point, tốc độ, seek chính xác, preload nguồn của Scene Preview để chuyển không giật.
- Chuyển đổi màu YUV→RGB bằng shader (BT.601/709/2020, full/limited range) — **đã làm ở M6** (mục 3.5).
- **Giải mã phần cứng (M6, `VideoDecodeOptions`)**: Windows mặc định `auto` = **D3D11VA** qua `hw_device_ctx` của FFmpeg (`get_format` chọn đúng định dạng phần cứng); khung ở VRAM được `av_hwframe_transfer_data` về RAM (NV12/P010) **ngay trong thread giải mã** (trả khung cho bộ giải mã, hàng chờ phát lại không giữ khung VRAM) rồi đi đường shader. Đây **chưa phải zero-copy** (không `WGL_NV_DX_interop`/`EGL_ANGLE` — sẽ làm khi số đo cho thấy đường RAM là nút cổ chai ở 4K). Hệ điều hành khác: tắt mặc định; chọn thiết bị khác bằng `--hwdec vaapi|cuda|...` hoặc `LUMACAST_HWDEC`. Dự phòng: không có thiết bị/codec, `avcodec_open2` lỗi, `receive_frame` báo lỗi, `transfer_data` lỗi, hoặc 60 gói tin liên tiếp không ra khung → mở lại decoder phần mềm và phát lại từ đầu (một lần, có log); `decodeInfo()` hiện chế độ đang dùng.
- **HAP (M6)**: luồng `AV_CODEC_ID_HAP` kích thước chia hết 4 và GPU có S3TC (`HapSupport`, đặt bởi `Compositor::init`) → thread giải mã bỏ qua `libavcodec`, tách khung bằng `hapParseFrame` và đưa khối DXT thẳng lên GPU; HAP là intra-only nên khung không hỗ trợ (Hap Alpha-only, Hap M) đi qua bộ giải mã `hap` của FFmpeg xen kẽ. `LUMACAST_HAP=0` tắt.

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
 ├─ docs/                 SPEC, PLAN, KienTruc, PROJECT-FORMAT, TEST-PLAN, TEST-WINDOWS, CONTROL-API, PLUGIN-SDK
 ├─ src/
 │   ├─ core/             Types, Scene/Layer, Channel (Program/Preview/transition/T-bar/FTB), Project (JSON, wall)
 │   ├─ render/           Compositor (toàn bộ GL), GlHelpers (FBO/texture), Engine (thread render, monitor),
 │   │                    OutputSink (giao diện đích), OutputWindow (cửa sổ LED)
 │   ├─ control/          (M5c) HttpServer (HTTP/1.1 + SSE), Osc (bộ phân tích/dựng gói), ControlApi (lệnh + trạng thái + định tuyến), ControlService (bật/tắt theo cài đặt), trang web điều khiển
│   ├─ audio/            (M5a) AudioStream (ring lock-free + đồng hồ âm thanh), AudioDevice (giao diện + backend Null), AudioWasapi (Windows), AudioMixer (thread mixer, bus/Channel, meter), AudioPlan (dựng kế hoạch trộn từ Project)
│   ├─ media/            Source (giao diện), StaticSources (ảnh/màu/test pattern), FFmpegSource (video + âm thanh)
 │   └─ app/              main (CLI), MainWindow, InspectorPanel (Layer/Output + undo), MonitorWidget, Theme
 └─ tests/                test_render.cpp (lõi/engine), test_ui.cpp (giao diện)
```
**Dự kiến thêm:** `plugin/` (Host, Loader, ManifestParser), `sdk/` (header C ABI + plug-in mẫu), MIDI trong `control/`,
`shaders/` (GLSL tách khỏi mã), backend CoreAudio cho macOS.

**Thư viện đang dùng:** Qt 6 (Core, Gui, Widgets, OpenGL, **Network** — chỉ để dùng `QTcpServer`/`QUdpSocket`; **không** dùng Qt WebSockets, máy Windows của dự án chỉ có Qt Base), FFmpeg (avformat, avcodec, swscale, **swresample**, avutil); âm thanh ra thiết bị chỉ dùng Win32/COM (WASAPI), **không** dùng Qt Multimedia. **Dự kiến:** NDI SDK.
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
| Âm thanh | Có, bật/tắt theo từng Channel (mục 13); **đã làm ở M5a** |
| NDI / Camera | Có; làm ở M5b |
| Giấy phép | Dùng cá nhân; nếu bán có gói Cơ bản và Thương mại (SPEC mục 5). Lõi đóng, SDK plug-in mở (đề xuất); FFmpeg LGPL, Qt LGPL liên kết động — **chờ xác nhận** |

## 13. Âm thanh (M5a, `src/audio/`)
**Đường đi.** `FFmpegSource` (một thread giải mã cho cả hình lẫn tiếng) → swresample đổi sang **float32 xen kẽ, stereo, 48 kHz** (định dạng chung của mọi bus; mono được nhân đôi −3 dB, đa kênh hạ về stereo theo ma trận mặc định của swresample) → `AudioStream` của nguồn (ring lock-free 2 s) → **`AudioMixer`** (thread riêng, khối 10 ms) → ring đầu ra (`targetLatencyMs` mặc định 40 ms) → callback của `AudioDevice`. Callback của thiết bị **chỉ rút từ ring**: không cấp phát, không khoá mutex. Thread mixer có thể lấy một mutex rất ngắn (đổi con trỏ kế hoạch, một lần mỗi khối khi có thay đổi) và không cấp phát ở trạng thái ổn định (vector dùng lại).

**Kế hoạch trộn (`AudioPlan`).** Mixer không biết gì về Scene/Transition. **Mỗi khung hình**, render thread (sau khi các Channel đã render) gọi `buildAudioPlan(project, now)` và chỉ đẩy cho mixer khi kế hoạch **đổi** (`AudioPlan::sameAs`). Mỗi mục = (nguồn, Channel, hệ số). Hệ số = trọng số Scene (`Channel::audioWeights`) × âm lượng Layer × âm lượng Channel × (không mute) × (1 − FTB), Layer ẩn = 0. Vì tính trên render thread từ **cùng trạng thái transition** mà hình dùng, crossfade âm thanh đi đúng tiến độ hình kể cả easing và T-bar; không có logic transition thứ hai để lệch. Mute/âm lượng/FTB do UI/HTTP/OSC đặt qua `RenderEngine::post()` lên `Channel::mute/volume` (atomic) — đúng quy tắc "mọi thay đổi đi qua render thread". Quy tắc trọng số: Fade/Wipe/Push/Plugin: nội suy **tuyến tính** theo `easing` (tổng hệ số = 1, nên cùng một nguồn ở hai Scene không bị to lên; nếu hai nguồn khác nhau thì giữa chừng thấp hơn khoảng 3–6 dB — chọn tuyến tính thay vì công suất đều để cùng-nguồn không nhô); Dip: Program về 0 ở nửa đầu, Preview lên ở nửa sau; Stinger: cắt cứng ở `cutPoint`, tiếng của chính video stinger đi vào bus trong lúc chạy. Preview bình thường **không ra tiếng**.

**Mọi nguồn trong mọi Scene của Channel đều có mặt trong kế hoạch (hệ số 0 nếu chưa lên sóng)** để mixer rút đều và đồng hồ âm thanh của nó chạy đúng; nguồn xuất hiện ở nhiều Channel/Scene chỉ bị rút **một lần** mỗi khối rồi cộng vào các bus. Mute = hệ số 0 chứ không phải ngừng rút, nên bật lại không lệch hình–tiếng. Hệ số được đổi **tuyến tính theo từng mẫu** trong khối (nguồn mới vào kế hoạch tăng từ 0), nên không có tiếng "lách" khi bấm mute hay khi kế hoạch đổi mỗi khung.

**Đồng bộ: hình theo tiếng.** `AudioStream::heardPosition()` = vị trí mẫu mixer đã rút − độ trễ (ring đầu ra + bộ đệm thiết bị) — là vị trí **đang được nghe**. Luồng giải mã dùng nó làm đồng hồ chủ cho hình **khi mixer đang rút nguồn đó** (`consumerActive`, 250 ms) và âm thanh còn chạy; ngược lại (không có tiếng, chưa mở thiết bị, nguồn không nằm trong kế hoạch, underrun > 250 ms) rơi về **đồng hồ tường tích phân** (liên tục, không nhảy). Khung hình được giải mã vào hàng chờ (`pend`, ≤ 6 khung; 30 khi âm thanh sắp cạn) và chỉ đưa lên khi `cont ≤ đồng hồ`; khung đến hạn dồn thì bỏ khung cũ. *Dòng thời gian liên tục:* hình có `cont = pts + vOffset`, `vOffset` đặt ở **mẫu âm thanh đầu tiên của mỗi đoạn** (đầu file/sau `restart`/sau mỗi vòng lặp) = vị trí mẫu đó trên đồng hồ − pts của nó. Nhờ vậy độ lệch hình–tiếng vốn có trong file được giữ nguyên và **độ dài hình ≠ độ dài tiếng không cộng dồn qua các vòng lặp** (một clip 10 s hình/10,02 s tiếng lặp 30 phút sẽ lệch 3,6 s nếu mỗi vòng dùng một mốc cố định). Lặp là **nối liền**: không flush ring ở cuối file. Nếu không ai rút (nguồn không nằm trong Scene nào / mixer tắt) thì nguồn chỉ giữ ~100 ms tiếng mới nhất để lúc mixer bắt đầu không phát lại tiếng cũ.

**`restart()` và `SceneClock`.** `Source::restart()` (Stinger) gọi `AudioStream::holdUntilFlush()` ngay (mixer im lặng tức thì, không phát nốt tiếng cũ) rồi thread giải mã seek về 0, flush decoder/swr/ring và mở lại. `SceneClock` (đồng hồ keyframe) **độc lập** với đồng hồ nguồn video — video/tiếng luôn tự chạy và lặp theo file (giống hành vi hình từ M1), nên `play/pause/seek` của timeline không đụng tiếng; `restartOnAir` chỉ đặt lại keyframe.

**Thiết bị (`AudioDevice`).** Giao diện nhỏ: `start(renderFn)`, `stop()`, `name()`, `latencySeconds()`; `listAudioDevices()`/`createAudioDevice(id)` chọn backend. *WASAPI* (`AudioWasapi.cpp`, chỉ Windows): shared mode, event-driven, yêu cầu float32 stereo 48 kHz với `AUTOCONVERTPCM | SRC_DEFAULT_QUALITY` (Windows tự đổi tần số/số kênh cho thiết bị thật nên không cần resampler riêng), MMCSS "Pro Audio", thử mở lại mỗi giây nếu thiết bị bị rút; GUID ghi thẳng trong mã (không cần `.lib` chứa IID). *Null*: thread tiêu thụ theo đồng hồ thật — dùng cho test và nền tảng chưa có backend. Thiết bị hỏng → mixer **tự dùng Null** (đồng hồ hình vẫn chạy). Chọn thiết bị lưu trong project (`audio.device`); một thiết bị ra chung cho mọi Channel (mỗi Channel một thiết bị riêng: chưa làm). `RenderEngine` sở hữu `AudioMixer`; thiết bị do `main` mở (`--no-audio` để bỏ) hoặc do dock *Âm thanh*, nên test không có engine-âm-thanh trừ khi tự mở.

**UI.** Dock *Âm thanh* (`AudioPanel`): mỗi Channel một dải (nút Tắt tiếng, thanh 0–150%, `LevelMeter` peak+RMS theo dB −60…0, vàng ≥ −12, đỏ ≥ −3) + meter master + chọn thiết bị. Tắt tiếng/âm lượng là **điều khiển live** nên còn dùng được khi Khoá vận hành; chọn thiết bị là thao tác sửa nên bị khoá. UI chỉ đọc meter (atomic) và trạng thái atomic của `Channel`, tự đồng bộ khi giá trị bị đổi từ HTTP/OSC.

**Giới hạn đã biết.** NDI làm nguồn tiếng và bù trôi đồng hồ bằng resample thích ứng đã làm ở M5b (mục 16); chưa có: tiếng từ camera/card capture, WASAPI exclusive, mỗi Channel một thiết bị, âm lượng theo keyframe, limiter thật (hiện cắt cứng ±1 và đếm `clippedBlocks`), CoreAudio. Tối đa 32 bus (`kMaxAudioChannels`).

## 14. Mở rộng ngoài LED
Lõi (Source/Scene/Channel/Compositor) không phụ thuộc LED. Đích đến là giao diện `OutputSink` (`src/render/OutputSink.h`): hiện có `OutputWindow`
(màn hình LED/projector/LCD qua cửa sổ toàn màn hình); NDI ra đã có từ M5b (`NdiOutputSink`, mục 16); sau này thêm ghi file/RTMP/SRT, Art-Net/sACN/DMX. Tính năng riêng từng loại màn hình
(hiệu chỉnh màu LED, edge-blending + warp cho máy chiếu) đi vào module riêng, cắm trước bước `present()`.

## 15. Chi tiết đã triển khai

**Project (`core/Project.cpp`).** JSON phiên bản 1, ghi nguyên tử; chi tiết ở `PROJECT-FORMAT.md`. Mở file lỗi trả thông báo rõ, không crash.

**Hiệu chỉnh màu** ở shader cuối của Output: `out = pow(clamp(((c−0.5)·contrast+0.5)·brightness·gain), 1/gamma)`.

**Video wall** = nhiều Output cùng một Channel, mỗi cái crop một ô của canvas (`makeWall`). **Bù khe/bezel** (OUT-04): canvas = ô + khe, các crop chừa đúng phần khe nên không Output nào hiện nó và mỗi ô vẫn 1:1. Không lưu trường riêng: `detectWall` nhận lại lưới, ô và khe từ `crop` + kích thước canvas (nên project cũ cũng đặt khe được). Đổi khe lúc đang chạy (`InspectorWall.cpp`) đổi kích thước Channel, các Scene cùng cỡ và crop của mọi ô trong **một** lệnh render thread để không có khung hình lệch; `TargetPool` giải phóng bộ đệm cỡ cũ sau 300 khung không dùng.

**Video xoay.** `FFmpegSource` đọc display matrix (luồng và khung hình) → `TexRef.rotation` (0/90/180/270 theo chiều kim đồng hồ); `Compositor` xoay layer trên GPU,
Fit/Fill tính theo tỉ lệ sau xoay; ảnh theo EXIF khi nạp.

**Monitor, Dừng hình, FTB.** Engine tạo ảnh thu nhỏ (`Compositor::thumbnail`); Dừng hình = Output bỏ qua cả vẽ lẫn hoán đổi bộ đệm nên màn hình giữ khung cuối
(Đen luôn thắng); FTB = `Compositor::fadeToBlack` phủ đen với độ mờ tăng/giảm theo thời gian (rủi ro R1 trong PLAN: Dừng hình chưa xác nhận trên Windows).

**Hoàn tác/làm lại.** `QUndoStack` trong `MainWindow`; mỗi chỉnh sửa Inspector là `LayersCmd`/`OutputCmd` (lưu trước/sau); lần `redo()` đầu bị bỏ qua vì UI đã áp dụng;
các chỉnh sửa liên tiếp cùng một trường gộp bằng `mergeWith` theo `id`. Thao tác live (CUT/TAKE/FTB/Đen) không nằm trong undo. Từ M5c nhập media và thuộc tính Scene cũng hoàn tác được (xem bên dưới).

**T-bar và AUTO.** `Channel::setManual(type, t)` chạy trên render thread, tiến độ trực tiếp (không ease), bị bỏ qua khi đang chạy transition tự động; kéo hết = hoàn tất,
về 0 = huỷ, CUT giữa chừng hoàn tất ngay. AUTO là công tắc giao diện: bật thì bấm Scene = đặt Preview + `startTransition`.

**Pacing.** Một render thread, nhịp `fps` chủ (ngủ gần hết rồi quay vòng 1 ms cuối); khung hoàn tất sau hạn chót được đếm là rơi (`droppedFrames`).

**Độ tin cậy (M3c).**
- *Nhận diện màn hình:* `OutputConfig.screenName` (`QScreen::name()`) ưu tiên hơn `screenIndex` vì chỉ số đổi khi rút một màn giữa chừng (`resolveScreenIndex`). Màn không còn → Output **không hiển thị**, tuyệt đối không dùng màn khác.
- *Giám sát:* `OutputSupervisor` (UI thread, 500 ms + tín hiệu `screenAdded/Removed`) tính `OutputHealth` (Ok/NoScreen/Hidden/WrongScreen); mất màn thì ẩn cửa sổ, màn quay lại hoặc cửa sổ bị Qt dời thì `applyScreen()`; ghi log mỗi lần đổi.
- *Watchdog:* render thread cập nhật `heartbeat` mỗi khung; `stalled()` đúng khi quá ngưỡng (mặc định 2 s). Khi treo, cửa sổ giữ hình cuối vì không có `swapBuffers`; giao diện báo đỏ và ghi log.
- *Thống kê khung:* `FrameStats` (vòng đệm 300 mẫu) cho p50/p99/max thời gian render, cập nhật mỗi giây.
- *Mẫu thử theo Output:* `makeOutputTestPattern` vẽ đúng độ phân giải cửa sổ, bỏ qua scale/xoay/crop/hiệu chỉnh; ưu tiên hiển thị: Đen > Mẫu thử > Dừng hình > Program.
- *Log:* `core/Log` cài `qInstallMessageHandler`, ghi file xoay vòng (2 MB × 3).

**Effect, Transition và plug-in shader (M4a).**
- *Một cơ chế duy nhất:* `EffectDef` (id, loại, tham số, mã GLSL) dùng cho Effect/Transition dựng sẵn (`BuiltinEffects.cpp`) lẫn plug-in nạp từ thư mục (`PluginManager`). `buildFragmentSource` ghép header chuẩn + tham số thành uniform + `#line 1` + mã người dùng + `main()`.
- *Kho dùng chung:* `EffectRegistry` giữ bản bất biến của các định nghĩa (đổi nguyên khối dưới mutex, người đọc giữ `shared_ptr`) nên UI thread nạp plug-in và render thread biên dịch/dùng không đụng nhau. Biên dịch ở render thread, nhớ theo `(id, revision)`; `revision` tăng khi **nội dung** (manifest + shader, băm SHA-1) đổi → nạp lại nóng chỉ biên dịch lại phần đã đổi.
- *Chuỗi Effect:* `Compositor::applyLayerEffects` chạy trên texture nguồn (đảo trục dọc ở lượt đầu vì nguồn lưu từ trên xuống), ping-pong qua FBO của pool; kết quả ở FBO (gốc dưới-trái) nên lượt vẽ Layer lật lại uv. Chạy ở độ phân giải của nguồn.
- *Transition:* `TransitionSpec` (kiểu, `pluginId`, tham số, easing, thời lượng, Stinger). Fade/Dip/Wipe vẫn là shader cứng; Push/Slide/Zoom/Luma và plug-in là shader từ kho. Stinger: Channel cắt cảnh cứng ở `cutPoint`, `Compositor::drawOverlay` vẽ video alpha đè lên; `Source::restart()` đưa video về đầu lúc bắt đầu (FFmpeg: seek về 0, trả rỗng cho đến khi có khung mới để không hiện khung cuối cũ).
- *An toàn:* manifest/entry bị kiểm tra (chặn `../`, id `builtin.` bị cấm); shader lỗi → Effect bị bỏ qua và lỗi ghi vào kho; project giữ nguyên dữ liệu Effect của plug-in thiếu. **Không** chống được shader chạy vô hạn (xem `PLUGIN-SDK.md` mục 6).

**Hoạt hoạ, xoay/crop/blend (M4b).**
- *Mô hình:* `Layer` có thêm `rotation`, `crop`, `blend`, `tracks` (`Track` = thuộc tính + danh sách `Keyframe{time, value, interp}` luôn sắp xếp). `evalLayer(layer, t)` trả `LayerState` (rect/opacity/rotation): track ghi đè giá trị tĩnh; trước/sau key biên giữ giá trị biên; giữa hai key nội suy theo `interp` của key trước (`evalTrack`).
- *Đồng hồ Scene:* `SceneClock` (atomic `time`/`playing`) nằm trong `Scene`; engine `tick()` cho **mọi Scene** mỗi khung (rẻ), chỉ tiến khi `playing`. `Channel` gọi `restart()` cho Scene đi vào khi CUT / bắt đầu TAKE / bắt đầu kéo T-bar nếu `restartOnAir`. `Compositor::renderScene` đọc `scene.clock.time`, nên Preview, Program và cả hai Scene trong một lần chuyển đều đúng thời điểm của riêng mình.
- *Xoay hình học:* đổi ở vertex shader chung (`uRotSC`, `uAspectM1`): xoay `offset` của đỉnh trong không gian điểm ảnh (hiệu chỉnh tỉ lệ rộng/cao đích) nên hình vuông không bị méo trên Scene không vuông; mặc định uniform = 0 nghĩa là không xoay nên các chương trình khác không phải đụng tới.
- *Crop:* tính trong không gian **hình đã xoay** rồi đổi sang vùng trên texture gốc bằng `cropToTexture` (0/90/180/270); Fit/Fill dùng tỉ lệ của vùng đã crop; sau cùng `uUvRect` + `uUvRot` lấy mẫu vùng con và xoay quanh tâm của chính vùng đó.
- *Blend:* `glBlendFuncSeparate` theo chế độ + `uBlendMode` trong shader: Add/Screen dùng màu nhân alpha trước; Multiply dùng `mix(1, rgb, alpha)`; alpha đích không bị đổi.
- *Timeline:* `TimelineWidget` + `TrackView` (tự vẽ). Mọi sửa đổi đi qua `InspectorPanel::editCurrentLayer` nên dùng chung `QUndoStack`; mỗi lần nhấn chuột kéo key có `dragSession` riêng để không bị gộp với lần kéo trước. Thuộc tính Scene (thời lượng/lặp/tự phát lại) từ M5c đi qua `ScenePropsCmd` nên hoàn tác được. Playhead đọc `clock.time` (atomic); khi tua, UI dùng `scrubTime_` để không phải đợi render thread.
- *An toàn:* timeline báo khi Scene đang chọn đang On Air; từ M5c có thêm Khoá vận hành (xem bên dưới).

**Điều khiển ngoài (M5c, `control/`).** Chi tiết API ở `CONTROL-API.md`; ở đây là các quyết định thiết kế.
- *SSE thay vì WebSocket.* Nhu cầu là *đẩy trạng thái một chiều*; lệnh đã có REST. SSE là HTTP thường nên dùng chung bộ phân tích, giới hạn, xác thực và kiểm tra Origin/Host với phần còn lại, không có bắt tay nâng cấp/mask/phân mảnh/ping-pong (nơi hay có lỗi khi tự viết RFC 6455). Qt WebSockets lại không có trên máy Windows của dự án, nên chỉ cần `QtNetwork`. Trình duyệt dùng `fetch()` đọc luồng thay `EventSource` để gửi được header `Authorization` (token không đi qua URL). Nếu sau này cần lệnh hai chiều độ trễ thấp, WebSocket có thể thêm trên cùng `HttpServer` mà không đổi `ControlApi`.
- *Luồng và dữ liệu.* `HttpServer`/`OscServer`/`ControlApi` chạy trên UI thread (vòng lặp sự kiện Qt), không có thread riêng. Mọi lệnh gọi `RenderEngine::post()` như giao diện. Trạng thái đọc từ các trường atomic (Program/Preview/FTB/Đen/Dừng/Mẫu/đồng hồ Scene) và từ **bản chụp** danh sách Scene/Output lấy trên UI thread (`ControlApi::refreshProject()` sau mỗi thay đổi project); FPS từng Output đo bằng timer 500 ms. Các lệnh Output cũng đi qua `post()` (khác các nút trên giao diện hiện còn ghi thẳng atomic) nên có thêm độ trễ ≤ 1 khung.
- *Dùng chung HTTP và OSC.* Cả hai gọi cùng các hàm lệnh của `ControlApi` (`setPreview`, `cut`, `take`, `ftb`, `outputFlag`, `blackAll`, `sceneTransport`) nên quy tắc an toàn không thể lệch nhau; `handleOsc` chỉ ánh xạ địa chỉ → hàm lệnh.
- *Mô hình mối đe doạ.* Mặc định tắt và chỉ loopback; LAN → token bắt buộc (≥ 16 ký tự, so sánh hằng thời gian) hoặc không mở cổng; giới hạn tài nguyên (header/thân/URL/kết nối/SSE/thời gian) chống treo; Origin≠Host → 403 (CSRF qua trình duyệt), Host lạ khi loopback → 403 (DNS rebinding); không CORS. OSC không có xác thực nên chỉ loopback theo mặc định. HTTP không mã hoá (cần mạng điều khiển riêng/VPN/proxy TLS).
- *Khoá vận hành và API.* `ControlApi` hỏi một hàm `lockProvider`; tuyến đăng ký với `edit=true` trả `423` khi khoá. Hiện chưa có tuyến sửa cấu hình nào (lệnh live luôn chạy); khoá không thể được mở từ API.

**Khoá vận hành (M5c, UI-07).** `MainWindow::setLocked` vô hiệu: `InspectorPanel` (cả widget lẫn `editLayers/editOutput`), `TimelineWidget` (sửa key/thời lượng/lặp, kéo thước; Phát/Tạm dừng/Về 0 giữ lại), dock Plug-in, Nhập media (nút, menu, nhấp đúp Media Pool, `addMediaFile`), Hoàn tác/Làm lại (action tự định nghĩa thay `createUndoAction` để khoá được), Mở project/Gần đây, Cài đặt, và `closeEvent`. Điều khiển live không bị đụng. Mở khoá bằng `LockButton` phải giữ ≥ 1 s (chuột hoặc Space). Trạng thái chỉ nằm trong bộ nhớ (không `QSettings`, không project).

**Tự khởi động và mở lại project (M5c, OPS-01).** `core/Startup` tách logic khỏi registry: `setAutostart/autostartState` nhận một `QSettings` bất kỳ (registry thật chỉ ở `openRunKey()` trên Windows; test dùng file .ini). `decideStartupProject` + `pickProjectToLoad` dùng chung cho `--project` và tự mở lại; tự mở lại bị bỏ qua nếu dòng lệnh có `-o/-m/--wall/--save`. Thất bại (file mất/hỏng) chỉ đẩy thông báo lên thanh trạng thái, không dừng chương trình, để khởi động không người trực vẫn lên được.

**Hoàn tác nhập media (M5c, PRJ-07).** `ImportCmd` (trong `MainWindow.cpp`) giữ `ImportEntry{SourceDef, Scene}`; `redo` = thêm vào `project_`/mọi Channel trên render thread, `undo` = gỡ ra và chỉnh chỉ số Program/Preview (Preview trỏ vào Scene bị gỡ → -1), gỡ nguồn nếu không Scene nào còn dùng. Cả hai chạy bằng `RenderEngine::postAndWait` (chờ có giới hạn 2 s) rồi cập nhật ngay bản sao UI (Inspector/Timeline/API/danh sách) để các bước Hoàn tác nối nhau thấy dữ liệu nhất quán. Scene đang ở Program hoặc đang chuyển → từ chối ở UI, kiểm tra lại ở render thread (lệnh live có thể xen vào giữa) và nếu bị chặn thì khôi phục ngăn xếp bằng `redo()`.

**Timeline nâng cao (M5c).** Vùng nhìn = (`viewStart_`, `zoom_`); `timeToX/xToTime` đổi theo, thước và key vẽ trong vùng cắt để không đè cột nhãn. Vùng chọn lưu theo `(thuộc tính, thời điểm)` (không theo chỉ số vì chỉ số đổi khi sửa) và được dọn mỗi lần `refresh()`. Mọi thao tác nhóm đi qua `InspectorPanel::editCurrentLayer` (một bước Hoàn tác); kéo liên tục dùng `mergeId` theo `dragSession_`. Bộ nhớ đệm chép/dán dùng chung giữa các Scene trong tiến trình.

**Cài đặt theo máy và cô lập khi test (M5c).** Luôn dùng `lc::appSettings()` (`core/AppSettings.h`) thay cho `QSettings("LumaCast", "LumaCast")`: constructor (tổ chức, ứng dụng) luôn dùng `NativeFormat` nên **bỏ qua** `QSettings::setDefaultFormat()` mà test gọi, khiến test ghi đè lên cấu hình/registry thật (lỗi đã có từ trước M5c, được sửa cùng đợt). `appSettings()` theo `defaultFormat()`: sản phẩm vẫn là `NativeFormat` (cùng vị trí cũ), test dùng `IniFormat` + `setPath`. Biến môi trường `LUMACAST_CONFIG_DIR` đặt thư mục cấu hình + log riêng cho một tiến trình `lumacast` (dùng trong test tiến trình con; hữu ích cho bản mang theo).

## 16. Nguồn trực tiếp: NDI và camera (M5b, `src/live/`, `src/render/NdiOutput.*`, `src/audio/AudioDrift.h`)

**Phân lớp.** `Source` (hình + `audio()`) ← **`LiveSource`** (nguồn trực tiếp chung) ← `LiveInput` (một *kết nối* đã mở: `capture(timeout)` trả Video/Audio/Lost/None). `LiveInputFactory` mở kết nối mới mỗi lần nối lại. Backend cắm vào factory: **NDI** (`NdiBackend`: `RealBackend` nạp runtime thật, `FakeNdiBackend` cho test/demo, `none` khi thiếu runtime) và **camera** (`CameraBackend`: Media Foundation trên Windows, `FakeCameraBackend`, `none` trên Linux/macOS). Backend dùng chung được chọn lúc chạy qua `ndiBackend()`/`cameraBackend()` (test đổi bằng `setNdiBackend`/`setCameraBackend`; `LUMACAST_NDI_BACKEND=fake`, `LUMACAST_CAMERA_BACKEND=fake` để dev/demo, `LUMACAST_NDI_DISABLE=1` tắt NDI).

**`LiveSource`.** Một thread: mở kết nối → vòng `capture(20 ms)`. Hình → RGBA vào ô "mới nhất" (một `shared_ptr<Frame>` dưới mutex; khung cũ chưa kịp lên GPU bị thay, đếm `droppedVideo`) — **không có hàng chờ nên độ trễ phía LumaCast ≤ 1 khung render**. `acquire()` (render thread) chỉ upload khung mới nhất; không bao giờ chờ mạng/thiết bị. Mất nguồn = `Lost` từ kết nối **hoặc** không có dữ liệu trong `stallMs` (NDI 2 s, camera 2,5 s): giữ nguyên `latest_` + texture (nên Program **giữ khung cuối**, không đen), đóng kết nối, đợi backoff 0,5→2 s rồi mở lại; trạng thái giữ `Lost` cho tới khi có khung hình mới. `Unavailable` (thiếu NDI Runtime / nền tảng chưa hỗ trợ) thử lại thưa (3 s) — không phải lỗi mở project. Huỷ nguồn = set cờ + join (mọi `capture` có timeout nên không treo).

**Âm thanh nguồn trực tiếp và bù trôi đồng hồ.** Nguồn trực tiếp **không có đồng hồ file**: hình hiện ngay khi tới, tiếng đi theo đồng hồ của *người gửi*, còn mixer rút theo đồng hồ *thiết bị ra* — lệch vài chục–vài trăm ppm sẽ làm ring cạn (rè/mất tiếng) hoặc đầy (trễ tăng mãi). `DriftResampler` (header-only, `AudioDrift.h`) giữ ring ở mức đệm mục tiêu (60 ms):
- đổi mọi tần số/kênh về float32 stereo 48 kHz bằng **nội suy tuyến tính** với bước `rate/48000·(1+ε)`;
- ε do **bộ điều khiển PI** theo độ đầy ring (đã lọc EMA): `ε = Kp·err + ∫`, `Kp=0,3`, `Ki=0,0225` (tắt dần tới hạn, ω≈0,15 rad/s → hội tụ ~30 s), kẹp **±0,5%** (≈±8,6 cent, không nghe thấy), làm mượt đổi ε từng khối để không "run" cao độ;
- ε tích phân chính là độ lệch đồng hồ ước lượng (`ratioPpm()` đã lọc ~1 s, hiện trong `stats()`);
- **mất đồng bộ** (ring cạn giữa chừng → đệm lại bằng im lặng; ring > 3× mục tiêu → bỏ bớt; ring tràn) được **đếm** (`audioResyncs`) thay vì cố bù tiếp; nếu không ai rút (mixer chưa chạy/nguồn không nằm trong Scene nào) chỉ giữ ~mục tiêu mới nhất, không tích trữ.
*Giới hạn:* nội suy tuyến tính kém chất lượng với tần số vào thấp (44,1 kHz); đa kênh > 2 chỉ lấy 2 kênh đầu; hội tụ chậm (~30 s) nên lúc đầu độ lệch lớn sẽ tăng/giảm đệm dần chứ không nhảy; **lệch hình–tiếng tĩnh ≈ mức đệm (60 ms) + độ trễ mixer (40 ms) + đường ra thiết bị** vì hình hiện ngay (chưa có đường trễ hình — chưa làm); `ratioPpm` đo sẵn để chỉnh khi thử trên máy thật (T-NDI-IN). Camera hiện **không có âm thanh** (cần WASAPI capture — chưa làm).

**NDI vào (`NdiRuntime.cpp`).** Nạp runtime bằng `QLibrary` (Windows `Processing.NDI.Lib.x64.dll`, tìm qua `NDI_RUNTIME_DIR_V*` rồi PATH; Linux `libndi.so.6/5`; `LUMACAST_NDI_LIB` ép đường dẫn — test dùng để nạp thư viện giả) và `resolve` từng hàm C `NDIlib_*`; thiếu hàm nào ⇒ coi là không tương thích (không crash). Struct/enum tự khai báo tối thiểu (`static_assert` kích thước x64); nhận ở định dạng `RGBX_RGBA` (RGBX → ép alpha 255; BGRA/BGRX cũng được xử lý), âm thanh planar float → xen kẽ. **ABI chưa đối chiếu với header chính thức** (ghi ở đầu file và `SPEC.md` 5.1). Liệt kê nguồn dùng một `find` bền (`find_wait_for_sources`) gọi từ thread phụ của hộp thoại, không bao giờ từ render thread.

**NDI ra (`NdiOutputSink`).** `present()` (render thread): vẽ Program vào FBO kích thước cấu hình bằng `drawToCurrent` (cùng scale/crop/xoay/hiệu chỉnh như màn hình), `glReadPixels` vào PBO[i] và lấy PBO[1−i] của khung trước (trễ +1 khung, **không chặn GPU**), lật dọc khi sao chép, đưa vào ô "mới nhất". **Thread gửi riêng** lấy ô đó và gọi `NdiSender::sendVideo` (đồng bộ nhưng chỉ chặn thread gửi); render nhanh hơn gửi thì bỏ khung cũ (đếm `droppedFrames`); không có khung mới 1 s thì gửi lại khung cuối. Âm thanh: `AudioMixer::setTap(channel, ring)` — mixer đẩy bus Channel sau mỗi khối 10 ms, thread gửi rút từng khối 10 ms. Đen/Dừng/Mẫu có nghĩa như `OutputWindow` (Dừng = không cập nhật khung). Thiếu runtime: thread gửi báo trạng thái và thử lại mỗi 3 s; render không bị ảnh hưởng. `RenderEngine` giữ `sinks_` dưới `sinksMu_`; `addNdiOutput` chạy trên render thread (`postAndWait`), chỉ **thêm** (không xoá) lúc đang chạy nên con trỏ Output ổn định tới `stopEngine`; `OutputSink::shutdown()` giải phóng GL của sink trước khi huỷ context.

**Camera Media Foundation (`CameraMF.cpp`, chỉ Windows).** `IMFSourceReader` **bất đồng bộ** (callback) với `MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING` → RGB32 → BGRX→RGBA (tôn trọng stride âm = ảnh ngược); `capture()` chờ condvar có hạn nên ReadSample treo không chặn `LiveSource`; lỗi/`ENDOFSTREAM` (rút thiết bị) → `Lost`. Trạng thái giữa callback và `MfInput` chia sẻ qua `shared_ptr<Shared>` (callback có thể đến muộn sau khi `MfInput` bị huỷ). Liệt kê thiết bị theo `MFEnumDeviceSources` (id = symbolic link, lưu kèm tên để nhận lại khi id đổi), chế độ liệt kê riêng khi chọn thiết bị (mở reader). **Mới kiểm tra cú pháp bằng mingw, chưa chạy trên Windows.**

**Project.** Nguồn `ndi` (`ndiName`, `ndiUrl`, `label`) và `camera` (`device`, `deviceName`, `width`, `height`, `fps`), Output `type: "ndi"` (`ndiName`, `ndiAudio`) — tuỳ chọn, `version` vẫn 1 (`PROJECT-FORMAT.md`). Tạo nguồn trực tiếp **không bao giờ thất bại** vì thiếu NDI/camera.

---

## 17. Phát hành Windows và giấy phép (M6)

- **Cấu hình cài đặt** nằm trong `CMakeLists.txt` (phần cuối): `install()` đặt `lumacast`, plug-in mẫu (`<exe>/plugins`), tài liệu, `THIRD-PARTY-NOTICES.txt`, `license.example.json`, `licenses/`. Windows: cài thẳng vào thư mục gốc (exe + DLL cạnh nhau, đúng chỗ `PluginManager::defaultDirs()` tìm). **Qt**: `qt_generate_deploy_app_script` (windeployqt; dự phòng `FILENAME_VARIABLE` cho Qt 6.3–6.4). **FFmpeg**: glob `avcodec/avformat/avutil/swscale/swresample*.dll` từ `LUMACAST_FFMPEG_BIN_DIR` hoặc `../bin` của `FFMPEG_LIBRARY_DIRS`/vcpkg, rồi `file(GET_RUNTIME_DEPENDENCIES)` kéo theo DLL phụ thuộc (zlib...). Giấy phép `copyright` của vcpkg (ffmpeg, qtbase) được chép vào `licenses/` nếu thấy.
- **CPack**: NSIS (mặc định) + ZIP mang theo; `-DLUMACAST_INSTALLER=WIX` chuyển sang WiX (chưa thử; GUID nâng cấp cố định trong CMake). Trang đầu của trình cài là `THIRD-PARTY-NOTICES.txt` (chưa có EULA riêng). Lệnh: `cmake --build build --config Release && cpack --config build/CPackConfig.cmake -C Release`.
- **Rào chặn NDI** (`cmake/CheckNoNdi.cmake`): bước `install(CODE)` cuối cùng; thấy `Processing.NDI*`, `libndi*`, `ndi*.dll/.so` trong thư mục đóng gói thì cài đặt dừng với lỗi. Có test (`ctest -R package`).
- **Giấy phép thư viện**: `libraryInfo()` (`src/app/AboutInfo.*`) đọc `avutil_license()`/`avutil_configuration()` và nhận ra Qt tĩnh (`QT_STATIC`); hiện trong Giới thiệu kèm cảnh báo; ctest `license` ép FFmpeg LGPL khi `LUMACAST_REQUIRE_LGPL=1` (CI Windows/phát hành). Dòng "NDI® is a registered trademark of Vizrt NDI AB." nằm ở `ndiTrademarkNotice()`.
- **Cờ tính năng theo gói** (`src/core/Features.*`): `Entitlements` (edition, licensee, maxOutputs, 5 cờ) đọc từ `license.json` (`$LUMACAST_LICENSE`, `<exe>/license.json`, `%AppData%\LumaCast\license.json`); không có file = không khoá. Mọi chỗ thi hành chỉ hỏi `Entitlements::current()` (`createSource` NDI/camera, `PluginManager::rescan`, `ControlService::apply`, `RenderEngine::startEngine/addNdiOutput`, `MainWindow` timeline + các hộp thoại thêm nguồn). Output vượt gói/NDI bị chặn được thay bằng `DisabledSink` giữ chỗ để `syncOutputsToProject()` không lệch chỉ số. Đây là cờ cấu hình, **không** phải DRM.
- **CI**: `.github/workflows/ci.yml` (Linux đầy đủ; Windows: Qt qua `install-qt-action`, FFmpeg qua `vcpkg.json`, kiểm tra LGPL + gói là bắt buộc, `ctest` đầy đủ không chặn vì runner không có GPU) và `release.yml` (dựng bộ cài, tải LGPL-2.1/3.0 vào `licenses/`, tải artifact lên, không tự phát hành). **Chưa chạy trên GitHub.**

