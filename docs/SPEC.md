# LumaCast — Đặc tả yêu cầu (SPEC)

**Phiên bản tài liệu:** 1.1 · **Trạng thái mã:** sau M5a (nhánh `m5a-audio`, xếp chồng trên `m4b` đã gồm M5c) · **Nền tảng mục tiêu:** Windows 10/11 x64 (macOS sau)

Tài liệu này trả lời *"phần mềm phải làm gì và thế nào là đạt"*. Thiết kế kỹ thuật nằm ở `KienTruc.md`, lộ trình ở `PLAN.md`,
định dạng file ở `PROJECT-FORMAT.md`, cách kiểm thử ở `TEST-PLAN.md`.

**Quy ước.** Ưu tiên: **P0** phải có cho bản dùng thật đầu tiên · **P1** nên có · **P2** để sau.
Trạng thái: ✅ xong và có test · 🟡 xong một phần / chưa xác nhận trên phần cứng thật · ⬜ chưa làm.
"Test tự động" = chạy được bằng `ctest`; "Thử tay" = mục trong `TEST-PLAN.md` mục 3.

---

## 1. Mục đích và phạm vi

LumaCast là phần mềm điều khiển nội dung thời gian thực cho màn hình LED sân khấu/sự kiện, kiểu vMix: một máy tính điều khiển
**nhiều kênh (Channel)** và **nhiều đầu ra (Output) với độ phân giải khác nhau**, có chuyển cảnh, hiệu ứng và plug-in.
Mỗi Output là một cửa sổ toàn màn hình trên màn hình hệ điều hành, đưa tín hiệu HDMI/DP tới bộ gửi tín hiệu LED.

**Người dùng chính:** operator LED/visual vận hành trong buổi diễn; kỹ thuật viên cấu hình trước show.
**Mô hình sử dụng:** cấu hình trước (Scene, Output, mapping, màu) → vận hành live (chọn Scene, chuyển cảnh, Đen/Dừng hình).

**Ngoài phạm vi bản đầu:** đồng bộ nhiều máy tính, SDI, giao thức riêng của bộ xử lý LED (Novastar/Colorlight qua mạng),
edge-blending/warp cho máy chiếu, marketplace plug-in.

## 2. Thuật ngữ
Source · Layer · Scene · Channel · Output · Mapping · Transition · Effect · Plug-in: xem `KienTruc.md` mục 1.
**Program** = hình đang phát; **Preview** = hình chuẩn bị; **TAKE/CUT** = chuyển Preview sang Program có/không hiệu ứng;
**FTB** = Fade-To-Black; **Wall** = một canvas rộng chia cho nhiều Output.

---

## 3. Yêu cầu chức năng

### 3.1 Nguồn (SRC)
| ID | Yêu cầu | Ưu tiên | TT | Nghiệm thu |
|---|---|---|---|---|
| SRC-01 | Nguồn ảnh (PNG/JPG/BMP/GIF/WebP), màu trơn, test pattern có lưới/thanh màu/viền | P0 | ✅ | Test tự động (ảnh, màu, snapshot) |
| SRC-02 | Nguồn video qua FFmpeg, phát theo PTS, tự lặp | P0 | ✅ | Test tự động (nếu có `ffmpeg` để tạo video thử) + thử tay T-VID |
| SRC-03 | Video dọc/quay bằng điện thoại hiện đúng chiều (cờ xoay); ảnh theo EXIF | P0 | ✅ | Test tự động so với FFmpeg (90/180/270) + đã thử tay trên Windows |
| SRC-04 | Giải mã phần cứng (D3D11VA) và đổi màu YUV→RGB bằng shader | P1 | ⬜ | Benchmark 4K ≥ 60 fps, CPU thấp hơn bản CPU |
| SRC-05 | Codec HAP | P1 | ⬜ | Phát HAP 4K không rơi khung |
| SRC-06 | NDI vào | P1 | 🟡 | Nhận stream NDI (hình + tiếng), độ trễ < 3 khung. **Mã xong, test tự động bằng backend giả + thư viện NDI giả; chưa thử với NDI Runtime thật** (T-NDI-IN, T-NDI-LOSS) |
| SRC-07 | Camera/card capture | P1 | 🟡 | Hiện hình từ webcam/card capture. **Mã Media Foundation viết xong, mới kiểm tra cú pháp bằng mingw, chưa chạy trên Windows**; test tự động bằng camera giả (T-CAM, T-CAM-HOT). Chưa có: âm thanh từ capture, Linux/macOS |
| SRC-08 | Chữ/đồ hoạ (CG), web, chuỗi ảnh | P2 | ⬜ | — |

