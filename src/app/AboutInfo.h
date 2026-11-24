#pragma once
#include <QString>

namespace lc {

// Thong tin phien ban/giay phep thu vien cho hop thoai Gioi thieu va kiem thu (M6).
struct LibraryInfo {
    QString qtVersion;          // phien ban Qt luc chay
    bool qtShared = false;      // Qt lien ket DONG (dieu kien LGPL)
    QString ffmpegVersion;      // vd "libavcodec 60.31.102"
    QString ffmpegLicense;      // avutil_license(): "LGPL version 2.1 or later" | "GPL version 2 or later" | "nonfree and unredistributable"
    QString ffmpegConfig;       // avutil_configuration() (mot dong)
    bool ffmpegLgpl = false;    // dung LGPL (khong GPL, khong nonfree): duoc phep phan phoi kem
};
LibraryInfo libraryInfo();

// HTML cho QMessageBox::about: phien ban, giay phep tinh nang, thu vien + giay phep, ghi nhan thuong hieu NDI (SPEC 5.1).
QString aboutHtml();
// Dong ghi nhan thuong hieu NDI hien trong Gioi thieu (de kiem thu).
QString ndiTrademarkNotice();

} // namespace lc
