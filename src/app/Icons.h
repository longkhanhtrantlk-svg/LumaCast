#pragma once
#include <QIcon>

namespace lc {

// Bieu tuong cac nut phu (ve bang QPainter: khong can file anh, ro net o moi ti le phong man hinh).
enum class Glyph {
    Import, Camera, Ndi, Play, Pause, Rewind, Loop, Speaker, Muted, Rename, Delete, Add, Screen,
    Params, Black, Freeze, Pattern, PatternAll, Refresh, Folder,
    Prev, Next, Stop, AddScene, AddFile, Remove, Up, Down,
    Eye, EyeOff, Reset, Close, Grip,
    TitleText, Ticker, Countdown, Logo,
    PosCenter, PosTopLeft, PosTopRight, PosBottomLeft, PosBottomRight,
    Fit, Fill, Stretch,
    ZoomIn, ZoomOut, ZoomFit, FullScreen, Layout
};
QIcon glyphIcon(Glyph g);

} // namespace lc
