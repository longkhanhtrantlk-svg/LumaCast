# LumaCast (M1) - LED Display Controller

Bo dieu khien man hinh LED nhieu kenh / nhieu do phan giai. Xem thiet ke: `docs/KienTruc.md`.

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
