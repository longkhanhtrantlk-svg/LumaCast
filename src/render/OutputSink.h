#pragma once
#include "core/Types.h"
#include "render/Compositor.h"
#include <atomic>

namespace lc {

// Dich den cua mot Channel: man hinh LED (cua so), sau nay la NDI out, file, DMX, projector...
// Loi (Channel/Compositor) khong biet gi ve LED; moi loai dich den tu lo cach dua hinh ra.
class OutputSink {
public:
    virtual ~OutputSink() = default;
    virtual const OutputConfig& config() const = 0;
    // Render thread, context GL da tao. src = texture Program cua kenh (co the nullptr).
    virtual void present(QOpenGLContext& ctx, Compositor& comp, RenderTarget* src) = 0;
    std::atomic<bool> black{false};      // ngat hinh tuc thi
};

} // namespace lc