### 3.2 Scene và Layer (SCN)
| ID | Yêu cầu | Ưu tiên | TT | Nghiệm thu |
|---|---|---|---|---|
| SCN-01 | Scene có độ phân giải riêng và nhiều Layer | P0 | ✅ | Test tự động |
| SCN-02 | Layer: vị trí, kích thước, độ mờ (alpha), kiểu fit (Stretch/Fit/Fill), ẩn/hiện | P0 | ✅ | Test tự động |
| SCN-03 | Đổi thứ tự Layer (kéo-thả hoặc nút), thêm/xoá, hoàn tác/làm lại | P0 | ✅ | Test tự động |
| SCN-04 | Xoay Layer (độ, quanh tâm layer, đúng trên Scene không vuông) và crop Layer (tính theo hình đã xoay, đúng cả với video điện thoại xoay 90/180/270; kết hợp Fit/Fill) | P1 | ✅ | Test tự động (điểm ảnh; 4 nguồn xoay × crop) |
| SCN-05 | Blend mode: Normal, Add, Multiply, Screen (kể cả opacity) | P1 | 🟡 | Test tự động so với giá trị tính tay. Chưa có Overlay, Soft light… |
| SCN-06 | Chuỗi Effect trên **Layer** (blur, glow, chỉnh màu, RGB split, pixelate, noise): thêm/gỡ/đổi thứ tự/bật-tắt, tham số tự sinh, hoàn tác, lưu trong project | P0 | ✅ | Test tự động (từng Effect, hướng ảnh, chuỗi, UI). **Chưa có Effect cấp Scene** (xem PLAN) |
| SCN-07 | Timeline và keyframe cho thuộc tính Layer (vị trí X/Y, rộng, cao, alpha, xoay): đồng hồ Scene (phát/tạm dừng/về 0/tua, lặp, thời lượng, tự phát lại khi lên Program), 5 kiểu nội suy, thêm/dời/xoá keyframe bằng chuột hoặc nút ◆, tự đặt keyframe khi sửa giá trị, hoàn tác | P1 | ✅ | Test tự động (nội suy, đồng hồ, điểm ảnh ở từng thời điểm, chuột thật trên timeline, hoàn tác). Đã thêm ở M5c: zoom 1–64× và cuộn ngang, chọn nhiều key (Ctrl/Shift/khung chọn), kéo nhóm, Ctrl+C/Ctrl+V (dán tại playhead), xoá/đổi nội suy cả nhóm, hoàn tác thuộc tính Scene (test tự động `lumacast_m5c_tests`; **chưa thử bằng tay trên Windows**, T-TLZ). Chưa: keyframe cho crop/tham số Effect, đường cong Bézier |
| SCN-08 | Mask, chroma key, nhóm Layer | P2 | ⬜ | — |

