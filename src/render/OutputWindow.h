#pragma once
#include "render/GlHelpers.h"
#include "render/OutputHealth.h"
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

    // --- UI thread ---
    bool isFullscreenOutput() const { return ident_.fullscreen; }
    bool canShow() const;                 // man hinh da cau hinh con ton tai (hoac la cua so thuong)
    bool applyScreen();                   // dua cua so len DUNG man hinh va hien; false neu man hinh khong con
    OutputHealth health() const;
protected:
    void keyPressEvent(QKeyEvent* e) override;   // Esc thoat fullscreen cua so thu nghiem
    bool event(QEvent* e) override;              // nut X / Alt+F4 chi AN cua so (khong huy be mat render dang dung)
private:
    OutputConfig cfg_;             // render thread co the sua qua mutableConfig()
    const OutputConfig ident_;     // dinh danh man hinh, bat bien -> UI thread doc an toan
    GlTexture testTex_;
    QSize testSize_;
};

} // namespace lc
