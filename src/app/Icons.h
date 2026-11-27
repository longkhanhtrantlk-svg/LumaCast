#pragma once
#include <QIcon>

namespace lc {

// Bieu tuong cua cac nut moi (ve bang QPainter: khong can file anh, ro net o moi ti le phong man hinh).
enum class Glyph { Import, Camera, Ndi, Play, Pause, Rewind, Loop, Speaker, Muted, Rename, Delete, Add, Screen };
QIcon glyphIcon(Glyph g);

} // namespace lc
