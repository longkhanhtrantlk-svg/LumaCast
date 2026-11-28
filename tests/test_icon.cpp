// Icon ung dung (ctest "icon"): tai nguyen trong app du cac co va dung hinh; lumacast.exe mang icon (Windows). Khong mo cua so.
#include "Icons.h"
#include <QApplication>
#include <QColor>
#include <QImage>
#include <QSet>
#include <cstdio>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "[OK]  " : "[LOI] ", what);
    if (!ok) ++failures;
}
const int kSizes[] = {16, 20, 24, 32, 40, 48, 64, 96, 128, 256};
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    const QIcon icon = lc::appIcon();
    check(!icon.isNull(), "icon nap duoc tu tai nguyen");
    const QList<QSize> sizes = icon.availableSizes();
    bool all = true;
    for (int n : kSizes) all = all && sizes.contains(QSize(n, n));
    check(all, "du cac co 16..256");

    // icon khong nen: goc trong suot, man hinh xanh nhat hang tren (#46cff9), giua la tam giac play mau trang
    const QImage big = icon.pixmap(QSize(256, 256), 1.0).toImage();
    check(big.size() == QSize(256, 256), "ve duoc co 256");
    if (!big.isNull()) {
        const QColor corner = big.pixelColor(2, 2), tile = big.pixelColor(21, 56), middle = big.pixelColor(128, 128);
        check(corner.alpha() == 0, "khong co nen (goc trong suot)");
        check(tile.alpha() == 255 && qAbs(tile.red() - 0x46) < 8 && qAbs(tile.green() - 0xcf) < 8 && qAbs(tile.blue() - 0xf9) < 8,
              "o man hinh xanh nhat hang tren");
        check(middle.alpha() == 255 && middle.red() > 245 && middle.green() > 245 && middle.blue() > 245, "nut play trang o giua");
    }

#ifdef Q_OS_WIN
    // icon nhung trong file thuc thi (Explorer, loi tat, thanh tac vu): nhom IDI_ICON1 co du cac co
    const HMODULE exe = LoadLibraryExW(reinterpret_cast<const wchar_t*>(QStringLiteral(LUMACAST_EXE).utf16()), nullptr,
                                       LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    check(exe != nullptr, "mo duoc lumacast.exe");
    if (exe) {
        const HRSRC res = FindResourceW(exe, L"IDI_ICON1", MAKEINTRESOURCEW(14) /* RT_GROUP_ICON */);
        check(res != nullptr, "lumacast.exe co icon IDI_ICON1");
        if (res) {
            // GRPICONDIR: 3 WORD dau (reserved, type, count), moi muc 14 byte, 2 byte dau la rong va cao (0 = 256)
            const auto* data = static_cast<const unsigned char*>(LockResource(LoadResource(exe, res)));
            const int count = data[4] | (data[5] << 8);
            QSet<int> inExe;
            for (int i = 0; i < count; ++i) inExe.insert(data[6 + 14 * i] ? data[6 + 14 * i] : 256);
            bool full = true;
            for (int n : kSizes) full = full && inExe.contains(n);
            check(full, "icon trong exe du cac co");
        }
        FreeLibrary(exe);
    }
#endif

    std::printf(failures ? "%d LOI\n" : "icon: OK\n", failures);
    return failures ? 1 : 0;
}
