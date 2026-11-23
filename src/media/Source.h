#pragma once
#include "render/GlHelpers.h"
#include <QString>

namespace lc {

struct TexRef {
    GLuint id = 0;     // 0 = chua co khung hinh
    int w = 0, h = 0;
};

// Nguon hinh. acquire() chi duoc goi tren render thread (co context GL hien hanh).
class Source {
public:
    virtual ~Source() = default;
    virtual QString name() const = 0;
    virtual TexRef acquire(GL& gl) = 0;     // khung hinh moi nhat, dang texture GL
};

} // namespace lc
