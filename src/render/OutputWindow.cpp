#include "OutputWindow.h"
#include "core/I18n.h"
#include "core/ScreenSetup.h"
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

bool OutputWindow::matches(const OutputConfig& c) const {
    if (c.kind != OutputKind::Screen || c.fullscreen != ident_.fullscreen) return false;
    if (!c.fullscreen) return true;
    const QStringList names = screenNames();
    const int want = resolveScreenIndex(c, names), have = resolveScreenIndex(ident_, names);
    if (want >= 0 || have >= 0) return want == have;
    return c.screenName == ident_.screenName && c.screenIndex == ident_.screenIndex;       // ca hai man deu khong con
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
        // co vung LED: mau thu ve dung co vung do, vien do sat mep vung -> thay ngay bo xu ly dang cat hay co
        const QRect area = cfg_.area.isEmpty() ? QRect() : outputPictureRect(cfg_, QSize(W, H));
        const QSize size = area.isEmpty() ? QSize(W, H) : area.size();
        QString label = cfg_.name;
        if (!area.isEmpty())
            label += QString(LC_TR("\nvùng LED tại (%1, %2)\nkhung tín hiệu %3×%4")).arg(area.x()).arg(area.y()).arg(W).arg(H);
        const QString key = QString("%1x%2 %3").arg(size.width()).arg(size.height()).arg(label);
        if (!testTex_.id() || testKey_ != key) {
            // GL: hang dau cua texture la hang DUOI man hinh -> lat doc, khong thi mau thu (va toa do 4 goc) lon nguoc
            const QImage img = makeOutputTestPattern(size.width(), size.height(), label).convertToFormat(QImage::Format_RGBA8888).mirrored(false, true);
            testTex_.upload(comp.gl(), img.constBits(), size.width(), size.height());
            testKey_ = key;
        }
        OutputConfig plain;
        plain.scale = ScaleMode::Stretch;
        comp.drawToCurrent(testTex_.id(), size.width(), size.height(), plain, W, H, area);
    } else {
        comp.drawToCurrent(src->tex, src->w, src->h, cfg_, W, H, cfg_.area.isEmpty() ? QRect() : outputPictureRect(cfg_, QSize(W, H)));
    }
    ctx.swapBuffers(this);
    ++presented;
}

bool OutputWindow::event(QEvent* e) {
    if (e->type() == QEvent::Close) {
        e->ignore();
        hide();
        return true;
    }
    return QWindow::event(e);
}

void OutputWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape && cfg_.fullscreen) showNormal();
    else QWindow::keyPressEvent(e);
}

} // namespace lc
