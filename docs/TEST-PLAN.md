# LumaCast — Kế hoạch kiểm thử (TEST-PLAN)

Liên quan: `SPEC.md` (tiêu chí nghiệm thu), `PLAN.md` (Definition of Done), `TEST-WINDOWS.md` (cách build trên Windows).

## 1. Tổng quan
| Tầng | Công cụ | Chạy ở đâu | Số kiểm tra |
|---|---|---|---|
| Tự động — render/lõi | `lumacast_tests` | Linux (GPU phần mềm llvmpipe), Windows | 60 |
| Tự động — giao diện | `lumacast_ui_tests` | Linux (xvfb), Windows | 106 |
| Tự động — độ tin cậy | `lumacast_m3c_tests` | Linux (xvfb), Windows | 28 |
| Tự động — Effect/Transition/plug-in | `lumacast_effects_tests` | Linux (xvfb), Windows | 65 |
| Tự động — hoạt hoạ/Layer | `lumacast_anim_tests` | Linux (xvfb), Windows | 67 |
| Thử tay trên phần cứng thật | Checklist mục 3 | Windows, màn LED/bộ gửi tín hiệu thật | theo bảng |
| Benchmark hiệu năng | Mục 4 | Windows, máy mục tiêu | theo bảng |

Quy tắc: mỗi PR phải qua `ctest`; phần thử tay liên quan phải được chạy hoặc ghi rõ *chưa thử* trong mô tả PR.

## 2. Test tự động
Chạy:
- Linux: `xvfb-run -a ctest --test-dir build --output-on-failure`
- Windows: `ctest --test-dir build --output-on-failure` (cần driver OpenGL 3.3, không chạy qua Remote Desktop)

**`lumacast_tests`** (đọc lại điểm ảnh từ FBO để so sánh): Scene/Layer (Fit/Fill/chiều ảnh), Transition (Fade, Cut, Wipe, Dip, T-bar),
Output (Fit, Stretch, xoay, crop), hiệu chỉnh màu, video wall, FTB, thumbnail/snapshot, project (lưu–mở, đường dẫn tương đối, các lỗi),
video FFmpeg (khung hình, lặp, xoay 90/180/270 so với FFmpeg tự xoay).

**`lumacast_ui_tests`** (điều khiển widget như người dùng): engine + cửa sổ chính, monitor, CUT/TAKE/AUTO/T-bar, BLACK ALL, Dừng hình,
FTB, Inspector (Layer, Output), Hoàn tác/Làm lại (kể cả gộp bước), kéo-thả/ẩn-hiện Layer, nhập media, lưu và tự lưu.

**Giới hạn đã biết:**
- Dùng GPU phần mềm trên Linux: không phản ánh hiệu năng và hành vi driver thật (NVIDIA/AMD/Intel).
- Test video cần lệnh `ffmpeg` để tạo video thử; không có thì tự bỏ qua (thường xảy ra trên Windows nếu `ffmpeg.exe` không nằm trong PATH). Khi đó phần phát video chỉ kiểm chứng bằng thử tay.
- Chưa có test cho: nhiều Output toàn màn hình, mất thiết bị, chạy dài hạn, kéo chuột thật (T-bar, kéo-thả Layer).

## 3. Checklist thử tay trên Windows
Trạng thái: **Đạt** · **Lỗi** · **Chưa thử**. Cập nhật cột này mỗi lần chạy và ghi vào mô tả PR.

