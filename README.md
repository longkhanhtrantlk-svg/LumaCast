# LumaCast - LED Display Controller

Bo dieu khien man hinh LED nhieu kenh / nhieu do phan giai. Xem thiet ke: `docs/KienTruc.md`; yeu cau: `docs/SPEC.md`.

NDI® (Network Device Interface) là nhãn hiệu đã đăng ký của Vizrt NDI AB — https://ndi.video

## Tài liệu
| File | Nội dung |
|---|---|
| `docs/SPEC.md` | Yêu cầu chức năng/phi chức năng, ưu tiên, tiêu chí nghiệm thu, trạng thái |
| `docs/PLAN.md` | Hiện trạng, lộ trình M3c → M7, nhiệm vụ, nghiệm thu, rủi ro, quy trình |
| `docs/KienTruc.md` | Thiết kế kỹ thuật và quyết định |
| `docs/PLUGIN-SDK.md` | Huong dan viet plug-in shader (Effect/Transition) |
| `docs/CONTROL-API.md` | API dieu khien ngoai: HTTP/SSE, OSC, bao mat, vi du |
| `docs/PROJECT-FORMAT.md` | Đặc tả định dạng file project (phiên bản 1) |
| `docs/TEST-PLAN.md` | Test tự động, checklist thử tay trên Windows, benchmark |
| `docs/TEST-WINDOWS.md` | Hướng dẫn build và chạy trên Windows |
| `THIRD-PARTY-NOTICES.txt` | Giấy phép thư viện bên thứ ba (Qt, FFmpeg, NDI®) — đi kèm bộ cài |

## Build
Linux (de phat trien / chay test): `apt install qt6-base-dev libqt6opengl6-dev libavcodec-dev libavformat-dev libswscale-dev libswresample-dev libavutil-dev`
Windows (muc tieu): Qt 6 + FFmpeg qua vcpkg (`vcpkg install ffmpeg:x64-windows` hoac manifest `vcpkg.json` -- ban LGPL, **khong bat feature gpl/nonfree**), roi:

    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build        # test render + UI (can OpenGL 3.3; Linux khong man hinh: xvfb-run ctest)

## Chay
    lumacast -o 1920x1080@0 -o 3840x2160@1 -o 1920x1080@2/2 --fullscreen -m clip.mp4 -m logo.png --scale fit

`-o WxH[@man_hinh][/kenh]` (lap lai cho nhieu output), `--scale fit|fill|stretch|pixel`, `--fps 60`.
Phim tat (kenh dang chon): `1..9` chon Preview, `Space` = TAKE, `Enter` = CUT (xem muc *Phím tắt và MIDI*).

## Phím tắt và MIDI
Mở *Cài đặt > Phím tắt và MIDI…* (bị khoá khi đang Khoá vận hành). Cấu hình lưu **theo máy** (`QSettings`), không nằm trong file project.

- **Lệnh gán được:** CUT, TAKE, FTB, BLACK ALL, chọn Preview 1–9, Đen/Dừng/Mẫu từng Output, và Khoá vận hành (chỉ phím tắt, chỉ để khoá).
- **Mặc định giữ nguyên như trước:** Space = TAKE, Enter = CUT, B = BLACK ALL, 1–9 = Preview, Ctrl+L = khoá. FTB và Đen/Dừng/Mẫu chưa gán.
- **Space và Enter cố định:** luôn là TAKE/CUT, không gán cho lệnh khác. TAKE và CUT có thể thêm một phím phụ (vd. PageDown cho bút trình chiếu).
- Trùng phím hoặc phím đã dùng cho menu/Timeline thì hộp thoại báo lỗi và không lưu. *Khôi phục phím mặc định* trả về như ban đầu.
- **MIDI** (Windows, API `winmm`, mặc định **tắt**): tích *Bật điều khiển MIDI*, chọn thiết bị, bấm **Học** ở dòng lệnh rồi nhấn nút trên thiết bị. Nhận Note On, CC (tính khi giá trị vượt lên từ dưới 64) và Program Change.
- Nút MIDI tác động lên **kênh và hiệu ứng đang chọn** trên giao diện. Theo cùng quy tắc an toàn với HTTP/OSC: **chọn Preview từ MIDI không bao giờ đổi Program** (kể cả khi AUTO đang bật — khác phím 1–9), bị từ chối khi đang chuyển cảnh; vẫn chạy khi Khoá vận hành; không khoá/mở khoá được.
- Rút thiết bị: thanh trạng thái báo *MIDI: chưa nối*; cắm lại thì tự nối sau khoảng 1 giây (rút rồi cắm lại quá nhanh mà nút không ăn: bấm *Kết nối lại*). Trong lúc hộp thoại mở, nút MIDI không điều khiển chương trình.
- Gói giấy phép không có điều khiển ngoài thì MIDI cũng không mở.
- **Chưa thử với thiết bị MIDI thật** (`docs/TEST-PLAN.md`: T-MIDI). Chưa có: đèn LED phản hồi, núm/fader điều khiển liên tục (T-bar, âm lượng), nhiều thiết bị cùng lúc.

