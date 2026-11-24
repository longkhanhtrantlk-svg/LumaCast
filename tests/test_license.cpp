// M6 giay phep phat hanh (khong can GL/man hinh): Qt dong, FFmpeg LGPL, ghi nhan NDI, THIRD-PARTY-NOTICES, khong NDI trong cay ma nguon.
#include "AboutInfo.h"
#include "core/Features.h"
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <iostream>

using namespace lc;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << std::string(msg) << "  (" #cond ")\n"; } else std::cout << "ok:   " << std::string(msg) << "\n"; } while (0)

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    // ===== Gioi thieu / ghi nhan thuong hieu / giay phep thu vien =====
    {
        const QString h = aboutHtml();
        CHECK(h.contains("NDI®") && h.contains("registered trademark of Vizrt NDI AB"), "Gioi thieu: co dong ghi nhan 'NDI® is a registered trademark of Vizrt NDI AB.' (SPEC 5.1)");
        CHECK(h.contains("https://ndi.video"), "Gioi thieu: co lien ket toi ndi.video");
        CHECK(h.contains("Qt") && h.contains("FFmpeg") && h.contains("LGPL") && h.contains("THIRD-PARTY-NOTICES"), "Gioi thieu: liet ke Qt/FFmpeg, giay phep LGPL, tro toi THIRD-PARTY-NOTICES");
        CHECK(h.contains("không kèm") || h.contains("không kèm theo"), "Gioi thieu: noi ro NDI khong kem theo");
        const LibraryInfo li = libraryInfo();
        std::cout << "      Qt " << li.qtVersion.toStdString() << (li.qtShared ? " (dong)" : " (TINH)") << " | " << li.ffmpegVersion.toStdString() << " | giay phep FFmpeg: "
                  << li.ffmpegLicense.toStdString() << "\n";
        CHECK(li.qtShared, "Qt lien ket DONG (dieu kien de dung LGPL)");
        CHECK(!li.ffmpegLicense.isEmpty() && !li.ffmpegVersion.isEmpty(), "Doc duoc giay phep/phien ban FFmpeg luc chay");
        if (qEnvironmentVariableIntValue("LUMACAST_REQUIRE_LGPL")) {
            CHECK(li.ffmpegLgpl, "FFmpeg la ban LGPL, khong GPL/nonfree (LUMACAST_REQUIRE_LGPL=1) - " + li.ffmpegLicense.toStdString());
        } else {
            std::cout << "      (bo qua ep LGPL; dat LUMACAST_REQUIRE_LGPL=1 khi dong goi: ban FFmpeg o may nay " << (li.ffmpegLgpl ? "la LGPL" : "KHONG phai LGPL - khong duoc phan phoi") << ")\n";
        }
        const Entitlements bs = Entitlements::preset("basic");
        Entitlements::setCurrent(bs);
        CHECK(aboutHtml().contains("Gói basic") && aboutHtml().contains("không có"), "Gioi thieu: hien goi giay phep dang dung");
        Entitlements::setCurrent(Entitlements());
    }

    // ===== THIRD-PARTY-NOTICES + khong kem NDI =====
    {
        const QString root = QString::fromUtf8(LUMACAST_SOURCE_DIR);
        QFile nf(root + "/THIRD-PARTY-NOTICES.txt");
        const bool open = nf.open(QIODevice::ReadOnly);
        const QString t = open ? QString::fromUtf8(nf.readAll()) : QString();
        CHECK(open && t.contains("Qt") && t.contains("LGPL") && t.contains("FFmpeg") && t.contains("Snappy"), "THIRD-PARTY-NOTICES.txt: co Qt, FFmpeg, LGPL, ghi chu Snappy tu viet");
        CHECK(t.contains("NDI®") && t.contains("Processing.NDI.Lib") && t.contains("KHÔNG"), "THIRD-PARTY-NOTICES.txt: NDI - ghi nhan thuong hieu + khong kem runtime");
        bool ndiFile = false;
        QDirIterator it(root, {"*NDI.Lib*", "libndi*", "*.dll"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) { const QString f = it.next(); if (!f.contains("/build") && !f.contains("/.git/")) { ndiFile = true; std::cerr << "NDI/DLL trong cay ma nguon: " << f.toStdString() << "\n"; } }
        CHECK(!ndiFile, "Cay ma nguon KHONG chua thu vien NDI hay DLL nao (SPEC 5.1)");
    }

    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
