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
| Tự động — điều khiển ngoài (HTTP/SSE/OSC, bảo mật) | `lumacast_control_tests` | Linux (xvfb), Windows | 186 |
| Tự động — vận hành (cài đặt, tự khởi động, mở lại project, khoá, hoàn tác nhập media, timeline) | `lumacast_m5c_tests` | Linux (xvfb), Windows | 197 |
| Tự động — âm thanh: ring, mixer, trọng số crossfade, kế hoạch trộn, lưu/mở, backend Null + thread, FFmpeg, hình theo đồng hồ âm thanh | `lumacast_audio_tests` | Linux, Windows (không cần GPU) | 80 |
| Tự động — âm thanh qua engine thật: mute/âm lượng/crossfade/T-bar/FTB qua meter, HTTP + OSC, panel Âm thanh, Inspector, khoá vận hành | `lumacast_audio_ui_tests` | Linux (xvfb), Windows | 44 |
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

**`lumacast_control_tests`** (socket TCP/UDP thật ở `127.0.0.1`, cổng ngẫu nhiên): bộ phân tích OSC (đúng, cụt ở mọi độ dài, bundle lồng/quá sâu, fuzz 6000 gói), HTTP (dòng yêu cầu/header/Content-Length sai, header 20 KB, thân quá lớn, quá nhiều header, slowloris, quá tải kết nối, 300 kết nối rác), toàn bộ endpoint và mã lỗi, Preview không đổi Program, Đen đúng phạm vi, token (thiếu/sai/lệch 1 ký tự/tiền tố/Basic), LAN bắt buộc token, Origin/Host (CSRF, DNS rebinding), SSE (đẩy khi đổi, giới hạn client, client gửi rác), khoá vận hành (423 cho lệnh sửa, live vẫn chạy), OSC qua UDP (lệnh đúng, gói hỏng, địa chỉ lạ, lũ 2000 gói), log bật/tắt/từ chối.

**`lumacast_m5c_tests`**: hộp thoại cài đặt dùng thật trên `MainWindow`, tự khởi động (QSettings giả), tự mở lại project gần nhất (chạy cả tiến trình `lumacast` thật với 5 kịch bản), khoá vận hành (từng điều khiển bị vô hiệu/giữ lại, giữ nút/Space mở khoá), hoàn tác/làm lại nhập media và thuộc tính Scene (kể cả Scene đang On Air), timeline (zoom/cuộn, chuột thật, khung chọn, kéo nhóm, sao chép/dán, khoá).

**`lumacast_audio_tests`** (không cần GPU): `AudioStream` (ring, quay vòng, underrun, flush, giữ im lặng khi restart, đồng hồ), mixer (bus độc lập, mute, đổi hệ số tuyến tính trong khối, nguồn ở hai Channel chỉ rút một lần, giới hạn ±1, meter), trọng số crossfade của `Channel` (Fade/easing/Dip/T-bar/Stinger), kế hoạch trộn từ `Project` (mute, âm lượng, FTB, Layer ẩn, cùng nguồn ở hai Scene), lưu/mở project (mặc định, project cũ, giá trị rác), backend Null + thread mixer (nhịp, độ trễ, đổi thiết bị, giải phóng nguồn), và với `ffmpeg`: giải mã âm thanh 44,1 kHz mono → 48 kHz stereo, video không tiếng, hình theo đồng hồ âm thanh qua nhiều vòng lặp (đo lệch độc lập từ thread test), lặp nối liền không underrun, mute không làm đứng đồng hồ, `restart()`, mixer dừng thì rơi về đồng hồ tường.

**`lumacast_audio_ui_tests`** (engine + `MainWindow` thật, nguồn âm thanh DC để đọc mức qua meter): mức từng bus, mute/âm lượng qua HTTP/OSC (kể cả giá trị sai, kênh không có, khoá vận hành), Fade tuyến tính 0,4→0,2 mượt không tụt, T-bar theo vị trí, Dip về ~0 ở giữa, FTB, nút Tắt tiếng/thanh âm lượng/meter trên UI, đồng bộ ngược khi đổi từ API, chọn thiết bị, khoá vận hành, âm lượng Layer + Hoàn tác.

**`lumacast_ui_tests`** (điều khiển widget như người dùng): engine + cửa sổ chính, monitor, CUT/TAKE/AUTO/T-bar, BLACK ALL, Dừng hình,
FTB, Inspector (Layer, Output), Hoàn tác/Làm lại (kể cả gộp bước), kéo-thả/ẩn-hiện Layer, nhập media, lưu và tự lưu.

