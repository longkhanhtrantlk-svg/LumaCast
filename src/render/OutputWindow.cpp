#include "OutputWindow.h"
#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>

namespace lc {

OutputWindow::OutputWindow(const OutputConfig& cfg) : cfg_(cfg) {
    setSurfaceType(QSurface::OpenGLSurface);
    setFormat(QSurfaceFormat::defaultFormat());
    setTitle("LumaCast (LED Display Controller) - " + cfg.name);
    const auto screens = QGuiApplication::screens();
    if (cfg.fullscreen) {
        const int i = std::clamp(cfg.screenIndex < 0 ? 0 : cfg.screenIndex, 0, int(screens.size()) - 1);
        setScreen(screens[i]);
        setFlag(Qt::FramelessWindowHint);
        setGeometry(screens[i]->geometry());
    } else {
        resize(cfg.size);
    }
}

void OutputWindow::present(QOpenGLContext& ctx, Compositor& comp, RenderTarget* src) {
    if (!isExposed()) return;
    if (freeze && !black) return;          // khong ve, khong swap -> man hinh giu khung cuoi
    ctx.makeCurrent(this);
    const int W = int(width() * devicePixelRatio()), H = int(height() * devicePixelRatio());
    comp.gl().glBindFramebuffer(GL_FRAMEBUFFER, ctx.defaultFramebufferObject());
    if (black || !src) {
        comp.gl().glViewport(0, 0, W, H);
        comp.gl().glClearColor(0, 0, 0, 1);
        comp.gl().glClear(GL_COLOR_BUFFER_BIT);
    } else {
        comp.drawToCurrent(src->tex, src->w, src->h, cfg_, W, H);
    }
    ctx.swapBuffers(this);
    ++presented;
}

void OutputWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape && cfg_.fullscreen) showNormal();
    else QWindow::keyPressEvent(e);
}

} // namespace lc
