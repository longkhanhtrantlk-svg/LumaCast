#include "AboutInfo.h"
#include "core/I18n.h"
#include "core/Features.h"
#include "live/Ndi.h"
#include <QCoreApplication>
#include <QString>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

namespace lc {

LibraryInfo libraryInfo() {
    LibraryInfo i;
    i.qtVersion = QString::fromLatin1(qVersion());
#ifdef QT_STATIC
    i.qtShared = false;                  // Qt tinh nang duoc lien ket tinh -> khong dung dieu kien LGPL (phai cho nguoi dung thay the duoc)
#else
    i.qtShared = true;
#endif
    const unsigned v = avcodec_version();
    i.ffmpegVersion = QString("libavcodec %1.%2.%3").arg(v >> 16).arg((v >> 8) & 255).arg(v & 255);
    i.ffmpegLicense = QString::fromLatin1(avutil_license());
    i.ffmpegConfig = QString::fromLatin1(avutil_configuration());
    // avutil_license() la "LGPL version 2.1 or later" khi build khong co --enable-gpl/--enable-nonfree; GPL/nonfree co chuoi khac.
    i.ffmpegLgpl = i.ffmpegLicense.startsWith("LGPL") && !i.ffmpegConfig.contains("--enable-gpl") && !i.ffmpegConfig.contains("--enable-nonfree");
    return i;
}

QString ndiTrademarkNotice() {
    return "NDI® is a registered trademark of Vizrt NDI AB.";
}

QString aboutHtml() {
    const LibraryInfo li = libraryInfo();
    const Entitlements& e = Entitlements::current();
    QString h = QString(LC_TR("<b>LumaCast</b> — LED Display Controller<br>Phiên bản %1<br><br>")).arg(QCoreApplication::applicationVersion().isEmpty() ? "0.1.0" : QCoreApplication::applicationVersion());
    h += QString(LC_TR("<b>Giấy phép tính năng:</b> %1<br><br>")).arg(e.summary().toHtmlEscaped());
    h += LC_TR("<b>Thư viện bên thứ ba</b> (liên kết động, thay thế được):<br>");
    h += QString("• Qt %1 — GNU LGPL v3 (qt.io)%2<br>").arg(li.qtVersion, li.qtShared ? "" : LC_TR(" <i>(CẢNH BÁO: bản Qt liên kết tĩnh)</i>"));
#ifdef Q_OS_WIN
    h += "• Velopack — MIT (github.com/velopack/velopack)<br>";
#endif
    h += QString("• FFmpeg (%1) — %2%3<br>").arg(li.ffmpegVersion, li.ffmpegLicense.toHtmlEscaped(),
                                                li.ffmpegLgpl ? "" : LC_TR(" <i>(CẢNH BÁO: không phải bản LGPL)</i>"));
    h += LC_TR("Toàn văn giấy phép và nguồn: xem THIRD-PARTY-NOTICES.txt trong thư mục cài đặt.<br><br>");
    if (ndiCompiledIn()) {
        h += ndiTrademarkNotice().toHtmlEscaped() + " <a href=\"https://ndi.video\">ndi.video</a><br>";
        h += LC_TR("NDI không kèm theo LumaCast; cài NDI Tools/Runtime từ ndi.video để dùng nguồn/Output NDI.");
    } else {
        h += LC_TR("Bản này không có NDI.");
    }
    return h;
}

} // namespace lc