## Mapping trực quan
- Mở bằng **Ctrl+M**, menu *View > Mapping trực quan…* hoặc nút ở tab *Output*. Cửa sổ vẽ hình Program của một Kênh cùng vùng cắt của từng Output thuộc Kênh đó.
- Kéo giữa vùng để di chuyển, kéo mép/góc để đổi cỡ. Bắt dính vào mép canvas và mép Output khác (giữ **Alt** để tạm tắt). Mũi tên nhích 1 px, Shift + mũi tên 10 px, **Esc** huỷ lần kéo đang dở.
- Ô X/Y/Rộng/Cao nhập theo điểm ảnh của Kênh. *Khớp 1:1* đặt cỡ vùng bằng số điểm ảnh thật của Output; *Cả khung* trả về toàn bộ Kênh. Dòng thông tin cho biết đang 1:1 hay bị co giãn.
- Mỗi lần kéo là một bước Hoàn tác. **Thay đổi tác động ngay lên Output đang phát.** Khi Khoá vận hành cửa sổ chỉ để xem.
- Phím tắt cửa sổ chính (Space = TAKE, Enter = CUT, Ctrl+Z) vẫn chạy khi cửa sổ này đang được chọn; trong ô số, Enter chỉ xác nhận số vừa gõ.

## An toàn khi đang diễn
- **Enter** trong ô số hoặc ô chữ chỉ xác nhận giá trị, không CUT. Ngoài ô nhập, Enter vẫn là CUT.
- **Video lên Program thì phát lại từ đầu** (CUT/TAKE/AUTO/T-bar/MIDI/HTTP, ngay khi bắt đầu chuyển). Ở Preview video **vẫn chạy**. Hai Scene dùng chung một video thì chuyển qua lại không giật về đầu. Bỏ chọn *Phát lại khi lên Program* (Timeline) để video của Scene đó không bị tua. Một file đang ở cả Preview và Program thì Preview hiện đúng đoạn Program đang phát.
- **Đóng cửa sổ Output** (nút X, Alt+F4) chỉ ẩn nó; bảng *Outputs* hiện nút **Mở lại**.
- Mở project **thiếu hoặc hỏng file ảnh/video**: project vẫn mở, chỗ đó hiện hình *THIẾU FILE*, có thông báo liệt kê. Lưu không làm mất tham chiếu; chép file về rồi mở lại.

## Nhập media bằng kéo thả
- Kéo một hoặc nhiều file ảnh/video từ Windows Explorer vào **Media Pool** hoặc bất kỳ chỗ nào trong cửa sổ chính (kể cả dock đã tách rời). Giống *File > Nhập media...*: mỗi file thêm một nguồn + một Scene và là một bước Hoàn tác (Ctrl+Z).
- Thả không đưa gì lên Program: Preview/Program giữ nguyên.
- Nhận các đuôi: `mp4 mov mkv avi webm png jpg jpeg bmp gif webp`. File khác, thư mục, ảnh không đọc được bị bỏ qua và báo ở thanh trạng thái (định dạng video khác vẫn thử nhập được qua hộp thoại *Nhập media*, lọc "Tất cả").
- Đang **Khoá vận hành** hoặc đang mở hộp thoại: không thả được (khi khoá có thông báo).
- File gốc không bị di chuyển hay xoá: project chỉ tham chiếu tới đường dẫn của nó.