### 3.3 Channel và chuyển cảnh (CHN)
| ID | Yêu cầu | Ưu tiên | TT | Nghiệm thu |
|---|---|---|---|---|
| CHN-01 | Mỗi Channel có Preview và Program độc lập; nhiều Channel chạy song song | P0 | ✅ | Test tự động |
| CHN-02 | CUT: chuyển tức thì; sau chuyển Program ↔ Preview hoán đổi | P0 | ✅ | Test tự động |
| CHN-03 | TAKE với Fade, Dip-to-black, Wipe 4 hướng; thời lượng 0,1–10 s | P0 | ✅ | Test tự động (pixel ở giữa chặng) |
| CHN-04 | T-bar kéo tay: kéo hết = hoàn tất, về 0 = huỷ; CUT giữa chừng không kẹt | P0 | ✅ | Test tự động (engine + UI) + T-TBAR |
| CHN-05 | AUTO: bật thì bấm Scene = chuyển thẳng lên Program; tắt thì chỉ vào Preview | P1 | ✅ | Test tự động |
| CHN-06 | FTB: làm mờ dần Program về đen, bấm lại để trở lại | P0 | ✅ | Test tự động |
| CHN-07 | Preview **không bao giờ** tự đổi Program | P0 | ✅ | Test tự động |
| CHN-08 | Transition bổ sung: Push, Slide, Zoom, Luma wipe (5 mẫu), **Stinger** (video alpha, điểm cắt, tự phát lại từ đầu); easing chọn được (4 kiểu) | P1 | ✅ | Test tự động. T-bar kéo tay không hỗ trợ Stinger (rơi về Fade); thử tay Stinger với video alpha thật chưa làm (T-STG) |
| CHN-09 | Transition do plug-in cung cấp (kèm tham số) | P1 | ✅ | Test tự động |

### 3.4 Output và mapping (OUT)
| ID | Yêu cầu | Ưu tiên | TT | Nghiệm thu |
|---|---|---|---|---|
| OUT-01 | Nhiều Output, mỗi cái có độ phân giải riêng (tối thiểu 1920×1080 và 3840×2160), mục tiêu 4 Output | P0 | 🟡 | Đã chạy 2 Output cửa sổ trên Windows; chưa thử 4 Output / 4K (T-4K) |
| OUT-02 | Scale Fit/Fill/Stretch/1:1, xoay 0/90/180/270, crop vùng của Channel | P0 | ✅ | Test tự động |
| OUT-03 | Video wall: chia một canvas rộng cho N Output (`--wall CxR`) | P0 | ✅ | Test tự động + ảnh chụp thử |
| OUT-04 | Bù khe (bezel/pixel gap) giữa các màn | P1 | ⬜ | — |
| OUT-05 | Hiệu chỉnh màu mỗi Output: sáng, tương phản, gamma, gain R/G/B | P0 | ✅ | Test tự động; thử tay với test chart T-CAL |
| OUT-06 | LUT 3D, giới hạn độ sáng | P2 | ⬜ | — |
| OUT-07 | **Đen** theo từng Output và BLACK ALL; Đen luôn thắng Dừng hình | P0 | ✅ | Test tự động |
| OUT-08 | **Dừng hình** theo từng Output (giữ khung cuối), Output khác vẫn chạy | P0 | 🟡 | Test tự động trên Linux; **chưa xác nhận trên Windows** (T-FRZ) |
| OUT-09 | Toàn màn hình đúng màn hình đã chọn, không xé hình; **không bao giờ hiện lên màn khác** (nhận diện màn theo tên, chỉ số vượt quá số màn = không hiển thị) | P0 | 🟡 | Test tự động logic + ẩn cửa sổ; **chưa thử trên màn LED thật** (T-FS) |
| OUT-10 | Trạng thái từng Output: hiển thị/không hiển thị, FPS thật, Đen/Dừng | P0 | ✅ | Test tự động |
| OUT-11 | Test pattern riêng theo Output (lưới, thanh màu, thước điểm ảnh, toạ độ 4 góc, tên màn), bật từng Output hoặc tất cả | P1 | ✅ | Test tự động (nội dung cửa sổ thật); thử tay T-PAT |
| OUT-12 | Phát hiện mất màn/sai màn/cửa sổ không hiển thị, báo đúng Output (giao diện + log), ẩn cửa sổ khi mất màn, tự hiện lại đúng màn khi màn quay lại | P0 | 🟡 | Test tự động (logic, ẩn cửa sổ); **chưa thử rút màn thật** (T-LOSS) |
| OUT-13 | Output dạng khác: NDI ra, ghi file, DMX/Art-Net | P1 | 🟡 | Qua `OutputSink`. **NDI ra** có (Program của một Channel, tên cấu hình được, kèm âm thanh bus); test bằng backend giả, **chưa mở được bằng phần mềm NDI thật** (T-NDI-OUT). Ghi file, DMX/Art-Net: chưa làm |

