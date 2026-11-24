#include "AboutInfo.h"
#include "core/Features.h"
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
    QString h = QString("<b>LumaCast</b> — LED Display Controller<br>Phiên bản %1<br><br>").arg(QCoreApplication::applicationVersion().isEmpty() ? "0.1.0" : QCoreApplication::applicationVersion());
    h += QString("<b>Giấy phép tính năng:</b> %1<br><br>").arg(e.summary().toHtmlEscaped());
    h += "<b>Thư viện bên thứ ba</b> (liên kết động, thay thế được):<br>";
    h += QString("• Qt %1 — GNU LGPL v3 (qt.io)%2<br>").arg(li.qtVersion, li.qtShared ? "" : " <i>(CẢNH BÁO: bản Qt liên kết tĩnh)</i>");
    h += QString("• FFmpeg (%1) — %2%3<br>").arg(li.ffmpegVersion, li.ffmpegLicense.toHtmlEscaped(),
                                                li.ffmpegLgpl ? "" : " <i>(CẢNH BÁO: không phải bản LGPL)</i>");
    h += "Toàn văn giấy phép và nguồn: xem THIRD-PARTY-NOTICES.txt trong thư mục cài đặt.<br><br>";
    h += ndiTrademarkNotice().toHtmlEscaped() + " <a href=\"https://ndi.video\">ndi.video</a><br>";
    h += "NDI không kèm theo LumaCast; cài NDI Tools/Runtime từ ndi.video để dùng nguồn/Output NDI.";
    return h;
}

} // namespace lc