## Bù khe video wall (viền / khe hở giữa các màn)
- Phần canvas nằm "sau" viền/khe bị bỏ qua, nên hình đi liền mạch qua các tấm và không bị kéo giãn; mỗi Output vẫn đúng **1:1** từng điểm ảnh.
- Dòng lệnh: `lumacast --wall 4x1 --cell 1920x1080 --gap 40x0` (`--gap NGANGxDỌC`, đơn vị điểm ảnh; một số = cả hai; 0…4096).
- Giao diện: khung **Video wall** ở đầu tab *Output*, hai ô *khe ngang* / *khe dọc* (gõ xong nhấn Enter). Hoàn tác được; Khoá vận hành chặn; Program/Preview giữ nguyên. **Thay đổi tác động ngay lên Output đang phát.**
- **Canvas rộng thêm đúng bằng tổng khe**: 4×1 ô 1920×1080 khe 40 → 7800×1080. Channel và các Scene cùng cỡ canvas đổi theo (Scene có độ phân giải riêng giữ nguyên). Media làm sẵn cho 7680×1080 sẽ co giãn hoặc có viền theo *fit* của Layer; muốn đúng từng điểm ảnh thì làm media 7800×1080.
- Cách đo số khe: khoảng cách giữa hai vùng sáng của hai màn kề nhau (gồm cả hai viền) chia cho bước điểm ảnh (pixel pitch) của màn. Ví dụ khe 5 mm, pitch 2,5 mm → 2 px.
- Project lưu khe ngay trong kích thước canvas và `crop` (không đổi định dạng). Project wall cũ mở rồi đặt khe được. Nếu sửa tay `crop` làm lệch lưới, ô khe bị khoá cho tới khi Hoàn tác hoặc sửa lại.

## Chữ và logo overlay
- Dock **Overlay** (tab cạnh *Inspector*): thêm **Tiêu đề** (chữ tĩnh), **Chữ chạy** (sang trái), **Đếm ngược**, **Logo** (PNG/WebP/GIF/JPG/BMP, giữ kênh trong suốt; GIF chỉ lấy khung đầu).
- Overlay thuộc **từng Kênh**, vẽ đè lên Program: đổi Scene, CUT/TAKE hay transition đều giữ nguyên. FTB che cả overlay; Đen/Dừng/Mẫu của Output áp dụng như thường. Preview không hiện overlay; xem trước ở khung nhỏ trong dock.
- **Thêm không tự đưa gì lên Program.** Bấm *HIỆN lên Program* hoặc tick trong danh sách; hiện/ẩn mờ dần 0,3 s (đổi được, 0 = cắt ngay). Đầu danh sách = lớp trên cùng.
- Đếm ngược theo thời lượng: *Bắt đầu / Tạm dừng / Đặt lại*; hết giờ dừng ở 00:00.
- Chọn font, cỡ (px theo độ phân giải Kênh), đậm/nghiêng, màu chữ/viền/nền (kể cả độ trong suốt), căn lề, vị trí và kích thước hộp (0–1). Chữ không vừa hộp bị cắt và dock báo cảnh báo.
- **Khoá vận hành:** vẫn hiện/ẩn và điều khiển đồng hồ; không thêm/xoá/sửa. Sửa đổi có Hoàn tác; hiện/ẩn và đồng hồ thì không.
- Trong ô nhập của dock, **Enter** chỉ xác nhận giá trị, không CUT. Ở chỗ khác trong dock, phím tắt live (Space, Enter, B, 1–9) vẫn chạy như thường.
- Lưu trong project (kể cả overlay đang hiện: mở lại là hiện ngay; đồng hồ mở lại ở trạng thái dừng, đầy đủ). Logo lưu đường dẫn tới file gốc; mất file thì overlay đó trống, project vẫn mở. Font không có trên máy khác sẽ được thay thế.
- Chưa có: điều khiển overlay qua HTTP/OSC, chữ chạy sang phải/lên xuống, kéo chuột để đặt vị trí.

