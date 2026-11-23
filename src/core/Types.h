#pragma once
#include <QRectF>
#include <QSize>
#include <QString>

namespace lc {

enum class ScaleMode { Fit, Fill, Stretch, Pixel };   // Pixel = 1:1, can giua
enum class LayerFit  { Stretch, Fit, Fill };
enum class TransitionType { Cut, Fade, DipToBlack, WipeLeft, WipeRight, WipeUp, WipeDown };

inline const char* transitionName(TransitionType t) {
    switch (t) {
    case TransitionType::Cut: return "Cut";
    case TransitionType::Fade: return "Fade";
    case TransitionType::DipToBlack: return "Dip to black";
    case TransitionType::WipeLeft: return "Wipe left";
    case TransitionType::WipeRight: return "Wipe right";
    case TransitionType::WipeUp: return "Wipe up";
    case TransitionType::WipeDown: return "Wipe down";
    }
    return "?";
}

// Hieu chinh mau/do sang theo tung man hinh (cac tam LED khac lo khac nhau). Mac dinh = khong doi.
struct Calibration {
    float brightness = 1.0f;   // nhan
    float contrast = 1.0f;     // quanh 0.5
    float gamma = 1.0f;        // out = in^(1/gamma)
    float gainR = 1.0f, gainG = 1.0f, gainB = 1.0f;
};

// Mot man hinh LED vat ly (mot cua so output).
struct OutputConfig {
    QString name;
    int channel = 0;            // Channel cap hinh cho output nay
    int screenIndex = -1;       // chi so man hinh he dieu hanh (khi fullscreen)
    bool fullscreen = false;
    QSize size{1920, 1080};     // kich thuoc cua so khi o che do cua so
    ScaleMode scale = ScaleMode::Fit;
    int rotation = 0;           // 0 / 90 / 180 / 270 (theo chieu kim dong ho)
    QRectF crop{0, 0, 1, 1};    // vung cua Channel, toa do chuan hoa, goc tren-trai (Mapping)
    Calibration cal;
};

} // namespace lc