| ID | Bước | Kết quả mong đợi | Trạng thái |
|---|---|---|---|
| T-BLD | Build theo `TEST-WINDOWS.md`, `ctest` | Build sạch; 2/2 test qua | Đạt (Qt 6.8.3, FFmpeg 9.1, MSVC 2022) |
| T-RUN | `lumacast.exe -o 960x540 -o 540x960 -m clip.mp4` | Hai cửa sổ Output, giao diện hiển thị, FPS ≈ 60 | Đạt (AMD Radeon, 60 fps) |
| T-VID | Phát video thật, để chạy ≥ 2 phút | Mượt, tự lặp, không giật | Chưa thử (chỉ xem ngắn) |
| T-PORT | Video dọc 9:16 | Đúng chiều, vừa khít Output dọc | Đạt |
| T-TR | Thử CUT, TAKE (Fade/Dip/Wipe), AUTO | Chuyển đúng, Program/Preview hoán đổi | Chưa thử |
| T-TBAR | Kéo T-bar bằng chuột: giữa chừng, về 0, hết hành trình | Hình pha theo tay; về 0 = huỷ; hết = hoàn tất, T-bar tự về 0 | Chưa thử |
| T-UNDO | Chỉnh alpha/vị trí/màu Output vài lần, Ctrl+Z, Ctrl+Y | Quay lại từng bước đúng; kéo liên tục = một bước | Chưa thử |
| T-LAYER | Kéo-thả đổi thứ tự Layer bằng chuột; tick ẩn/hiện | Thứ tự và hiển thị đổi ngay ở Program | Chưa thử |
| T-FRZ | Bấm **Dừng** ở Output 1 khi video đang chạy | Hình Output 1 đứng yên; Output 2 vẫn chạy; Đen vẫn thắng Dừng | Đạt (người dùng xác nhận 24/11/2026, chưa ghi chi tiết máy/GPU) |
| T-BLK | BLACK ALL, Đen từng Output, phím B | Đen đúng phạm vi; bấm lại trả hình | Chưa thử |
| T-FTB | FTB bật/tắt | Program mờ dần về đen và trở lại | Chưa thử |
| T-SAVE | Lưu, đóng, mở lại bằng `--project` | Mọi Scene/Layer/Output/màu khôi phục | Chưa thử |
| T-REC | Chỉnh project, đợi > 60 s, tắt tiến trình đột ngột, mở lại | Hỏi khôi phục bản tự lưu; khôi phục đúng | Chưa thử |
| T-IMP | Nhập media bằng nút và nhấp đúp trong Media Pool | Có Scene mới, ảnh thu nhỏ đúng, video có ảnh | Chưa thử |
| T-KEY | Space, Enter, B, 1–9, Ctrl+S/O/Z | Đúng chức năng, không xung đột khi gõ vào ô số | Chưa thử |
| T-QUIT | Đóng cửa sổ chính | Hỏi xác nhận; chọn Có thì mọi cửa sổ Output đóng | Chưa thử |
| T-FS | `-o 1920x1080@1 --fullscreen` với màn LED/bộ gửi tín hiệu | Hình ra đúng màn, không viền, không xé; Esc thoát | Đạt (người dùng xác nhận 24/11/2026, chưa ghi chi tiết máy/GPU) |
| T-WALL | `--wall 4x1 --cell 1920x1080` trên 4 màn | Hình liền mạch qua các màn | Chưa thử |
| T-CAL | Dùng test chart; chỉnh sáng/gamma/gain từng Output | Màu khớp giữa các màn sau chỉnh | Chưa thử |
| T-4K | 2 Output 4K + 2 Output 1080p, video 4K | Xem mục 4 | **Chưa thử — NFR-02** |
| T-SOAK | Chạy liên tục ≥ 1 giờ có chuyển cảnh định kỳ | Không crash, khung rơi < 0,1 %, bộ nhớ ổn định | Chưa thử |
| T-LOSS | Rút cáp/tắt một màn LED khi đang chạy (2 màn trở lên) | Output đó báo "MẤT MÀN HÌNH", **cửa sổ không nhảy sang màn khác**; Output còn lại không ảnh hưởng; cắm lại thì hình tự về đúng màn; có dòng trong log | Đạt (người dùng xác nhận 24/11/2026, chưa ghi chi tiết máy/GPU) |
| T-PAT | Bấm "Mẫu" ở một Output, rồi "Mẫu thử tất cả Output" | Hiện lưới/thanh màu/thước điểm ảnh/toạ độ 4 góc đúng độ phân giải thật; viền đỏ sát mép màn LED; tắt thì về Program | Đạt (người dùng xác nhận 24/11/2026, chưa ghi chi tiết máy/GPU) |
| T-FX | Thêm Effect (làm mờ, chỉnh màu, glow…) vào một Layer đang chạy video; chỉnh tham số bằng chuột | Hình trên Program và trên màn Output đổi theo, mượt (FPS không tụt rõ); tắt/gỡ thì trở lại | Đạt (người dùng xác nhận 24/11/2026, chưa ghi chi tiết máy/GPU) |
| T-TRX | Thử TAKE với Push (4 hướng), Slide, Zoom, Luma wipe (5 mẫu), plug-in Quét xoay; đổi easing và thời lượng | Hiệu ứng đúng hướng/kiểu, chạy hết rồi Program/Preview hoán đổi | Đạt (người dùng xác nhận 24/11/2026, chưa ghi chi tiết máy/GPU) |
| T-STG | Nhập một video có kênh alpha (ví dụ .mov ProRes 4444 hoặc WebM VP9 alpha), chọn Stinger, TAKE | Video chạy đè từ đầu, đổi cảnh ở điểm cắt, tự kết thúc; lần TAKE sau lại phát từ đầu | Đạt (người dùng xác nhận 24/11/2026, chưa ghi chi tiết máy/GPU) |
| T-PLG | Chép plug-in mẫu vào `%AppData%\LumaCast\plugins`, sửa file `.frag` khi đang chạy; thử plug-in cố tình sai | Tự nạp lại sau ~0,3 s; sai thì Effect bị bỏ qua, có lỗi trong dock Plug-in/tab Effect/log, ứng dụng không sập | Đạt (người dùng xác nhận 24/11/2026, chưa ghi chi tiết máy/GPU) |
| T-ANI | Đặt keyframe X/alpha/xoay cho một Layer (nhấp đúp trên làn, nút ◆, hoặc sửa ô giá trị khi đã có key); bấm Phát; kéo playhead trên thước khi đang chạy video | Chuyển động mượt đúng thời điểm, đúng kiểu nội suy; tua không làm giật hình ra Output; lặp đúng; kéo key bằng chuột đúng chỗ | Đạt (người dùng xác nhận 24/11/2026, chưa ghi chi tiết máy/GPU) |
| T-AIR | Đưa Scene có keyframe lên Program bằng CUT, TAKE và T-bar | Chạy lại từ 0 mỗi lần lên sóng; tắt "Phát lại khi lên Program" thì giữ nguyên; timeline hiện cảnh báo "đang ở PROGRAM" khi chọn đúng Scene đó | Đạt (người dùng xác nhận 24/11/2026, chưa ghi chi tiết máy/GPU) |
| T-LYR | Xoay, crop (kể cả với video dọc quay bằng điện thoại), đổi blend (Add/Multiply/Screen) cho Layer; hoàn tác | Hình đúng trên Output; crop theo hình đã xoay | Đạt (người dùng xác nhận 24/11/2026, chưa ghi chi tiết máy/GPU) |
| T-LOG | Mở `%AppData%\LumaCast\logs\lumacast.log` sau một phiên chạy | Có dòng khởi động, GPU, sự kiện Output; thanh trạng thái hiện p50/p99 | Đạt (người dùng xác nhận 24/11/2026, chưa ghi chi tiết máy/GPU) |