## Multiview
*View > Multiview…* mở một cửa sổ lưới: hàng trên là **Preview + Program của từng kênh**, bên dưới là **một ô cho mỗi Scene** của kênh đang chọn.
- Đỏ = đang On Air (Program; khi đang chuyển cảnh thì cả Scene đang vào cũng đỏ), xanh lá = Preview. Ô Scene có thêm nhãn chữ PGM / PVW; ô Program có nhãn ON AIR (tắt khi FTB đen hẳn).
- Bấm ô Scene = đưa vào **Preview**. Bấm ô PVW/PGM của kênh khác = chọn kênh đó. **Không thao tác chuột nào trong Multiview đưa hình lên Program** (kể cả nhấp đúp hay khi nút AUTO đang bật).
- Phím tắt của cửa sổ chính vẫn chạy khi đang chọn Multiview: Space = TAKE, Enter = CUT, B = BLACK ALL, 1–9 = Preview. Khi đang Khoá vận hành vẫn dùng được.
- Kéo cửa sổ sang màn hình bất kỳ rồi bấm **F11** (hoặc nút trên cửa sổ) để phóng toàn màn hình, **Esc** để thoát. Bị từ chối trên màn hình đang có Output toàn màn hình. Thu nhỏ cửa sổ chính thì Multiview ẩn theo.
- Ảnh trong lưới là ảnh thu nhỏ (PVW/PGM ~15 lần/giây, rộng 480 px; ô Scene tối đa ~7 lần/giây, 320 px, càng nhiều Scene càng thưa) nên không dùng để soi nét hay đồng bộ hình. Ảnh không gồm hiệu chỉnh/cắt/Đen/Dừng riêng của từng Output. Đóng Multiview thì không tốn thêm gì khi render.

## M6 them (hieu nang va phat hanh Windows)
- **NDI tat trong ban phat hanh:** CMake `-DLUMACAST_ENABLE_NDI=OFF` (CI/release dat san) -> khong co ma NDI trong file thuc thi, giao dien an muc NDI (vi EULA NDI SDK buoc EULA rieng; xem `docs/SPEC.md` muc 5.1). **Mac dinh OFF va bi khoa**: bat NDI can `-DLUMACAST_ENABLE_NDI=ON -DLUMACAST_NDI_EULA_ACCEPTED=I_ACCEPT_NDI_SDK_EULA` sau khi tu doc/chap nhan EULA cua NDI SDK (ctest `live`, `live_ui` chi chay khi ON). Ma NDI van nam trong repo de nguoi co chuyen mon xem; can ban co NDI thi lien he tac gia.
- **Doi mau YUV->RGB bang shader tren GPU** (mac dinh): plane Y/UV (8/10 bit, NV12/P010, 4:2:0/4:2:2/4:4:4, BT.601/709/2020, gioi han/day du) len GPU thay vi `sws_scale` RGBA tren CPU. `--no-gpu-yuv` / `LUMACAST_GPU_YUV=0` ve duong CPU.
- **Giai ma phan cung D3D11VA** (Windows, mac dinh `auto`): `--hwdec auto|off|<ten thiet bi FFmpeg>` / `LUMACAST_HWDEC` / khoa cai dat `video/hwdec`. Khong dung duoc -> tu ve phan mem. **Chua chay voi D3D11VA that** (T-HWDEC).
- **HAP** (Hap, Hap Alpha, Hap Q): upload khoi DXT thang len GPU, khong giai ma CPU. `LUMACAST_HAP=0` tat.
- **Upload bat dong bo qua PBO** (`LUMACAST_NO_PBO=1` tat), thumbnail monitor doc bat dong bo, bo shader hieu chinh khi Output khong hieu chinh.
- **Benchmark lap lai duoc:** `lumacast --benchmark "outputs=4,size=3840x2160,source=synthetic-yuv,seconds=10" --bench-json ket_qua.json` (p50/p99/roi khung tung giai doan; so tren llvmpipe KHONG dai dien GPU that - `docs/TEST-PLAN.md` muc 4). `GET /api/state` -> `engine.stages`.
- **Phat hanh:** `cpack --config build/CPackConfig.cmake -C Release` -> NSIS + ZIP (Qt qua windeployqt, FFmpeg DLL; **khong kem NDI**, co rao chan `cmake/CheckNoNdi.cmake`). `THIRD-PARTY-NOTICES.txt` (Qt/FFmpeg LGPL lien ket dong). Help > Gioi thieu co dong "NDI® is a registered trademark of Vizrt NDI AB.". CI: `.github/workflows/ci.yml`, `release.yml` (chua chay tren GitHub).
- **Co tinh nang theo goi** doc tu `license.json` (mau `license.example.json`; khong co file = khong khoa gi). Chinh sach phan goi chua chot (`docs/SPEC.md` muc 5, cau hoi mo 6).
- **Chua kiem chung:** D3D11VA that, hieu nang 4 Output 4K tren GPU that, PBO co loi that tren GPU that khong, trinh cai tren Windows sach, CI tren GitHub (T-HWDEC, T-HAP, T-PERF, T-INST, T-LIC).

