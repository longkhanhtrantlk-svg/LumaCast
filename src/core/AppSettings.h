#pragma once
#include <QSettings>

namespace lc {

// Cai dat theo may/nguoi dung cua LumaCast. DUNG HAM NAY thay cho QSettings("LumaCast", "LumaCast"):
// constructor (to chuc, ung dung) luon dung NativeFormat nen BO QUA QSettings::setDefaultFormat() -> test khong co lap duoc
// (ghi de len registry/cau hinh that cua may). Ham nay theo QSettings::defaultFormat():
// san pham = NativeFormat (registry tren Windows, cung vi tri nhu truoc), test / LUMACAST_CONFIG_DIR = IniFormat + setPath().
inline QSettings appSettings() { return QSettings(QSettings::defaultFormat(), QSettings::UserScope, "LumaCast", "LumaCast"); }

} // namespace lc
