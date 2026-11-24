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
    // Luong am thanh cua nguon (float32 stereo 48 kHz); nullptr = im lang (anh, mau, video khong co tieng). Mixer rut tu day.
    virtual AudioStream* audio() { return nullptr; }
    virtual double durationSeconds() const { return 0; }     // do dai (giay) neu biet, 0 neu khong (dung cho Stinger)
};

} // namespace lc