## M5b them (NDI va camera)
- **Nguon truc tiep** (NDI, camera): khong co dong ho file; chi giu khung moi nhat; **mat nguon -> giu khung cuoi + bao "Mat tin hieu" + tu noi lai** (khong bao gio treo render). Trang thai o Media Pool (● xanh/vang/do, ○ xam = khong kha dung) va `GET /api/sources`.
- **NDI vao**: *File > Them nguon NDI...* (liet ke nguon tren mang, hinh + tieng; tieng di qua mixer voi **bu troi dong ho** bang resample thich ung). **NDI ra**: *File > Them Output NDI...* phat Program cua mot Channel (kem tieng) ra mang. Nap dong runtime NDI - **khong kem NDI SDK/runtime**; may chua cai se bao "Chua cai NDI Runtime" va moi thu khac chay binh thuong. Dieu khoan NDI (dang ky, ghi "NDI®", khong phan phoi runtime): `docs/SPEC.md` muc 5.1. **ABI chua doi chieu voi NDI that.**
- **Camera/capture**: *File > Them camera...* (Windows: Media Foundation, chi Win32/COM; chon thiet bi + do phan giai/fps; rut cam tu noi lai). Linux/macOS: chua ho tro (bao ro). Chua co tieng tu camera.
- Bien moi truong dev: `LUMACAST_NDI_BACKEND=fake` / `LUMACAST_CAMERA_BACKEND=fake` (nguon gia, khong can NDI/camera), `LUMACAST_NDI_DISABLE=1` (tat han NDI), `LUMACAST_NDI_LIB=<duong dan>` (chi dinh runtime).
- **Chua kiem chung:** NDI that, Media Foundation that, do tre < 3 khung tren mang, NDI ra mo bang phan mem khac (xem `docs/TEST-PLAN.md`: T-NDI-*, T-CAM*).

## M5a them (am thanh theo Channel)
- Video co tieng duoc giai ma (FFmpeg + swresample -> float32 stereo 48 kHz); moi Channel mot bus, mixer chay thread rieng. Dock **Am thanh**: nut *Tat tieng*, thanh am luong 0-150%, meter peak/RMS tung Channel + master, chon thiet bi ra (luu trong project). Am luong rieng tung Layer o Inspector (o "tieng").
- **Crossfade am thanh theo dung tien do transition** (ca T-bar; Dip im o giua; FTB keo ca tieng). **Hinh chay theo dong ho am thanh** khi nguon co tieng; lap noi lien khong cat tieng.
- Windows: **WASAPI** (shared, event-driven; chi Win32/COM, khong can Qt Multimedia). Linux/macOS: backend **Null** (khong ra loa, dung de chay test; CoreAudio de sau). `--no-audio` de khong mo thiet bi.
- Dieu khien ngoai: `POST /api/channels/{n}/mute|volume`, `GET /api/audio`, OSC `/lumacast/channel/{n}/mute|volume` (lenh live, van chay khi khoa van hanh).
- **Chua kiem chung tren Windows that:** WASAPI, nghe bang tai, lech hinh-tieng sau 30 phut (xem `docs/TEST-PLAN.md`: T-AUD-*). Chua co: moi Channel mot thiet bi, file chi co am thanh, LUFS.

## M5c them (dieu khien ngoai va van hanh)
- **Dieu khien ngoai** (Cai dat > Cai dat van hanh): HTTP REST + SSE (`GET /` co trang web cho dien thoai) va OSC/UDP. **Mac dinh tat**, chi `127.0.0.1`; nghe LAN phai bat ro va **bat buoc token**. Chi dung Qt Network (khong can Qt WebSockets). Xem `docs/CONTROL-API.md` (endpoint, ma loi, OSC, bao mat, vi du `curl`).
- **Khoa van hanh** (nut tren thanh tren, Ctrl+L): vo hieu moi dieu khien SUA, giu dieu khien live; mo khoa bang giu nut 1 giay.
- **Khoi dong cung Windows** va **tu mo lai project gan nhat** (tat duoc); file mat -> chay binh thuong kem thong bao.
- **Hoan tac** cho nhap media / tao Scene va thuoc tinh Scene. **Timeline**: zoom/cuon ngang, chon nhieu keyframe, keo nhom, Ctrl+C / Ctrl+V.
- MIDI va phim tat tu dat: xem muc *Phím tắt và MIDI* o tren. (NDI, camera: xem M5b.)