### 3.5 Project (PRJ)
| ID | Yêu cầu | Ưu tiên | TT | Nghiệm thu |
|---|---|---|---|---|
| PRJ-01 | Lưu/mở project JSON; media theo đường dẫn tương đối; ghi nguyên tử | P0 | ✅ | Test tự động |
| PRJ-02 | File hỏng/sai tham chiếu → báo lỗi rõ, không crash | P0 | ✅ | Test tự động |
| PRJ-03 | Tự lưu mỗi 60 s; hỏi khôi phục khi bản tự lưu mới hơn | P0 | ✅ | Test tự động (tạo/đọc); thử tay khôi phục T-REC |
| PRJ-04 | Nhập media vào project khi đang chạy (tạo Scene) | P0 | ✅ | Test tự động |
| PRJ-05 | Mở project khác khi đang chạy | P1 | 🟡 | Hiện khởi động lại tiến trình với project mới |
| PRJ-06 | Hoàn tác/làm lại cho Layer và Output | P0 | ✅ | Test tự động |
| PRJ-07 | Hoàn tác cho nhập media, đổi tên, thêm/xoá Scene và Output | P1 | 🟡 | **Đã làm (M5c):** nhập media, tạo Scene từ Media Pool, thuộc tính Scene (thời lượng/lặp/tự phát lại) — test tự động, có xử lý Scene đang On Air. **Chưa làm:** đổi tên, xoá Scene, thêm/xoá Output (hiện chưa có giao diện cho các thao tác này) |
| PRJ-08 | Gói project (gom media), nâng cấp phiên bản định dạng | P1 | ⬜ | Xem `PROJECT-FORMAT.md` mục 5 |

### 3.6 Giao diện vận hành (UI)
| ID | Yêu cầu | Ưu tiên | TT | Nghiệm thu |
|---|---|---|---|---|
| UI-01 | Monitor Preview (viền xanh lá) và Program (viền đỏ, nhãn ON AIR), cập nhật ~15 Hz | P0 | ✅ | Test tự động |
| UI-02 | Danh sách Scene (đánh dấu PGM/PVW), Media Pool có ảnh thu nhỏ, Outputs, Inspector | P0 | ✅ | Test tự động |
| UI-03 | Phím tắt: Space = TAKE, Enter = CUT, B = BLACK ALL, 1–9 chọn Preview, Ctrl+S/O/Z | P0 | ✅ | Thử tay T-KEY |
| UI-04 | Giao diện tối; bố cục dock lưu/khôi phục; hỏi xác nhận khi thoát | P0 | ✅ | Thử tay |
| UI-05 | Thanh trạng thái: GPU, FPS, p50/p99 thời gian khung, khung rơi, số Output hiển thị, cảnh báo treo | P0 | ✅ | Test tự động |
| UI-06 | Multiview nhiều Channel dạng lưới; chế độ Operator tối giản | P1 | ⬜ | — |
| UI-07 | Phím tắt tuỳ chỉnh; khoá thao tác khi On Air (operator lock) | P1 | 🟡 | **Khoá vận hành đã làm (M5c):** vô hiệu Inspector/sửa timeline/nhập media/Plugin/Hoàn tác/mở project/cài đặt/đóng cửa sổ, giữ điều khiển live; mở khoá bằng giữ nút 1 s; theo phiên, hiện trên thanh trạng thái — test tự động Linux, **chưa thử tay** (T-LOCK). **Chưa làm:** phím tắt tuỳ chỉnh |
| UI-08 | Quản lý Output/canvas bằng giao diện (kéo-thả màn hình, wizard tạo project) | P1 | ⬜ | Hiện chỉnh qua Inspector và dòng lệnh |
| UI-09 | Ngôn ngữ: tiếng Việt; thêm tiếng Anh | P2 | 🟡 | Hiện chỉ tiếng Việt |

