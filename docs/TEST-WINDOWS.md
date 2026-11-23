# Test LumaCast tren Windows

## 1. Cai cong cu (1 lan)
1. **Visual Studio 2022** (hoac Build Tools) voi workload "Desktop development with C++".
2. **CMake** >= 3.21 va **Ninja** (kem theo VS, hoac `winget install Kitware.CMake Ninja-build.Ninja`).
3. **Qt 6** (>= 6.4) bang Qt Online Installer: chon `Qt 6.x > MSVC 2022 64-bit` (khong can Qt Quick/WebEngine).
4. **vcpkg** cho FFmpeg:
       git clone https://github.com/microsoft/vcpkg C:\vcpkg
       C:\vcpkg\bootstrap-vcpkg.bat
       C:\vcpkg\vcpkg install ffmpeg:x64-windows

## 2. Build (mo "x64 Native Tools Command Prompt for VS 2022")
    git clone https://github.com/longkhanhtrantlk-svg/LumaCast && cd LumaCast
    git checkout m2
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
      -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2022_64
    cmake --build build
    C:\Qt\6.x.x\msvc2022_64\bin\windeployqt.exe build\lumacast.exe build\lumacast_tests.exe build\lumacast_ui_tests.exe

## 3. Test tu dong
    ctest --test-dir build --output-on-failure
Ky vong: 2/2 pass. Neu fail o "Khong co OpenGL 3.3": cap nhat driver GPU (khong dung may ao/Remote Desktop).

## 4. Test thu cong (che do cua so, 1 man hinh la du)
    build\lumacast.exe -o 960x540 -o 540x960 -m clip.mp4
Kiem tra: 2 cua so hien, bang dieu khien 3 tab; bam `1`/`2`/`3` chon Preview, `Space` = AUTO, `Enter` = CUT;
FPS o duoi bang dieu khien ~60; video chay muot, lap lai; tab Output doi scale/xoay/hieu chinh mau thay doi ngay.

Wall + luu/mo:
    build\lumacast.exe --wall 3x1 --cell 480x270 --save wall.json
    build\lumacast.exe --project wall.json

## 5. Test fullscreen nhieu man hinh LED
    build\lumacast.exe -o 1920x1080@0 -o 3840x2160@1 -o 1920x1080@2/2 --fullscreen -m clip.mp4 --scale fit
`WxH@man_hinh/kenh`: `@N` la chi so man hinh Windows (0 = man chinh), `/K` la kenh. Chi so vuot so man hinh se dung man cuoi.
Esc tren cua so output thoat fullscreen. Neu nham man hinh, mo Windows Settings > Display de xem so thu tu.

## 6. Bao loi
Gui: ban build (commit), lenh da chay, thong bao loi, driver GPU. Neu build loi, dan nguyen van 20 dong loi dau tien.