## Playlist / slide ảnh tự chuyển
Dock **Playlist** (tab cạnh *Media Pool* và *Timeline*): một danh sách mục tự chạy lần lượt lên Program. **Mỗi kênh một playlist**, các kênh chạy độc lập.

- **Mục** là một Scene (ảnh/video nhập vào đều là một Scene). *Thêm Scene…* lấy Scene có sẵn; *Thêm file…* hoặc **kéo thả file vào khung Playlist** thì nhập media (thêm Scene như bình thường) rồi thêm vào cuối danh sách.
- **Thời gian:** mục không phải video giữ đúng *Thời lượng ảnh* (mặc định 8 s, tính sau khi chuyển vào xong); mục video để *Tự động* thì **phát từ đầu và chuyển khi phát hết** (hiệu ứng bắt đầu sớm để kết thúc đúng lúc video hết). Mỗi mục đặt được thời lượng riêng. Tính theo đồng hồ thật.
- **Hiệu ứng** riêng của playlist (Fade, Cut, Dip, Wipe, Push/Slide/Zoom/Luma và plug-in Transition; không có Stinger), lưu cùng project, không phụ thuộc thanh chuyển. Playlist **không đụng tới Preview**.
- **Điều khiển:** Chạy / Tạm dừng / Dừng / Mục trước / Mục sau, nhấp đúp một mục để chạy từ đó; khung hiện mục đang phát và thời gian còn lại. Chỉ lên Program khi bạn bấm Chạy; sửa danh sách không đổi Preview/Program. Dừng giữ nguyên hình đang phát.
- **Bạn can thiệp thì playlist tạm dừng:** CUT, TAKE, T-bar, bấm Scene khi AUTO bật, nhấp đúp Scene, hay lệnh CUT/TAKE từ HTTP/OSC. Hình của bạn được giữ; bấm *Chạy* thì mục đang dở quay lại Program rồi đi tiếp. Chỉ chọn Scene vào Preview thì playlist chạy tiếp. Đen, Dừng hình, FTB không ảnh hưởng playlist.
- **Mục lỗi** (video không đọc được, không ra khung hình trong 3 s, Scene đã bị gỡ) được bỏ qua, có thông báo ở thanh trạng thái và log; mọi mục đều lỗi thì playlist dừng. Video chỉ được đưa lên khi đã có khung hình đầu.
- Sửa danh sách hoàn tác được (Ctrl+Z) và bị khoá khi **Khoá vận hành**; điều khiển chạy vẫn dùng được. Điều khiển ngoài: `POST /api/channels/{n}/playlist/play|pause|stop|next|prev`, OSC `/lumacast/playlist/{n}/play…` (`docs/CONTROL-API.md`).
- **Giới hạn:** *Tạm dừng* chỉ ngừng chuyển mục, video đang hiện vẫn lặp (nguồn video chưa tạm dừng được). Tắt *Lặp lại* thì hết danh sách playlist dừng và giữ mục cuối (nếu là video, video đó tiếp tục lặp). Video được tua về đầu khi playlist đưa lên, kể cả khi cùng file đó đang hiện ở kênh khác. File media bị thiếu **lúc mở project** vẫn làm mở project thất bại như trước. Chưa thử tay trên màn LED (T-PL).

## M4b them (hoat hoa)
- Layer: **xoay**, **cat (crop)** theo hinh da xoay, **blend** Add/Multiply/Screen. O co vien vang = dang co keyframe.
- **Timeline** (tab canh Media Pool): Phat/Tam dung/Ve 0/Tua, lap, thoi luong; 6 lan keyframe (X, Y, Rong, Cao, Alpha, Xoay). Nhap doi them key, nut ◆ them/xoa tai playhead, keo key, Delete, chuot phai doi kieu noi suy (tuyen tinh/em/giu bac). Sua gia tri o nhap khi da co keyframe se tu dat key. Hoan tac duoc.
- Scene **tu phat lai tu dau khi len Program** (tat duoc). Timeline canh bao khi Scene dang chon dang o PROGRAM.

