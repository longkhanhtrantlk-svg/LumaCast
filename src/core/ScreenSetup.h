#pragma once
#include "core/Project.h"
#include <QStringList>

namespace lc {

// Thiet lap man hinh (UI-08): kich thuoc khung tin hieu, vung LED trong khung, canh bao co gian / cat / thu nho roi phong to.

constexpr int kMinCanvasSide = 16, kMaxCanvasSide = 16384;

// Toan bo "man hinh" ma nguoi van hanh sua trong hop thoai: de ap dung va Hoan tac mot lan.
struct ScreenSetup {
    std::vector<OutputConfig> outputs;
    std::vector<QSize> canvases;                                     // theo Channel
};

// Diem anh that cua ca khung ma Output gui di: toan man hinh = do phan giai man do; cua so = size x ti le phong; NDI = size.
QSize outputFramePixels(const OutputConfig& c);
// Khung ma noi dung duoc tinh toan (canh bao 1:1): toan man hinh / NDI = nhu tren; cua so thu = size cua Output (khong tinh ti le
// phong cua Windows vi cua so chi de xem thu).
QSize outputSignalSize(const OutputConfig& c);
// Cho dat hinh trong khung `frame` diem anh: vung LED (area) neu co, khong thi ca khung. Cua so: area tinh theo size cua Output.
QRect outputPictureRect(const OutputConfig& c, QSize frame);
// So diem anh cua mot canh LED dai `meters` met voi pixel pitch `pitchMm` mm (vd. 4 m, P2.5 -> 1600).
int ledPixels(double meters, double pitchMm);

// Canh bao cho Output `index`: bi co gian (meo), bi cat, thu nho / phong to, vung LED vuot khung. `canvas` = khung hinh cua
// Channel, `frame` = outputFramePixels. Rong = hinh ra dung 1:1 (hoac dung y do cua video wall).
QStringList outputWarnings(const std::vector<OutputConfig>& outputs, int index, QSize canvas, QSize frame);
// Scene dung o kich thuoc khac khung hinh cua Channel (len Program se bi co gian / thu phong). Rong = khong co.
QString sceneSizeWarning(const std::vector<std::shared_ptr<Scene>>& scenes, QSize canvas);

// Project moi: mot Channel `canvas`, Program = den, Preview = mau thu; `outputs` rong thi mot Output 1920x1080 dang cua so.
Project makeNewProject(QSize canvas, std::vector<OutputConfig> outputs = {});

} // namespace lc