### 3.7 Plug-in (PLG)
| ID | Yêu cầu | Ưu tiên | TT | Nghiệm thu |
|---|---|---|---|---|
| PLG-01 | Shader plug-in (GLSL + `plugin.json`) cho Effect và Transition; tham số tự sinh giao diện; nạp lại nóng; 4 plug-in mẫu + `PLUGIN-SDK.md` | P0 | ✅ | Test tự động (quét, kiểm tra manifest, hot reload, bật/tắt, ghi đè) + thử tay T-PLG |
| PLG-02 | Plug-in native (DLL, C ABI có phiên bản) cho Source/Effect/Output filter | P1 | ⬜ | Hoãn sang sau M5 (xem PLAN) |
| PLG-03 | Plug-in lỗi hoặc chạy quá hạn bị vô hiệu hoá, không làm sập ứng dụng | P0 | 🟡 | Lỗi manifest/biên dịch: ✅ có test, không crash, giữ dữ liệu project. **Shader chạy vô hạn/quá nặng: chưa bảo vệ được** (có thể treo GPU; watchdog chỉ báo) |
| PLG-04 | Plugin Manager (danh sách, bật/tắt, phiên bản, tác giả, lý do lỗi, quét lại, mở thư mục) | P1 | 🟡 | Test tự động. Chưa có: cài từ file, cập nhật/gỡ cài đặt, kiểm tra tương thích GPU |

### 3.8 Âm thanh, điều khiển ngoài, vận hành (AUD / CTL / OPS)
| ID | Yêu cầu | Ưu tiên | TT | Nghiệm thu |
|---|---|---|---|---|
| AUD-01 | Âm thanh theo Channel, bật/tắt từng Channel, chọn thiết bị ra (WASAPI) | P0 | 🟡 | **Đã làm (M5a):** giải mã FFmpeg (swresample → float32 stereo 48 kHz), mixer thread riêng, mỗi Channel một bus, ring lock-free, lớp trừu tượng `AudioDevice` (backend **WASAPI** shared/event-driven cho Windows — chỉ Win32/COM, không Qt Multimedia — và backend **Null**), chọn thiết bị trong dock *Âm thanh* và lưu vào project (`audio.device`), nút Tắt tiếng mỗi Channel. Nguồn không có tiếng thì im lặng, không lỗi. Test tự động trên Linux bằng backend Null (`lumacast_audio_tests`, `lumacast_audio_ui_tests`). **Chưa thử:** WASAPI thật, thiết bị đa kênh/rút thiết bị, nghe bằng tai (T-AUD-DEV, T-AUD-MUTE, T-AUD-HOT). **Chưa làm:** mỗi Channel ra một thiết bị riêng (hiện mix chung ra một thiết bị), macOS, WASAPI exclusive, file chỉ có âm thanh (chưa nhập được) |
| AUD-02 | Âm lượng, đồng bộ hình–tiếng theo đồng hồ âm thanh, crossfade theo transition, meter | P1 | 🟡 | **Đã làm (M5a):** âm lượng Channel (0–150% trên UI, 0–2 trong API) và âm lượng Layer (Inspector, hoàn tác được); crossfade Program→Preview theo **đúng tiến độ transition kể cả easing và T-bar** (Dip: im ở giữa; Stinger: cắt cứng ở điểm cắt, tiếng stinger đi kèm; FTB kéo cả tiếng), đổi hệ số nội suy tuyến tính theo từng mẫu (không tiếng lách); **hình chạy theo đồng hồ âm thanh** khi mixer đang lấy tiếng của nguồn (lặp nối liền không ngắt, mốc hình tính lại từ mẫu âm thanh đầu mỗi vòng nên không tích luỹ lệch), tự rơi về đồng hồ tường khi không có tiếng/mixer/âm thanh đứng; `restart()` im lập tức rồi phát lại từ đầu; meter peak/RMS L/R mỗi Channel và master; điều khiển ngoài mute/âm lượng (HTTP + OSC, lệnh live, chạy cả khi khoá vận hành). Test tự động trên Linux: lệch hình–tiếng đo được −1…+27 ms qua nhiều vòng lặp (clip 50 fps; tiêu chí test −15…+45 ms). **Chưa kiểm chứng:** lệch < 40 ms sau **30 phút** trên Windows với WASAPI thật (T-AUD-SYNC), nghe crossfade bằng tai (T-AUD-XFADE). Meter chưa chỉnh theo chuẩn LUFS/true-peak |
| CTL-01 | Điều khiển ngoài: HTTP/WebSocket, OSC, MIDI (chọn Scene, TAKE/CUT, Đen, đặt tham số) | P1 | 🟡 | **HTTP REST + SSE và OSC/UDP đã làm (M5c)**, xem `CONTROL-API.md`: mặc định tắt và chỉ `127.0.0.1`; LAN phải bật rõ + token bắt buộc; giới hạn kích thước/thời gian; chống CSRF/DNS rebinding; log từ chối. Test tự động bằng socket thật trên Linux (`lumacast_control_tests`). **Dùng SSE thay WebSocket** (lý do: `KienTruc.md` mục 15). **Chưa làm:** MIDI, đặt tham số Layer/Effect qua API (chưa có endpoint sửa cấu hình). **Chưa thử** trên Windows/LAN/điện thoại/Companion thật (T-CTL, T-OSC) |
| OPS-01 | Tự khởi động cùng Windows, tự mở lại project gần nhất, ghi log | P1 | 🟡 | **Đã làm (M5c):** tuỳ chọn ghi `HKCU\...\Run` (logic test bằng QSettings giả), tự mở lại project gần nhất (tắt được; file mất/hỏng → chạy bình thường + thông báo; dùng cơ chế khôi phục autosave), log đã có từ M3c. **Chưa thử** ghi registry thật/khởi động cùng Windows (T-AUTO) |
| OPS-02 | Watchdog: render thread treo (> 2 s) thì cảnh báo trên giao diện + log; màn hình giữ hình cuối | P0 | 🟡 | Test tự động (chặn render thread); **chưa tự khởi động lại** |
| OPS-04 | Ghi log ra file (`%AppData%/LumaCast/logs`, xoay vòng 2 MB × 3) | P0 | ✅ | Test tự động |
| OPS-05 | Thời gian render mỗi khung p50/p99 trên thanh trạng thái | P1 | ✅ | Test tự động |
| OPS-03 | Dự phòng (fallback) cấu hình trước cho nguồn mất | P1 | ⬜ | — |

