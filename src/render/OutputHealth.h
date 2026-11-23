#pragma once
#include "core/Types.h"
#include <QStringList>

namespace lc {

// Tinh trang cua mot Output (de bao cho operator va de tu phuc hoi).
enum class OutputHealth {
    Ok,
    NoScreen,       // man hinh duoc cau hinh khong con (rut cap/tat man) -> KHONG duoc hien hinh len man khac
    Hidden,         // cua so khong hien (bi thu nho/an/khong hien duoc)
    WrongScreen     // dang nam tren man hinh khac voi man da cau hinh (Qt tu doi cua so khi rut man)
};

// Chi so man hinh ma Output nay can dung, hoac -1 neu man do khong con.
// screenName (neu co) uu tien hon screenIndex vi chi so thay doi khi rut mot man giua chung.
int resolveScreenIndex(const OutputConfig& cfg, const QStringList& screenNames);

OutputHealth evaluateOutput(const OutputConfig& cfg, const QStringList& screenNames, int windowScreenIndex, bool exposed);

QString healthText(OutputHealth h, const OutputConfig& cfg);

} // namespace lc
