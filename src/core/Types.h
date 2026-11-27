#pragma once
#include <QRectF>
#include <QSize>
#include <QString>

namespace lc {

enum class ScaleMode { Fit, Fill, Stretch, Pixel };   // Pixel = 1:1, can giua
enum class LayerFit  { Stretch, Fit, Fill };
enum class BlendMode { Normal, Add, Multiply, Screen };

inline const char* blendId(BlendMode b) {
    switch (b) {
    case BlendMode::Normal: return "normal";
    case BlendMode::Add: return "add";
    case BlendMode::Multiply: return "multiply";
    case BlendMode::Screen: return "screen";
    }
    return "normal";
}
enum class TransitionType { Cut, Fade, DipToBlack, WipeLeft, WipeRight, WipeUp, WipeDown, Plugin, Stinger };

inline const char* transitionName(TransitionType t) {
    switch (t) {
    case TransitionType::Cut: return "Cut";
    case TransitionType::Fade: return "Fade";
    case TransitionType::DipToBlack: return "Dip to black";
    case TransitionType::WipeLeft: return "Wipe left";
    case TransitionType::WipeRight: return "Wipe right";
    case TransitionType::WipeUp: return "Wipe up";
    case TransitionType::WipeDown: return "Wipe down";
    case TransitionType::Plugin: return "Plug-in";
    case TransitionType::Stinger: return "Stinger";
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

// Loai dich den cua mot Output. Screen = cua so/man hinh LED; Ndi = phat Program cua Channel ra mang bang NDI (M5b, OUT-13).
enum class OutputKind { Screen, Ndi };

// Mot man hinh LED vat ly (mot cua so output) hoac mot dich den khac (NDI ra).
struct OutputConfig {
    OutputKind kind = OutputKind::Screen;
    QString ndiName;            // kind == Ndi: ten nguon NDI se hien tren mang (rong = dung `name`)
    bool ndiAudio = true;       // kind == Ndi: gui kem am thanh bus cua Channel
    QString name;
    int channel = 0;            // Channel cap hinh cho output nay
    int screenIndex = -1;       // chi so man hinh he dieu hanh (khi fullscreen)
    QString screenName;         // ten man hinh (QScreen::name); neu co thi UU TIEN hon screenIndex, vi chi so doi khi rut man
    bool fullscreen = false;
    QSize size{1920, 1080};     // kich thuoc cua so khi o che do cua so
    ScaleMode scale = ScaleMode::Fit;
    int rotation = 0;           // 0 / 90 / 180 / 270 (theo chieu kim dong ho)
    QRectF crop{0, 0, 1, 1};    // vung cua Channel, toa do chuan hoa, goc tren-trai (Mapping)
    // Vung LED trong tin hieu: hinh chi dat o day (diem anh, goc tren-trai), ngoai vung de den. Rong = ca khung. Dung khi bo xu ly
    // LED chi lay mot goc cua tin hieu HDMI (vd. LED 1600x800 tren tin hieu 1920x1080).
    QRect area;
    Calibration cal;
};

} // namespace lc
