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
Ky vong: tat ca test pass (9 bo: render, ui, reliability, effects, animation, control, ops, audio, audio_engine). Neu fail o "Khong co OpenGL 3.3": cap nhat driver GPU (khong dung may ao/Remote Desktop).

## 4. Test thu cong (che do cua so, 1 man hinh la du)
    build\lumacast.exe -o 960x540 -o 540x960 -m clip.mp4
Kiem tra: 2 cua so hien, bang dieu khien 3 tab; bam `1`/`2`/`3` chon Preview, `Space` = AUTO, `Enter` = CUT;
FPS o duoi bang dieu khien ~60; video chay muot, lap lai; tab Output doi scale/xoay/hieu chinh mau thay doi ngay.

Wall + luu/mo:
    build\lumacast.exe --wall 3x1 --cell 480x270 --save wall.json
    build\lumacast.exe --project wall.json

## 5. Test fullscreen nhieu man hinh LED
    build\lumacast.exe -o 1920x1080@0 -o 3840x2160@1 -o 1920x1080@2/2 --fullscreen -m clip.mp4 --scale fit
`WxH@man_hinh/kenh`: `@N` la chi so man hinh Windows (0 = man chinh), `/K` la kenh. Chi so vuot so man hinh dang co thi Output do KHONG hien (bao "MAT MAN HINH" trong bang Outputs va log) - khong bao gio roi sang man khac. Ten man hinh duoc ghi nho khi chay lan dau va luu vao project.
Esc tren cua so output thoat fullscreen. Neu nham man hinh, mo Windows Settings > Display de xem so thu tu.

## 5b. Test am thanh (M5a) - can loa/tai nghe
Build can FFmpeg co **swresample** (`vcpkg install ffmpeg:x64-windows` da gom san). Chay `lumacast.exe -m clip_co_tieng.mp4 -m clip2.mp4`:
1. Dock **Am thanh**: chon thiet bi ra, nghe tieng; *Tat tieng* tung Channel; keo thanh am luong; meter nhay theo tieng.
2. TAKE Fade/Dip, keo T-bar: tieng chuyen dung nhu hinh, khong ro/vo.
3. Cho clip co tieng chay lap >= 30 phut, so tieng/hinh (xem T-AUD-SYNC trong `TEST-PLAN.md`).
4. Rut tai nghe / doi thiet bi mac dinh khi dang chay (T-AUD-HOT). `curl http://127.0.0.1:8088/api/audio` (neu bat HTTP) cho `underruns`.
Neu khong co tieng: kiem tra dong "Am thanh:" trong `%AppData%\LumaCast\logs\lumacast.log` (thiet bi hong thi LumaCast tu dung backend Null va hinh van chay).

## 6. Bao loi
Gui: ban build (commit), lenh da chay, thong bao loi, driver GPU. Neu build loi, dan nguyen van 20 dong loi dau tien.

## 7. Loi thuong gap
**vcpkg: `Unable to find a valid Visual Studio instance` / `Could not locate a complete Visual Studio instance`**
Chua co bo bien dich C++. Cai (hoac Modify) Visual Studio / Build Tools voi workload *Desktop development with C++* (kem Windows SDK):

    winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"

Kiem tra da nhan chua (phai in ra mot duong dan):

    & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath

Mo cua so PowerShell moi roi chay lai `vcpkg install ffmpeg:x64-windows` (lan dau 10-30 phut).
