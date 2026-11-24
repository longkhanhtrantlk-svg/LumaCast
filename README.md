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
Phim tat (kenh 1): `1..9` chon Preview, `Space` = AUTO, `Enter` = CUT.

## M6 them (hieu nang va phat hanh Windows)
- **NDI tat trong ban phat hanh:** CMake `-DLUMACAST_ENABLE_NDI=OFF` (CI/release dat san) -> khong co ma NDI trong file thuc thi, giao dien an muc NDI (vi EULA NDI SDK buoc EULA rieng; xem `docs/SPEC.md` muc 5.1). Mac dinh ON de phat trien/kiem thu (ctest `live`, `live_ui` chi chay khi ON).
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
- Chua co (de dot sau): MIDI. (NDI, camera: xem M5b.)

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
Chua co: am thanh, NDI/camera, plug-in, dieu khien ngoai (M3-M5). Chua co bu khe (bezel) giua cac man trong wall.