---

## 4. Yêu cầu phi chức năng

| ID | Yêu cầu | Chỉ tiêu | Hiện tại |
|---|---|---|---|
| NFR-01 | Nhịp render | 60 fps ổn định, khung rơi < 0,1 % sau 1 giờ | Đo trên Windows (AMD Radeon, 2 Output cửa sổ): 60 fps. Chưa đo dài hạn |
| NFR-02 | Quy mô | 4 Output 1080p, hoặc 2 Output 4K + 2 Output 1080p | Chưa đo (T-4K) |
| NFR-03 | Độ trễ thao tác | Từ bấm CUT đến đổi hình ≤ 2 khung | Chưa đo |
| NFR-04 | An toàn khi live | Preview không đổi Program; Đen có phạm vi rõ; xác nhận khi thoát; FTB | Đáp ứng (UI-04, CHN-07, OUT-07) |
| NFR-05 | Phục hồi | Mất điện: mất tối đa 60 s thay đổi chưa lưu; mở lại khôi phục được | Đáp ứng cơ chế (PRJ-03) |
| NFR-06 | Độ bền | File hỏng/media thiếu không làm crash | Đáp ứng với project JSON (PRJ-02) |
| NFR-07 | Tương thích | Windows 10/11 x64; OpenGL 3.3 core; Qt ≥ 6.4 (đã dùng 6.4, 6.8.3); FFmpeg ≥ 6.1 (đã dùng 6.1, 9.1) | Đáp ứng |
| NFR-08 | Giấy phép phụ thuộc | FFmpeg bản LGPL liên kết động; Qt LGPL liên kết động hoặc giấy phép thương mại | Đáp ứng (vcpkg `ffmpeg` mặc định) |
| NFR-09 | Kiểm thử | Mọi tính năng mới có test tự động; PR phải qua `ctest` | 833 kiểm tra tự động (9 bộ) |
| NFR-10 | Khả năng mở rộng | GPU chỉ chạm qua `Compositor`, `GlHelpers` và upload texture để thay bằng RHI (macOS) | Giữ nguyên tắc |

