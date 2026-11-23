#include "OutputWindow.h"
#include "media/StaticSources.h"
#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>
#include <QStringList>

namespace lc {

static QStringList screenNames() {
    QStringList n;
    for (QScreen* s : QGuiApplication::screens()) n << s->name();
    return n;
}

// Ghi nho TEN man hinh luc khoi tao de lan sau nhan dung man du chi so co doi.
static OutputConfig withScreenName(OutputConfig c) {
    if (c.fullscreen && c.screenName.isEmpty()) {
        const int i = resolveScreenIndex(c, screenNames());
        if (i >= 0) c.screenName = QGuiApplication::screens()[i]->name();
    }
    return c;
}

OutputWindow::OutputWindow(const OutputConfig& cfg) : cfg_(withScreenName(cfg)), ident_(cfg_) {
    setSurfaceType(QSurface::OpenGLSurface);
    setFormat(QSurfaceFormat::defaultFormat());
    setTitle("LumaCast (LED Display Controller) - " + cfg_.name);
    if (ident_.fullscreen) {
        setFlag(Qt::FramelessWindowHint);
        const int i = resolveScreenIndex(ident_, screenNames());
        if (i >= 0) {                              // man khong con -> KHONG dat len man khac
            QScreen* sc = QGuiApplication::screens()[i];
            setScreen(sc);
            setGeometry(sc->geometry());
        }
    } else {
        resize(cfg_.size);
    }
}

bool OutputWindow::canShow() const {
    return !ident_.fullscreen || resolveScreenIndex(ident_, screenNames()) >= 0;
}

bool OutputWindow::applyScreen() {
    if (!ident_.fullscreen) { show(); return true; }
    const int i = resolveScreenIndex(ident_, screenNames());
    if (i < 0) return false;
    QScreen* sc = QGuiApplication::screens()[i];
    setScreen(sc);
    setGeometry(sc->geometry());
    showFullScreen();
    return true;
}

OutputHealth OutputWindow::health() const {
    const QStringList names = screenNames();
    const int winIdx = screen() ? names.indexOf(screen()->name()) : -1;
    return evaluateOutput(ident_, names, winIdx, isExposed());
}

void OutputWindow::present(QOpenGLContext& ctx, Compositor& comp, RenderTarget* src) {
    if (!isExposed()) return;
    if (freeze && !black && !testPattern) return;      // khong ve, khong swap -> man hinh giu khung cuoi
    ctx.makeCurrent(this);
    const int W = int(width() * devicePixelRatio()), H = int(height() * devicePixelRatio());
    comp.gl().glBindFramebuffer(GL_FRAMEBUFFER, ctx.defaultFramebufferObject());
    if (black || (!src && !testPattern)) {
        comp.gl().glViewport(0, 0, W, H);
        comp.gl().glClearColor(0, 0, 0, 1);
        comp.gl().glClear(GL_COLOR_BUFFER_BIT);
    } else if (testPattern) {                       // mau thu pixel-exact, khong scale/xoay/crop/hieu chinh
        if (!testTex_.id() || testSize_ != QSize(W, H)) {
            const QImage img = makeOutputTestPattern(W, H, cfg_.name).convertToFormat(QImage::Format_RGBA8888);
            testTex_.upload(comp.gl(), img.constBits(), W, H);
            testSize_ = QSize(W, H);
        }
        OutputConfig plain;
        plain.scale = ScaleMode::Stretch;
        comp.drawToCurrent(testTex_.id(), W, H, plain, W, H);
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