**Giới hạn đã biết:**
- Âm thanh trên Linux chỉ đi qua backend **Null** (không có tiếng ra loa): mixer, đồng hồ, crossfade, mute, meter, đồng bộ được kiểm tra; **WASAPI thật, nghe bằng tai, độ trễ thật của thiết bị, rút/đổi thiết bị, đo lệch 30 phút chỉ kiểm chứng được bằng thử tay (T-AUD-*)**.
- Dùng GPU phần mềm trên Linux: không phản ánh hiệu năng và hành vi driver thật (NVIDIA/AMD/Intel).
- Test video cần lệnh `ffmpeg` để tạo video thử; không có thì tự bỏ qua (thường xảy ra trên Windows nếu `ffmpeg.exe` không nằm trong PATH). Khi đó phần phát video chỉ kiểm chứng bằng thử tay.
- Chưa có test cho: nhiều Output toàn màn hình, mất thiết bị, chạy dài hạn, kéo chuột thật (T-bar, kéo-thả Layer).
- Cô lập cấu hình: test dùng `lc::appSettings()` + `QSettings::setPath`; tiến trình con dùng `LUMACAST_CONFIG_DIR`. (Trước M5c lời gọi `QSettings("LumaCast","LumaCast")` bỏ qua cách cô lập này; đã sửa. Nếu máy Windows từng chạy test cũ, registry `HKCU\\Software\\LumaCast` có thể chứa dữ liệu rác như danh sách "Gần đây" trỏ vào thư mục tạm.)
- M5c: ghi registry Windows **thật** (chỉ test logic bằng `QSettings` file .ini), mạng LAN thật, điện thoại/Companion/bàn OSC thật, hiệu năng khi nhiều client SSE, nhập/hoàn tác media video dài.

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
| T-FRZ | Bấm **Dừng** ở Output 1 khi video đang chạy | Hình Output 1 đứng yên; Output 2 vẫn chạy; Đen vẫn thắng Dừng | **Chưa thử — rủi ro R1** |
| T-BLK | BLACK ALL, Đen từng Output, phím B | Đen đúng phạm vi; bấm lại trả hình | Chưa thử |
| T-FTB | FTB bật/tắt | Program mờ dần về đen và trở lại | Chưa thử |
| T-SAVE | Lưu, đóng, mở lại bằng `--project` | Mọi Scene/Layer/Output/màu khôi phục | Chưa thử |
| T-REC | Chỉnh project, đợi > 60 s, tắt tiến trình đột ngột, mở lại | Hỏi khôi phục bản tự lưu; khôi phục đúng | Chưa thử |
| T-IMP | Nhập media bằng nút và nhấp đúp trong Media Pool | Có Scene mới, ảnh thu nhỏ đúng, video có ảnh | Chưa thử |
| T-KEY | Space, Enter, B, 1–9, Ctrl+S/O/Z | Đúng chức năng, không xung đột khi gõ vào ô số | Chưa thử |
| T-QUIT | Đóng cửa sổ chính | Hỏi xác nhận; chọn Có thì mọi cửa sổ Output đóng | Chưa thử |
| T-FS | `-o 1920x1080@1 --fullscreen` với màn LED/bộ gửi tín hiệu | Hình ra đúng màn, không viền, không xé; Esc thoát | **Chưa thử — OUT-09** |
| T-WALL | `--wall 4x1 --cell 1920x1080` trên 4 màn | Hình liền mạch qua các màn | Chưa thử |
| T-CAL | Dùng test chart; chỉnh sáng/gamma/gain từng Output | Màu khớp giữa các màn sau chỉnh | Chưa thử |
| T-4K | 2 Output 4K + 2 Output 1080p, video 4K | Xem mục 4 | **Chưa thử — NFR-02** |
| T-SOAK | Chạy liên tục ≥ 1 giờ có chuyển cảnh định kỳ | Không crash, khung rơi < 0,1 %, bộ nhớ ổn định | Chưa thử |
| T-LOSS | Rút cáp/tắt một màn LED khi đang chạy (2 màn trở lên) | Output đó báo "MẤT MÀN HÌNH", **cửa sổ không nhảy sang màn khác**; Output còn lại không ảnh hưởng; cắm lại thì hình tự về đúng màn; có dòng trong log | **Chưa thử** (đã có test tự động cho logic) |
| T-PAT | Bấm "Mẫu" ở một Output, rồi "Mẫu thử tất cả Output" | Hiện lưới/thanh màu/thước điểm ảnh/toạ độ 4 góc đúng độ phân giải thật; viền đỏ sát mép màn LED; tắt thì về Program | Chưa thử |
| T-FX | Thêm Effect (làm mờ, chỉnh màu, glow…) vào một Layer đang chạy video; chỉnh tham số bằng chuột | Hình trên Program và trên màn Output đổi theo, mượt (FPS không tụt rõ); tắt/gỡ thì trở lại | Chưa thử |
| T-TRX | Thử TAKE với Push (4 hướng), Slide, Zoom, Luma wipe (5 mẫu), plug-in Quét xoay; đổi easing và thời lượng | Hiệu ứng đúng hướng/kiểu, chạy hết rồi Program/Preview hoán đổi | Chưa thử |
| T-STG | Nhập một video có kênh alpha (ví dụ .mov ProRes 4444 hoặc WebM VP9 alpha), chọn Stinger, TAKE | Video chạy đè từ đầu, đổi cảnh ở điểm cắt, tự kết thúc; lần TAKE sau lại phát từ đầu | **Chưa thử** (mới có test với nguồn tổng hợp) |
| T-PLG | Chép plug-in mẫu vào `%AppData%\LumaCast\plugins`, sửa file `.frag` khi đang chạy; thử plug-in cố tình sai | Tự nạp lại sau ~0,3 s; sai thì Effect bị bỏ qua, có lỗi trong dock Plug-in/tab Effect/log, ứng dụng không sập | Chưa thử |
| T-ANI | Đặt keyframe X/alpha/xoay cho một Layer (nhấp đúp trên làn, nút ◆, hoặc sửa ô giá trị khi đã có key); bấm Phát; kéo playhead trên thước khi đang chạy video | Chuyển động mượt đúng thời điểm, đúng kiểu nội suy; tua không làm giật hình ra Output; lặp đúng; kéo key bằng chuột đúng chỗ | Chưa thử |
| T-AIR | Đưa Scene có keyframe lên Program bằng CUT, TAKE và T-bar | Chạy lại từ 0 mỗi lần lên sóng; tắt "Phát lại khi lên Program" thì giữ nguyên; timeline hiện cảnh báo "đang ở PROGRAM" khi chọn đúng Scene đó | Chưa thử |
| T-LYR | Xoay, crop (kể cả với video dọc quay bằng điện thoại), đổi blend (Add/Multiply/Screen) cho Layer; hoàn tác | Hình đúng trên Output; crop theo hình đã xoay | Chưa thử |
| T-LOG | Mở `%AppData%\LumaCast\logs\lumacast.log` sau một phiên chạy | Có dòng khởi động, GPU, sự kiện Output; thanh trạng thái hiện p50/p99 | Chưa thử |
| T-CTL | Cài đặt → Cài đặt vận hành → bật HTTP. Mở `http://127.0.0.1:8088/` trên máy, rồi bật LAN + token và mở `http://<IP máy>:8088/` trên điện thoại (cùng Wi-Fi), nhập token; thử chọn Scene, CUT, TAKE, FTB, Đen từng Output, BLACK ALL. Thử `curl` với token sai. Thử module HTTP của Bitfocus Companion | Điện thoại điều khiển được, trạng thái cập nhật tức thì (SSE); token sai → 401; log có dòng "TỪ CHỐI"; tường lửa Windows có thể hỏi quyền lần đầu | **Chưa thử** |
| T-OSC | Bật OSC (cổng 9000). Từ TouchOSC/Companion/`oscsend` gửi `/lumacast/channel/1/preview 3`, `/lumacast/channel/1/cut`, `/lumacast/output/1/black 1`, một bundle | Lệnh được thực thi đúng; gói rác không làm gì và không treo; log ghi gói hỏng | **Chưa thử** |
| T-AUTO | Bật "Khởi động LumaCast cùng Windows"; kiểm tra `regedit` mục `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` có `LumaCast` = đường dẫn exe trong ngoặc kép; đăng xuất/đăng nhập (hoặc khởi động lại) | LumaCast tự chạy; tắt tuỳ chọn thì giá trị bị xoá; di chuyển thư mục exe rồi mở cài đặt → có cảnh báo "đường dẫn khác" | **Chưa thử** |
| T-REOPEN | Lưu project, tắt, chạy `lumacast.exe` không tham số; xoá/đổi tên file project rồi chạy lại; tắt tuỳ chọn "Tự mở lại" rồi chạy lại; mất điện giả (kill) khi có autosave mới hơn | Tự mở lại đúng project; file mất → chạy bình thường + thông báo ở thanh trạng thái; tắt tuỳ chọn → không tự mở; có bản autosave → hỏi khôi phục | **Chưa thử** |
| T-LOCK | Bấm "Khoá vận hành" (hoặc Ctrl+L) khi đang On Air; thử sửa Inspector/Timeline/Nhập media/Plug-in/Ctrl+Z/đóng cửa sổ; thử CUT, TAKE, T-bar, FTB, BLACK ALL, Đen/Dừng/Mẫu, chọn Scene, Phát/Tạm dừng timeline; giữ nút 0,5 s rồi nhả, rồi giữ 1 s | Mọi điều khiển sửa bị mờ; điều khiển live vẫn chạy; thanh trạng thái hiện "KHOÁ VẬN HÀNH"; giữ 1 s mới mở khoá | **Chưa thử** |
| T-UNDO2 | Nhập một ảnh và một video, Ctrl+Z từng bước, Ctrl+Y; thử Ctrl+Z khi Scene vừa nhập đang On Air | Scene biến mất/hiện lại đúng, không treo, Program không đổi; đang On Air thì báo từ chối | **Chưa thử** |
| T-AUD-DEV | Mở dock *Âm thanh*: danh sách thiết bị có đủ loa/tai nghe/HDMI; chọn từng thiết bị (có lúc là thiết bị đang dùng ở 44,1 kHz hoặc 96 kHz); phát video có tiếng; lưu project, mở lại trên máy có/không có thiết bị đó | Ra đúng thiết bị, không méo/rè; đổi thiết bị giữa chừng không treo, tiếng tiếp tục; máy không có thiết bị đã lưu → dùng mặc định + báo; status hiện "Đang phát: <tên>" | **Chưa thử — AUD-01** |
| T-AUD-MUTE | Có ≥ 2 Channel với video có tiếng khác nhau. Bấm *Tắt tiếng* từng Channel, kéo thanh âm lượng 0–150%, xem meter; thử `curl .../mute`, `.../volume` và OSC `/lumacast/channel/1/mute 1`; bật Khoá vận hành rồi thử lại | Tắt/bật tiếng đúng Channel, không "cạch" khi đổi; meter đúng và về 0 khi tắt; hình không bị ảnh hưởng; lệnh ngoài chạy kể cả khi khoá | **Chưa thử — AUD-01** |
| T-AUD-XFADE | Hai Scene video có tiếng khác nhau; TAKE Fade 1–3 s, Dip, Wipe, Push, Stinger có tiếng; kéo T-bar chậm/nhanh qua lại; bật/tắt FTB | Tiếng của Program giảm và Preview tăng đúng tiến độ hình, không vỡ/lách/tụt âm lượng; Dip im ở giữa; T-bar theo ngay vị trí; FTB kéo cả tiếng | **Chưa thử — AUD-02** |
| T-AUD-SYNC | Video có thể nhìn ra tiếng (clip có tiếng vỗ tay/đèn nháy cùng lúc), phát lặp ≥ 30 phút ở 60 fps, vừa phát vừa chuyển cảnh; quay màn hình + loa bằng điện thoại 240 fps hoặc dùng clip kiểm tra "flash & beep" | Lệch hình–tiếng < 40 ms ở phút 1 và phút 30; lặp không có tiếng ngắt/click; không tụt khung do âm thanh | **Chưa thử — AUD-02 (nghiệm thu M5a)** |
| T-AUD-HOT | Đang phát: rút tai nghe/đổi thiết bị mặc định của Windows; bật/tắt Bluetooth; đặt thiết bị ở 96 kHz/5.1 | Không crash/treo; hình vẫn chạy (rơi về đồng hồ tường); thiết bị mới/được cắm lại thì tiếng trở lại sau ~1 s hoặc sau khi chọn lại | **Chưa thử** |
| T-AUD-SOAK | Chạy ≥ 1 giờ với 2–4 Channel có tiếng, nhiều video lặp, chuyển cảnh định kỳ | `GET /api/audio` cho `underruns` không tăng đều, CPU âm thanh thấp, bộ nhớ ổn định, meter không kẹt | **Chưa thử** |
| T-TLZ | Timeline: Ctrl + lăn chuột để zoom, kéo thanh cuộn; chọn nhiều key (Ctrl+nhấp, kéo khung); kéo nhóm; Ctrl+C/Ctrl+V tại playhead; Delete; Ctrl+Z | Đúng như mô tả, mượt với Scene dài (≥ 1 phút), mỗi thao tác một bước Hoàn tác | **Chưa thử** |

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