---

## 5. Phân gói dự kiến (đề xuất, chưa chốt)

Bạn từng nói nếu bán sẽ có gói cơ bản và gói thương mại nâng cao. Đề xuất ban đầu, **cần bạn quyết định** trước khi làm khoá tính năng:

| Tính năng | Cơ bản | Thương mại |
|---|---|---|
| Số Output | ≤ 2 | không giới hạn (mục tiêu 4+) |
| Transition và Effect dựng sẵn | có | có |
| Plug-in shader / native | không | có |
| Video wall, hiệu chỉnh màu, T-bar | có | có |
| NDI, camera/capture | không | có |
| Điều khiển ngoài (HTTP/OSC/MIDI) | không | có |
| Timeline / keyframe | không | có |
Cơ chế khoá nên là cờ tính năng đọc từ giấy phép, tách khỏi lõi (xem `PLAN.md`, mục Phát hành).

### 5.1 NDI® — điều khoản cần xác nhận **trước khi phát hành** (M5b)
NDI là công nghệ và thương hiệu đã đăng ký của Vizrt NDI AB. Cách LumaCast dùng: **không** chứa và **không** liên kết tĩnh bất kỳ header/thư viện nào của NDI SDK; runtime NDI được nạp **động** lúc chạy (`QLibrary`, `Processing.NDI.Lib.x64.dll` / `libndi`) và tự khai báo tối thiểu các hàm cần dùng. Không có runtime thì NDI tự tắt và báo "Chưa cài NDI Runtime"; mọi tính năng khác chạy bình thường. Các điểm **người dùng cần tự đọc điều khoản NDI mới nhất rồi quyết định** (tôi chưa xác minh nguyên văn điều khoản):
1. **Đăng ký/giấy phép:** dùng NDI trong sản phẩm bán ra thường yêu cầu đăng ký với NDI (ndi.video) và chấp nhận giấy phép SDK/Advanced SDK; cần xác nhận gói nào áp dụng cho gói Thương mại.
2. **Ghi thương hiệu:** hiển thị "NDI®" và dòng ghi nhận bản quyền theo hướng dẫn thương hiệu của NDI (màn hình giới thiệu, website, tài liệu) — hiện giao diện **chưa** có dòng này, cần thêm trước khi phát hành.
3. **Không phân phối lại runtime:** bộ cài LumaCast **không** được kèm `Processing.NDI.Lib.x64.dll` trừ khi giấy phép cho phép; hướng dẫn người dùng tự cài NDI Tools/Runtime miễn phí từ ndi.video (đây cũng là lý do thiết kế nạp động).
4. **ABI chưa đối chiếu:** các khai báo hàm/struct trong `src/live/NdiRuntime.cpp` viết theo header công khai từ trí nhớ, chưa đối chiếu với header chính thức của phiên bản SDK bạn dùng; cần đối chiếu + thử T-NDI-IN/T-NDI-OUT trước khi tin vào nó.
Biến môi trường `LUMACAST_NDI_DISABLE=1` tắt hẳn NDI (dùng khi nghi ngờ runtime làm sập ứng dụng).

## 6. Câu hỏi mở
1. Số Output tối đa và độ phân giải tối đa mục tiêu của bản đầu? (đang giả định 4 Output; 1080p và 4K)
2. Âm thanh, NDI, camera có cần ngay trong bản đầu để dùng thật không, hay chỉ hình trước?
3. Có cần điều khiển trực tiếp bộ gửi tín hiệu LED (Novastar, Colorlight) qua mạng không, hay chỉ HDMI/DP?
4. GPU và cấu hình máy tối thiểu để cam kết hiệu năng?
5. Bao giờ cần macOS? (quyết định thời điểm làm lớp RHI)
6. Phân gói và cách cấp giấy phép (xem mục 5).
