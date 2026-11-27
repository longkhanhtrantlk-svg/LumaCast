#pragma once
#include "core/Overlay.h"
#include <QImage>
#include <QSize>
#include <vector>

namespace lc {

// Dung hinh cua mot lop phu bang QPainter (khong can GL; goi duoc tu bat ky thread nao khi da co QGuiApplication).
// Anh KHONG gom mau nen (nen to rieng luc ghep). RGBA8888 nhan san alpha.

// Tat ca nhung gi lam doi ANH: bang nhau = khong phai dung lai. Vi tri, do mo, nen, toc do... khong nam o day.
struct OverlayRasterKey {
    OverlayType type = OverlayType::Title;
    QString text;               // chuoi hien thi (dem nguoc: da gom dong ho)
    QString font;
    int fontPx = 0;
    bool bold = false, italic = false;
    QRgb color = 0, outlineColor = 0;
    double outlinePx = 0;
    OverlayAlign align = OverlayAlign::Center;
    QSize box;                  // hop cua lop phu tren dich, tinh bang diem anh (chu chay: rong = 0, khong anh huong anh)
    QString image;

    bool operator==(const OverlayRasterKey&) const = default;
};

// `target` = kich thuoc anh se ve len; `canvas` = canvas cua Channel (moi kich thuoc px trong OverlayDef tinh theo no).
OverlayRasterKey overlayRasterKey(const OverlayDef& d, int secondsLeft, QSize canvas, QSize target);

struct OverlayBitmap {
    static constexpr int kTileWidth = 2048;        // chu chay dai duoc cat thanh nhieu manh (gioi han texture cua GPU)
    std::vector<QImage> tiles;                     // trai -> phai
    QSize size;                                    // tong kich thuoc noi dung
    bool clipped = false;                          // chu khong vua hop, bi cat
};

OverlayBitmap rasterizeOverlay(const OverlayRasterKey& key);

// Vi tri mep trai cua anh trong hop (toa do hop). Chu chay: theo thoi gian da hien (vao tu mep phai, ra het mep trai roi lap).
double overlayContentX(OverlayType type, OverlayAlign align, double boxW, double contentW, double onSeconds, double speedPx);

} // namespace lc
