# LumaCast — Kế hoạch phát triển (PLAN)

**Cập nhật:** sau M3b · Yêu cầu: `SPEC.md` · Thiết kế: `KienTruc.md` · Kiểm thử: `TEST-PLAN.md`

## 1. Hiện trạng
| Mốc | Nội dung | Trạng thái | PR |
|---|---|---|---|
| M1 | Lõi render GPU, Output nhiều cửa sổ, Scene/Layer, Channel Program/Preview, Cut/Fade/Dip/Wipe, nguồn ảnh/màu/video | ✅ xong | (trong #1) |
| M2 | Project JSON, hiệu chỉnh màu, video wall, nhiều Layer, video dọc | ✅ xong | #1 |
| M3a | Giao diện Live Control: monitor, Scene list, Media Pool, Outputs, Inspector, FTB, Đen/Dừng, tự lưu | ✅ xong | #1 |
| M3b | Hoàn tác/làm lại, T-bar, AUTO, panel Layer kéo-thả | ✅ xong | #2 |

Quy mô mã: khoảng 3.800 dòng C++ · 109 kiểm tra tự động · build được trên Linux và Windows (MSVC 2022, Qt 6.8.3, FFmpeg 9.1).

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
- [ ] Chạy checklist Windows T-FRZ, T-FS, T-4K, T-SOAK (xem `TEST-PLAN.md`), ghi kết quả vào `SPEC.md`.
- [ ] Sửa lỗi phát hiện được (ví dụ cơ chế Dừng hình nếu không đúng trên Windows).
- [ ] OUT-12: phát hiện mất thiết bị/mất màn, báo Output nào bị ảnh hưởng, cố gắng tạo lại cửa sổ.
- [ ] OPS-02: watchdog render thread (cảnh báo, giữ hình cuối, ghi log).
- [ ] OUT-11: chế độ test pattern gắn theo Output (lưới, đếm điểm ảnh, số nhận diện màn).
- [ ] Ghi log ra file (có xoay vòng) và đo thời gian khung (p50/p99) hiển thị ở thanh trạng thái.
- **Nghiệm thu:** 4 Output chạy ≥ 1 giờ, khung rơi < 0,1 %; rút một màn thì cảnh báo đúng Output; checklist Windows không còn mục "chưa thử" ở nhóm P0.
- **Phụ thuộc:** máy Windows có nhiều màn hình, tốt nhất có màn LED/bộ gửi tín hiệu thật.

### M4a — Effect, transition bổ sung, plug-in shader · Kích cỡ: L
- [ ] Giao diện nội bộ `Effect` (1 texture vào, 1 ra, tham số), chuỗi Effect trên Layer/Scene bằng FBO ping-pong (SCN-06).
- [ ] Effect dựng sẵn: blur, glow/bloom, chỉnh màu (LUT 1D), RGB split, pixelate, noise.
- [ ] Transition mới: Stinger (video alpha, điểm cắt), Luma wipe, Push/Slide, Zoom; easing chọn được (CHN-08).
- [ ] Trình nạp plug-in shader: GLSL + `plugin.json` (loại, tham số, mặc định), theo tinh thần ISF; nạp lại nóng (PLG-01).
- [ ] Giao diện tham số tự sinh từ manifest (slider, màu, bật/tắt, danh sách chọn); lưu tham số vào project.
- [ ] Bảo vệ plug-in: biên dịch shader lỗi → vô hiệu hoá + báo lỗi, không crash (PLG-03).
- [ ] Plugin Manager cơ bản (danh sách, bật/tắt) (PLG-04); cập nhật `PROJECT-FORMAT.md` (bản 2 hoặc trường tuỳ chọn).
- **Nghiệm thu:** thêm một Effect vào Layer, chỉnh tham số, thấy đổi ở Program; thả một file shader mới vào thư mục plug-in thì dùng được không cần khởi động lại; shader lỗi không làm sập ứng dụng; test render cho từng Effect/Transition.
- **Phụ thuộc:** M3c (nền ổn định). **Rủi ro:** hiệu năng nhiều Effect ở 4K → đo bằng T-4K.

### M4b — Timeline và keyframe · Kích cỡ: L
- [ ] Thêm xoay và crop cho Layer (SCN-04), blend mode (SCN-05) — là thuộc tính cần animate.
- [ ] Đồng hồ Scene trong engine; thuộc tính Layer theo thời gian (track + keyframe, nội suy tuyến tính/ease).
- [ ] Điều khiển phát (Play/Pause/Loop) theo Scene; giao diện timeline (thêm/kéo keyframe, scrub).
- [ ] Lưu keyframe vào project (nâng phiên bản định dạng nếu cần).
- **Nghiệm thu:** keyframe vị trí/alpha/xoay phát lại đúng thời điểm (test render theo mốc thời gian); scrub không làm giật Program; hoàn tác được thao tác keyframe.
- **Phụ thuộc:** M4a (Effect có thể là thuộc tính animate).

### M5a — Âm thanh theo Channel · Kích cỡ: L
- [ ] Giải mã âm thanh FFmpeg, mixer thread riêng, bus mỗi Channel, WASAPI ra thiết bị chọn được (AUD-01).
- [ ] Mute/bật mỗi Channel, âm lượng, crossfade theo transition, đồng bộ hình theo đồng hồ âm thanh, meter (AUD-02).
- **Nghiệm thu:** bật/tắt tiếng từng Channel; lệch hình–tiếng < 40 ms sau 30 phút; không vỡ tiếng khi chuyển cảnh.

### M5b — NDI và camera · Kích cỡ: M–L
- [ ] NDI vào (SRC-06) và NDI ra như một `OutputSink`; camera/card capture (SRC-07).
- **Nghiệm thu:** nhận NDI, độ trễ < 3 khung; webcam hiện hình; NDI ra mở được bằng phần mềm khác.
- **Lưu ý:** cần NDI SDK (điều khoản đăng ký/ghi thương hiệu) — xem mục 5.

### M5c — Điều khiển ngoài và vận hành · Kích cỡ: M
- [ ] HTTP/WebSocket, OSC, MIDI: chọn Scene, TAKE/CUT, Đen, FTB, đặt tham số (CTL-01).
- [ ] Tự khởi động cùng Windows, mở lại project gần nhất, chế độ Operator tối giản, khoá thao tác khi On Air (OPS-01, UI-06/07).
- **Nghiệm thu:** điều khiển được từ điện thoại/Companion; thao tác ngoài tuân theo cùng quy tắc an toàn như giao diện.

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