## 4. Benchmark hiệu năng
Mỗi cấu hình đo 5 phút và ghi: FPS trung bình, số khung rơi, thời gian khung p50/p99, GPU %, VRAM, CPU %.

| Cấu hình | Nguồn | Mục tiêu |
|---|---|---|
| B1: 1 Output 1080p | Video 1080p60 | 60 fps, rơi < 0,1 % |
| B2: 4 Output 1080p, cùng Channel | Video 1080p60 | 60 fps, rơi < 0,1 % |
| B3: 2 Output 4K + 2 Output 1080p | Video 4K30, test pattern | 60 fps, rơi < 0,1 % |
| B4: 4 Output 4K | Video 4K | Ghi nhận giới hạn thật (có thể chưa đạt khi chưa có giải mã phần cứng) |
| B5: B2 + chuỗi Effect (blur 64 px, glow, chỉnh màu) trên 1–4 Layer | Video 1080p60 | Ghi chi phí mỗi Effect (p99 thời gian khung); blur/glow dùng 32 mẫu nên đáng chú ý ở 4K |

Phần cứng đã đo: AMD Radeon (model chưa ghi), 2 Output cửa sổ, 60 fps. **Cần ghi đủ tên GPU, driver, phiên bản Windows cho mỗi lần đo.**

## 5. Mẫu báo kết quả (dán vào mô tả PR)
```
Phiên bản/commit: ...
Máy: GPU ..., driver ..., Windows ..., số màn hình ...
ctest: 2/2 qua | lỗi: ...
Thử tay: T-RUN Đạt, T-TBAR Đạt, T-FRZ Lỗi (mô tả), T-FS Chưa thử, ...
Benchmark: B2 = 60 fps, rơi 0,02 %, GPU 35 %
```
