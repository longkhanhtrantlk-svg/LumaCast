#pragma once
#include "render/OutputSink.h"
#include <QWindow>
#include <atomic>

namespace lc {

// Cua so fullscreen/cua so thuong cho mot man hinh LED. Chi ve tu render thread; khong tu ve.
class OutputWindow : public QWindow, public OutputSink {
public:
    explicit OutputWindow(const OutputConfig& cfg);
    const OutputConfig& config() const override { return cfg_; }
    OutputConfig& mutableConfig() override { return cfg_; }
    void present(QOpenGLContext& ctx, Compositor& comp, RenderTarget* src) override;
protected:
    void keyPressEvent(QKeyEvent* e) override;   // Esc thoat fullscreen cua so thu nghiem
private:
    OutputConfig cfg_;
};

} // namespace lc
