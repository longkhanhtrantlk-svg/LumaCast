# LumaCast (M1) - LED Display Controller

Bo dieu khien man hinh LED nhieu kenh / nhieu do phan giai. Xem thiet ke: `docs/KienTruc.md`; yeu cau: `docs/SPEC.md`.

## Tài liệu
| File | Nội dung |
|---|---|
| `docs/SPEC.md` | Yêu cầu chức năng/phi chức năng, ưu tiên, tiêu chí nghiệm thu, trạng thái |
| `docs/PLAN.md` | Hiện trạng, lộ trình M3c → M7, nhiệm vụ, nghiệm thu, rủi ro, quy trình |
| `docs/KienTruc.md` | Thiết kế kỹ thuật và quyết định |
| `docs/PROJECT-FORMAT.md` | Đặc tả định dạng file project (phiên bản 1) |
| `docs/TEST-PLAN.md` | Test tự động, checklist thử tay trên Windows, benchmark |
| `docs/TEST-WINDOWS.md` | Hướng dẫn build và chạy trên Windows |

## Build
Linux (de phat trien / chay test): `apt install qt6-base-dev libqt6opengl6-dev libavcodec-dev libavformat-dev libswscale-dev libavutil-dev`
Windows (muc tieu): Qt 6 + FFmpeg qua vcpkg (`vcpkg install ffmpeg[core,avcodec,avformat,swscale]` -- ban LGPL), roi:

    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build        # test render + UI (can OpenGL 3.3; Linux khong man hinh: xvfb-run ctest)

## Chay
    lumacast -o 1920x1080@0 -o 3840x2160@1 -o 1920x1080@2/2 --fullscreen -m clip.mp4 -m logo.png --scale fit

`-o WxH[@man_hinh][/kenh]` (lap lai cho nhieu output), `--scale fit|fill|stretch|pixel`, `--fps 60`.
Phim tat (kenh 1): `1..9` chon Preview, `Space` = AUTO, `Enter` = CUT.

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
