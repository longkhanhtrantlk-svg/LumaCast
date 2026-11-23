# LumaCast — Đặc tả yêu cầu (SPEC)

**Phiên bản tài liệu:** 1.0 · **Trạng thái mã:** sau M3b (PR #1, #2) · **Nền tảng mục tiêu:** Windows 10/11 x64 (macOS sau)

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
| SRC-06 | NDI vào | P1 | ⬜ | Nhận stream NDI, độ trễ < 3 khung |
| SRC-07 | Camera/card capture | P1 | ⬜ | Hiện hình từ webcam/card capture |
| SRC-08 | Chữ/đồ hoạ (CG), web, chuỗi ảnh | P2 | ⬜ | — |

### 3.2 Scene và Layer (SCN)
| ID | Yêu cầu | Ưu tiên | TT | Nghiệm thu |
|---|---|---|---|---|
| SCN-01 | Scene có độ phân giải riêng và nhiều Layer | P0 | ✅ | Test tự động |
| SCN-02 | Layer: vị trí, kích thước, độ mờ (alpha), kiểu fit (Stretch/Fit/Fill), ẩn/hiện | P0 | ✅ | Test tự động |
| SCN-03 | Đổi thứ tự Layer (kéo-thả hoặc nút), thêm/xoá, hoàn tác/làm lại | P0 | ✅ | Test tự động |
| SCN-04 | Xoay Layer, crop Layer | P1 | ⬜ | Test render |
| SCN-05 | Blend mode (Add, Multiply, Screen…) | P1 | ⬜ | Test render so với giá trị tính tay |
| SCN-06 | Chuỗi Effect trên Layer/Scene (blur, glow, màu, RGB split…) | P0 | ⬜ | M4a: bật/tắt, tham số, hiển thị đúng ở Program |
| SCN-07 | Timeline và keyframe cho thuộc tính Layer | P1 | ⬜ | M4b: phát lại đúng theo đồng hồ Scene |
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
| CHN-08 | Transition bổ sung: Stinger, Luma wipe, Push/Slide, Zoom; easing chọn được | P1 | ⬜ | M4a |
| CHN-09 | Transition do plug-in cung cấp | P1 | ⬜ | M4a |

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
| OUT-13 | Output dạng khác: NDI ra, ghi file, DMX/Art-Net | P1 | ⬜ | Qua `OutputSink` |

### 3.5 Project (PRJ)
| ID | Yêu cầu | Ưu tiên | TT | Nghiệm thu |
|---|---|---|---|---|
| PRJ-01 | Lưu/mở project JSON; media theo đường dẫn tương đối; ghi nguyên tử | P0 | ✅ | Test tự động |
| PRJ-02 | File hỏng/sai tham chiếu → báo lỗi rõ, không crash | P0 | ✅ | Test tự động |
| PRJ-03 | Tự lưu mỗi 60 s; hỏi khôi phục khi bản tự lưu mới hơn | P0 | ✅ | Test tự động (tạo/đọc); thử tay khôi phục T-REC |
| PRJ-04 | Nhập media vào project khi đang chạy (tạo Scene) | P0 | ✅ | Test tự động |
| PRJ-05 | Mở project khác khi đang chạy | P1 | 🟡 | Hiện khởi động lại tiến trình với project mới |
| PRJ-06 | Hoàn tác/làm lại cho Layer và Output | P0 | ✅ | Test tự động |
| PRJ-07 | Hoàn tác cho nhập media, đổi tên, thêm/xoá Scene và Output | P1 | ⬜ | — |
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
| UI-07 | Phím tắt tuỳ chỉnh; khoá thao tác khi On Air (operator lock) | P1 | ⬜ | — |
| UI-08 | Quản lý Output/canvas bằng giao diện (kéo-thả màn hình, wizard tạo project) | P1 | ⬜ | Hiện chỉnh qua Inspector và dòng lệnh |
| UI-09 | Ngôn ngữ: tiếng Việt; thêm tiếng Anh | P2 | 🟡 | Hiện chỉ tiếng Việt |

### 3.7 Plug-in (PLG)
| ID | Yêu cầu | Ưu tiên | TT | Nghiệm thu |
|---|---|---|---|---|
| PLG-01 | Shader plug-in (GLSL + manifest JSON, kiểu ISF) cho Effect và Transition; tham số tự sinh giao diện; nạp lại nóng | P0 | ⬜ | M4a |
| PLG-02 | Plug-in native (DLL, C ABI có phiên bản) cho Source/Effect/Output filter | P1 | ⬜ | M4a/M5 |
| PLG-03 | Plug-in lỗi hoặc chạy quá hạn bị vô hiệu hoá, không làm sập ứng dụng | P0 | ⬜ | Với PLG-01/02 |
| PLG-04 | Plugin Manager (danh sách, bật/tắt, cấu hình, phiên bản, tương thích) | P1 | ⬜ | — |

### 3.8 Âm thanh, điều khiển ngoài, vận hành (AUD / CTL / OPS)
| ID | Yêu cầu | Ưu tiên | TT | Nghiệm thu |
|---|---|---|---|---|
| AUD-01 | Âm thanh theo Channel, bật/tắt từng Channel, chọn thiết bị ra (WASAPI) | P0 | ⬜ | M5a |
| AUD-02 | Âm lượng, đồng bộ hình–tiếng theo đồng hồ âm thanh, crossfade theo transition, meter | P1 | ⬜ | M5a |
| CTL-01 | Điều khiển ngoài: HTTP/WebSocket, OSC, MIDI (chọn Scene, TAKE/CUT, Đen, đặt tham số) | P1 | ⬜ | M5c |
| OPS-01 | Tự khởi động cùng Windows, tự mở lại project gần nhất, ghi log | P1 | ⬜ | M5c |
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
| NFR-09 | Kiểm thử | Mọi tính năng mới có test tự động; PR phải qua `ctest` | 137 kiểm tra tự động |
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

## 6. Câu hỏi mở
1. Số Output tối đa và độ phân giải tối đa mục tiêu của bản đầu? (đang giả định 4 Output; 1080p và 4K)
2. Âm thanh, NDI, camera có cần ngay trong bản đầu để dùng thật không, hay chỉ hình trước?
3. Có cần điều khiển trực tiếp bộ gửi tín hiệu LED (Novastar, Colorlight) qua mạng không, hay chỉ HDMI/DP?
4. GPU và cấu hình máy tối thiểu để cam kết hiệu năng?
5. Bao giờ cần macOS? (quyết định thời điểm làm lớp RHI)
6. Phân gói và cách cấp giấy phép (xem mục 5).