## M4a them (Effect, transition, plug-in)
- Tab **Effect** (Inspector): gan chuoi Effect len Layer - lam mo, phat sang, chinh mau, tach RGB, pixelate, nhieu hat; tham so tu sinh giao dien; hoan tac duoc; luu trong project.
- Transition moi: Push, Slide, Zoom, Luma wipe, **Stinger** (video alpha), chon easing; plug-in quet xoay di kem.
- **Plug-in shader**: tha thu muc (plugin.json + file .frag) vao `plugins/` cua LumaCast hoac `%AppData%\LumaCast\plugins` -> tu nap lai khi sua file. Dock **Plug-in** liet ke, bat/tat, bao loi. Xem `docs/PLUGIN-SDK.md`.

## M3c them (do tin cay)
- Output toan man hinh nhan dien man theo **ten**; mat man -> bao dung Output, cua so KHONG nhay sang man khac, man quay lai thi tu hien lai dung man.
- Watchdog render thread, thanh trang thai hien p50/p99 thoi gian khung; log ra `%AppData%\LumaCast\logs\lumacast.log` (xoay vong).
- Nut "Mau" (tung Output) va "Mau thu tat ca Output": luoi, thanh mau, thuoc diem anh, toa do 4 goc dung do phan giai that.

## M3b them
- Hoan tac / Lam lai (Ctrl+Z, Ctrl+Shift+Z / Ctrl+Y, menu Edit) cho moi chinh sua Layer va Output; keo lien tuc mot o nhap = mot buoc.
- Panel Layer: danh sach keo-tha doi thu tu, tick an/hien, thuoc tinh cua layer dang chon, them/xoa.
- T-bar keo tay (keo het = hoan tat, ve 0 = huy) va nut AUTO (bat: bam Scene = chuyen thang len Program).
- Chua co: timeline / keyframe (can he thong animation trong engine -> M4).

## M3a them (giao dien Live Control)
- Cua so chinh toi: monitor Preview (vien xanh) / Program (vien do, ON AIR), Scene list, Media Pool (anh thu nho, nhap doi de tao Scene),
  Outputs (den trang thai, FPS tung output, Den / Dung hinh), Inspector (Layer, Output), thanh trang thai (GPU, FPS, roi khung).
- Thanh chuyen: CUT, TAKE (Fade/Dip/Wipe + thoi luong), BLACK ALL, FTB (Fade-To-Black cho kenh dang chon).
- Phim tat: Space = TAKE, Enter = CUT, B = BLACK ALL, 1..9 = chon Preview, Ctrl+S = Luu, Ctrl+O = Mo.
- File: Mo / Luu / Luu thanh / Gan day; tu dong luu moi 60 giay (`<project>.autosave`), hoi khoi phuc khi mo lai neu ban autosave moi hon.
- Bo cuc dock luu va khoi phuc; View > Dat lai bo cuc. Hoi xac nhan khi thoat.

## M2 them
- Luu/mo project JSON: `lumacast --project file.json`, `--save file.json`, nut "Luu project" (Ctrl+S). Media luu duong dan tuong doi.
- Video wall: `lumacast --wall 4x1 --cell 1920x1080` (canvas 7680x1080 chia cho 4 output bang crop).
- Hieu chinh mau tung Output: sang, tuong phan, gamma, gain R/G/B (tab Output, doi truc tiep).
- Nhieu Layer trong Scene: vi tri/kich thuoc/alpha/fit/an-hien/thu tu, them/xoa (tab Scene / Layer).
- Mapping: crop vung Channel len tung Output, chinh truc tiep tren tab Output.

## M1 gom
Engine render GPU (1 render thread, nhieu Output), Scene/Layer (Fit/Fill/Stretch), Channel Program/Preview,
transition Cut/Fade/Dip/Wipe x4, Output scale/xoay/crop, Black tung output, nguon: anh, mau, test pattern, video FFmpeg.
Chua co: am thanh, NDI/camera, plug-in, dieu khien ngoai (M3-M5). (Bu khe/bezel: xem muc "Bu khe video wall" o tren.)
