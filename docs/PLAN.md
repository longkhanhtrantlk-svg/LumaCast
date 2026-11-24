# LumaCast — Kế hoạch phát triển (PLAN)

**Cập nhật:** sau M5a (nhánh `m5a-audio`, xếp chồng trên `m4b` đã gồm M5c) · Yêu cầu: `SPEC.md` · Thiết kế: `KienTruc.md` · Kiểm thử: `TEST-PLAN.md`

## 1. Hiện trạng
| Mốc | Nội dung | Trạng thái | PR |
|---|---|---|---|
| M1 | Lõi render GPU, Output nhiều cửa sổ, Scene/Layer, Channel Program/Preview, Cut/Fade/Dip/Wipe, nguồn ảnh/màu/video | ✅ xong | (trong #1) |
| M2 | Project JSON, hiệu chỉnh màu, video wall, nhiều Layer, video dọc | ✅ xong | #1 |
| M3a | Giao diện Live Control: monitor, Scene list, Media Pool, Outputs, Inspector, FTB, Đen/Dừng, tự lưu | ✅ xong | #1 |
| M3b | Hoàn tác/làm lại, T-bar, AUTO, panel Layer kéo-thả | ✅ xong | #2 |
| M3c | Độ tin cậy: log, watchdog, mất màn, mẫu thử theo Output, thống kê khung | 🟡 phần mã xong; còn thử trên phần cứng thật | #4 |
| M4a | Effect trên Layer, Transition mới + Stinger + easing, plug-in shader, Plugin Manager | 🟡 phần mã xong; còn thử trên Windows | #5 |
| M4b | Xoay/crop/blend Layer, đồng hồ Scene, keyframe, timeline | 🟡 phần mã xong; còn thử trên Windows | (nhánh `m4b`) |
| M5a | Âm thanh theo Channel: giải mã, mixer thread, WASAPI/Null, mute/âm lượng, crossfade theo transition, hình theo đồng hồ âm thanh, meter | 🟡 phần mã xong, test Linux (backend Null); còn thử WASAPI/đo lệch 30 phút trên Windows | (nhánh `m5a-audio`, **xếp chồng trên `m4b`**) |
| M5c | Điều khiển ngoài (HTTP/SSE, OSC), tự khởi động/mở lại project, khoá vận hành, hoàn tác nhập media, timeline nâng cao | 🟡 phần mã xong, test Linux; còn thử trên Windows | (nhánh `m5-control`, **xếp chồng trên `m4b`**) |

Quy mô mã: khoảng 11.000 dòng C++ (gồm test) · 833 kiểm tra tự động (709 cũ + 124 của M5a) · build được trên Linux và Windows (MSVC 2022, Qt 6.8.3, FFmpeg 9.1).

**Chưa được xác nhận trên phần cứng thật** (rủi ro đã biết, xem mục 5): nút Dừng hình trên Windows; fullscreen đúng màn LED;
4 Output 4K; chạy liên tục nhiều giờ.

## 2. Nguyên tắc làm việc
1. **Mỗi mốc một nhánh**, một Pull Request vào `main`; `main` luôn build được và qua `ctest`.
2. **Hoàn thành (Definition of Done)** cho mỗi PR: (a) `ctest` qua trên Linux; (b) build được trên Windows; (c) các mục thử tay liên quan
   trong `TEST-PLAN.md` đã chạy hoặc được ghi rõ là *chưa thử*; (d) cập nhật `SPEC.md` (trạng thái), `README.md`, và `PROJECT-FORMAT.md` nếu đổi định dạng;
   (e) tính năng mới có test tự động.
3. **Chạy thật trước, giao diện sau:** không thêm giao diện cho thứ engine chưa làm được.
4. **GPU chỉ chạm qua** `Compositor`, `GlHelpers`, upload texture của nguồn (để thay bằng RHI cho macOS sau này).
5. Mọi thay đổi từ UI đi qua `RenderEngine::post()`; UI không đọc dữ liệu render thread đang ghi.

## 3. Lộ trình

Kích cỡ: **S** vài ngày · **M** 1–2 tuần · **L** vài tuần (ước lượng thô, một người làm).

### M3c — Nghiệm thu thật và độ tin cậy (đề xuất làm **trước** M4) · Kích cỡ: M
*Mục tiêu:* biến các điều "chưa xác nhận" thành đã kiểm chứng, và thêm bảo vệ cơ bản trước khi thêm tính năng.
- [ ] **Cần phần cứng thật:** chạy checklist Windows T-FRZ, T-FS, T-LOSS, T-4K, T-SOAK (xem `TEST-PLAN.md`), ghi kết quả vào `SPEC.md`.
- [ ] Sửa lỗi phát hiện được (ví dụ cơ chế Dừng hình nếu không đúng trên Windows).
- [x] OUT-12: phát hiện mất màn/sai màn, báo đúng Output, ẩn cửa sổ khi mất màn (không nhảy sang màn khác), tự hiện lại đúng màn. Nhận diện màn theo **tên** (`screenName`).
- [x] OPS-02: watchdog render thread (cảnh báo giao diện + log; màn hình giữ hình cuối). *Chưa làm:* tự khởi động lại.
- [x] OUT-11: test pattern gắn theo Output (nút "Mẫu", "Mẫu thử tất cả Output").
- [x] OPS-04/05: log ra file có xoay vòng; thời gian khung p50/p99 trên thanh trạng thái.
- [x] Sửa lỗi an toàn: chỉ số màn vượt quá số màn trước đây dùng màn cuối (có thể đẩy hình LED lên màn operator); nay không hiển thị và báo lỗi.
- **Nghiệm thu:** 4 Output chạy ≥ 1 giờ, khung rơi < 0,1 %; rút một màn thì cảnh báo đúng Output; checklist Windows không còn mục "chưa thử" ở nhóm P0.
- **Phụ thuộc:** máy Windows có nhiều màn hình, tốt nhất có màn LED/bộ gửi tín hiệu thật.

### M4a — Effect, transition bổ sung, plug-in shader · Kích cỡ: L · **phần mã xong; còn thử trên phần cứng thật**
- [x] Giao diện `EffectDef` (1 texture vào → 1 ra, tham số) và chuỗi Effect **trên Layer** bằng FBO ping-pong (SCN-06). *Chưa làm:* Effect cấp Scene (dùng Effect trên một Layer phủ kín Scene thay thế tạm).
- [x] Effect dựng sẵn: làm mờ, phát sáng (glow), chỉnh màu (sáng/tương phản/bão hoà/sắc độ/gamma), tách RGB, pixelate, nhiễu hạt. *Thay đổi so với kế hoạch:* chỉnh màu thay cho LUT 1D (LUT cần texture phụ, chưa có trong SDK).
- [x] Transition mới: Push, Slide, Zoom, Luma wipe (5 mẫu), **Stinger** (video alpha, điểm cắt, phát lại từ đầu); easing chọn được (CHN-08). T-bar không hỗ trợ Stinger.
- [x] Trình nạp plug-in shader: GLSL + `plugin.json`, kiểm tra manifest/entry, nạp lại nóng, bật/tắt, ghi đè theo thư mục (PLG-01). Tài liệu: `PLUGIN-SDK.md`; 4 plug-in mẫu.
- [x] Giao diện tham số tự sinh (số, tick, danh sách, màu, điểm); Effect lưu trong project (giữ dữ liệu khi thiếu plug-in); hoàn tác được.
- [x] Biên dịch shader lỗi → Effect bị bỏ qua + báo lỗi + log, không crash; Transition lỗi → Fade (PLG-03, một phần).
- [x] Plugin Manager cơ bản: danh sách, bật/tắt, lý do lỗi, quét lại, mở thư mục (PLG-04, một phần). `PROJECT-FORMAT.md`: thêm `layers[].effects` (trường tuỳ chọn, vẫn `version` 1).
- [ ] **Chưa làm / chuyển sang sau:** plug-in native (DLL, C ABI) → sau M5; bảo vệ shader chạy vô hạn/quá nặng (không có cách an toàn trong cùng tiến trình; cần tiến trình riêng, M6); lưu transition đang chọn vào project; benchmark B5.
- **Nghiệm thu:** thêm một Effect vào Layer, chỉnh tham số, thấy đổi ở Program (✅ test); thả một file shader mới vào thư mục plug-in thì dùng được không cần khởi động lại (✅ test hot reload); shader lỗi không làm sập ứng dụng (✅ test); test render cho từng Effect/Transition (✅). **Còn lại:** chạy thử trên Windows (T-FX, T-TRX, T-PLG, T-STG) và đo B5.
- **Phụ thuộc:** M3c. **Rủi ro:** hiệu năng nhiều Effect ở 4K (chưa đo); shader plug-in nặng có thể treo GPU.

### M4b — Timeline và keyframe · Kích cỡ: L · **phần mã xong; còn thử trên Windows**
- [x] Xoay Layer (quanh tâm, chiều kim đồng hồ), crop Layer (theo hình đã xoay), blend mode (Normal/Add/Multiply/Screen) (SCN-04, SCN-05).
- [x] Đồng hồ Scene trong engine (`SceneClock`, tick mỗi khung trên render thread, atomic để UI đọc); thuộc tính Layer theo thời gian: track + keyframe, 5 kiểu nội suy (tuyến tính, êm đầu/cuối/hai đầu, giữ bậc).
- [x] Phát/Tạm dừng/Về 0/Tua; thời lượng, lặp; **Scene tự phát lại từ đầu khi lên Program** (CUT, bắt đầu TAKE/AUTO, bắt đầu kéo T-bar), có thể tắt.
- [x] Giao diện timeline: thước thời gian, playhead (kéo trên thước để tua), 6 làn (X, Y, Rộng, Cao, Alpha, Xoay), nút ◆ thêm/xoá key tại playhead, nhấp đúp thêm key, kéo key (làm tròn 0,05 s), Delete xoá, chuột phải đổi kiểu nội suy; **tự đặt keyframe khi sửa giá trị của thuộc tính đã có track**; mọi thao tác là một bước Hoàn tác (kéo liên tục một lần = một bước).
- [x] Lưu trong project (thuộc tính tuỳ chọn, vẫn `version` 1); đọc file viết tay có dữ liệu xấu không crash (key sắp xếp lại, giá trị hỏng bị bỏ).
- [x] **Cảnh báo an toàn:** timeline hiện "Scene đang ở PROGRAM" khi Scene đang chọn đang lên sóng (phát/tua/sửa tác động trực tiếp lên màn hình).
- [x] *(đã làm ở M5c)* zoom/cuộn timeline, chọn nhiều key và sao chép/dán, hoàn tác cho thuộc tính Scene (thời lượng/lặp/tự phát lại).
- [ ] **Chưa làm:** keyframe cho crop/blend/tham số Effect, đường cong Bézier, âm thanh theo timeline.
- **Nghiệm thu:** keyframe vị trí/alpha/xoay phát lại đúng thời điểm (✅ test render theo mốc thời gian, cả hình Program thật trên monitor); tua không làm giật Program (**chưa đo trên phần cứng**: T-ANI); hoàn tác được thao tác keyframe (✅ test).
- **Phụ thuộc:** M4a. **Rủi ro:** thao tác timeline trên Scene đang On Air tác động trực tiếp lên màn hình (đã có cảnh báo; khoá vận hành đã có ở M5c).

### M5a — Âm thanh theo Channel · Kích cỡ: L
- [x] Giải mã âm thanh FFmpeg (swresample → float32 stereo 48 kHz), mixer thread riêng, bus mỗi Channel, `AudioDevice` trừu tượng với WASAPI (Windows) + Null (test/Linux), chọn thiết bị ra và lưu trong project (AUD-01).
- [x] Mute/bật mỗi Channel, âm lượng Channel + Layer, crossfade theo transition (kể cả T-bar), đồng bộ hình theo đồng hồ âm thanh, meter, lệnh HTTP/OSC (AUD-02).
- [ ] **Chưa làm / chưa thử:** thử WASAPI thật + nghe bằng tai; đo lệch hình–tiếng 30 phút; thiết bị đa kênh/rút thiết bị; mỗi Channel một thiết bị riêng; WASAPI exclusive; macOS (CoreAudio); nhập file chỉ có âm thanh; chuẩn LUFS/true-peak; âm thanh theo timeline (keyframe âm lượng).
- **Nghiệm thu:** bật/tắt tiếng từng Channel; lệch hình–tiếng < 40 ms sau 30 phút; không vỡ tiếng khi chuyển cảnh. *(Trên Linux/Null đã đo −1…+27 ms qua nhiều vòng lặp và crossfade mượt qua meter; ba tiêu chí trên máy thật xem T-AUD-MUTE, T-AUD-SYNC, T-AUD-XFADE — **chưa thử**.)*

### M5b — NDI và camera · Kích cỡ: M–L
- [ ] NDI vào (SRC-06) và NDI ra như một `OutputSink`; camera/card capture (SRC-07).
- **Nghiệm thu:** nhận NDI, độ trễ < 3 khung; webcam hiện hình; NDI ra mở được bằng phần mềm khác.
- **Lưu ý:** cần NDI SDK (điều khoản đăng ký/ghi thương hiệu) — xem mục 5.

### M5c — Điều khiển ngoài và vận hành · Kích cỡ: M · **phần mã xong (đợt A), test Linux; còn thử trên Windows**
Làm trên nhánh `m5-control`, tách từ `m4b` (PR xếp chồng; chưa merge `m4b` vào `main`).
- [x] **P0-1** HTTP REST + **SSE** tự viết trên `QTcpServer` (chỉ QtNetwork): trạng thái, preview/cut/take/ftb, Đen/Dừng/Mẫu từng Output, BLACK ALL, phát/tạm dừng/tua Scene; mặc định tắt + `127.0.0.1`, LAN bắt buộc token, giới hạn, 4xx đúng mã, log từ chối (CTL-01). Chi tiết: `CONTROL-API.md`.
- [x] **P0-2** OSC/UDP: bộ phân tích tự viết (bundle lồng nhau, gói hỏng bị bỏ), địa chỉ phản chiếu API, bật/tắt + cổng trong cài đặt, mặc định tắt (CTL-01).
- [x] **P0-3** Khởi động cùng Windows (ghi `HKCU\...\Run`), nhớ và tự mở lại project gần nhất (tắt được; mất file → chạy bình thường + thông báo; dùng lại khôi phục autosave) (OPS-01).
- [x] **P0-4** Khoá vận hành (UI-07): vô hiệu điều khiển sửa, giữ điều khiển live, mở khoá bằng giữ nút 1 s, hiện trên thanh trạng thái, theo phiên; API tôn trọng (có cơ chế `edit` → 423).
- [x] **P1-5** Hoàn tác cho nhập media/tạo Scene từ Media Pool và thuộc tính Scene (PRJ-07, một phần).
- [x] **P1-6** Timeline: zoom/cuộn ngang, chọn nhiều keyframe, kéo nhóm, sao chép/dán (SCN-07).
- [ ] **Chưa làm / để đợt sau (cố ý loại khỏi đợt A):** **MIDI** (cần API Windows, không test được trên Linux), **NDI** và **camera** (M5b); chế độ Operator tối giản và Multiview (UI-06); đặt tham số Layer/Effect qua API; hoàn tác đổi tên/xoá Scene và thêm/xoá Output (chưa có giao diện); phím tắt tuỳ chỉnh.
- **Nghiệm thu:** điều khiển được từ điện thoại/Companion (✅ test socket thật trên Linux; **chưa thử điện thoại/Companion/LAN thật**); thao tác ngoài tuân theo cùng quy tắc an toàn như giao diện (✅ test: Preview không đổi Program, Đen đúng phạm vi, chỉ số sai → 404/400, khoá → 423).

### M6 — Hiệu năng và phát hành Windows · Kích cỡ: L
- [ ] Giải mã phần cứng D3D11VA, YUV→RGB bằng shader, HAP, upload bằng PBO/đồng bộ bất đồng bộ (SRC-04/05).
- [ ] Tối ưu 4 Output 4K; xem xét thread render theo từng nhóm Output khi cần.
- [ ] Trình cài đặt Windows, kiểm tra giấy phép phụ thuộc (FFmpeg LGPL, Qt LGPL), cờ tính năng cho gói Cơ bản/Thương mại (SPEC mục 5).
- **Nghiệm thu:** NFR-01/02/03 đạt trên máy mục tiêu; cài đặt sạch trên Windows chưa có Qt/FFmpeg.

### M7 — macOS · Kích cỡ: L
- [ ] Lớp RHI (ưu tiên QRhi/Metal) thay phần GPU hiện tại; kiểm chứng nhiều Output trên máy Mac thật.
- **Điều kiện bắt đầu:** bạn xác nhận cần macOS (hiện: cần nhưng chưa gấp).

## 4. Phụ thuộc giữa các mốc
`M3c → M4a → M4b` · `M3c → M5a/M5b/M5c` (độc lập nhau) · `M4a, M5* → M6` · `M6 → M7`.
M5a/M5b/M5c có thể đổi thứ tự theo nhu cầu thực tế của buổi diễn đầu tiên.

## 5. Rủi ro
| # | Rủi ro | Ảnh hưởng | Giảm thiểu |
|---|---|---|---|
| R1 | Dừng hình dựa vào việc không hoán đổi bộ đệm; hành vi trên Windows (DWM) chưa xác nhận | Dừng hình không đứng yên | T-FRZ trong M3c; nếu sai, giữ một bản sao khung cuối và vẽ lại |
| R2 | Nhiều Output cùng tần số quét nhưng không đồng bộ tuyệt đối, có thể xé hình/lệch khung | Chất lượng hình trên LED ghép | Một render thread, đo bằng T-4K; cân nhắc card có Genlock khi ghép lớn |
| R3 | Băng thông GPU/VRAM với nhiều Output 4K | Rớt khung | Benchmark sớm (M3c), render theo độ phân giải Output chọn lọc, giải mã phần cứng (M6) |
| R4 | Plug-in bên thứ ba treo/crash | Sập cả show | Shader plug-in là đường chính, native có hạn chạy và vô hiệu hoá, tách tiến trình sau |
| R5 | Chuyển Qt/FFmpeg bản mới làm vỡ build (đã gặp: Qt 6.8 yêu cầu `Q_OBJECT`) | Chặn phát triển | Build thử Windows trước mỗi merge; ghi phiên bản đã dùng trong README |
| R6 | OpenGL bị Apple ngừng phát triển | Chặn macOS | Giữ ranh giới GPU hẹp (nguyên tắc 4), làm M7 qua RHI |
| R7 | Điều khoản NDI SDK và giấy phép thương mại của Qt khi bán gói Thương mại | Pháp lý | Xác nhận trước M5b/M6; giữ liên kết động LGPL |
| R8 | Phạm vi lớn so với một người làm | Chậm tiến độ | Làm theo mốc nhỏ, P0 trước; mỗi PR có thể phát hành độc lập |
| R9 | Chưa có máy LED/bộ gửi tín hiệu thật để kiểm tra trong môi trường phát triển | Lỗi chỉ lộ ra ngoài hiện trường | M3c bắt buộc có buổi thử trên phần cứng thật |

## 6. Cách làm việc theo mốc (quy trình)
1. Tách nhánh từ `main` (`m3c`, `m4a`…); viết test trước/cùng với mã.
2. Chạy `ctest` (Linux: `xvfb-run -a ctest`); build và chạy trên Windows theo `TEST-PLAN.md`.
3. Tạo PR, ghi rõ *đã kiểm chứng* và *chưa kiểm chứng*; chờ xác nhận rồi merge.
4. Sau merge: cập nhật bảng trạng thái ở mục 1 và `SPEC.md`.
