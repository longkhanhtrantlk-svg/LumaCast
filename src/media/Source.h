#pragma once
#include "audio/AudioStream.h"
#include "render/GlHelpers.h"
#include <QImage>
#include <QString>

namespace lc {

struct TexRef {
    GLuint id = 0;     // 0 = chua co khung hinh
    int w = 0, h = 0;  // kich thuoc texture THO (chua xoay)
    int rotation = 0;  // do xoay can ap dung khi hien thi: 0/90/180/270 theo chieu kim dong ho (video dien thoai)
};

// Nguon hinh. acquire() chi duoc goi tren render thread (co context GL hien hanh).
class Source {
public:
    virtual ~Source() = default;
    virtual QString name() const = 0;
    virtual TexRef acquire(GL& gl) = 0;     // khung hinh moi nhat, dang texture GL
    // Anh thu nho cho UI (Media Pool). Goi tu thread bat ky; rong neu chua co hinh.
    virtual QImage snapshot(int maxWidth = 160) const { Q_UNUSED(maxWidth); return {}; }
    // Phat lai tu dau (Stinger). Mac dinh khong lam gi. Goi tu render thread.
    virtual void restart() {}
    // Nguon vua len Program: phat lai tu dau, hien NGAY khung dau (khong chop den, khong lo doan dang chay). Mac dinh khong lam gi.
    // Khac restart(): restart() bo khung dang hien va cho khung moi. Render thread.
    virtual void rewind() {}
    // Luong am thanh cua nguon (float32 stereo 48 kHz); nullptr = im lang (anh, mau, video khong co tieng). Mixer rut tu day.
    virtual AudioStream* audio() { return nullptr; }
    virtual double durationSeconds() const { return 0; }     // do dai (giay) neu biet, 0 neu khong (dung cho Stinger, playlist)
    virtual QString error() const { return {}; }             // loi lam nguon khong phat duoc (rong = binh thuong); goi tu thread bat ky
    virtual bool ready() const { return true; }              // da co khung hinh de hien (video: false sau restart() cho den khung moi)
};

} // namespace lc
