#include "OutputSupervisor.h"
#include "core/I18n.h"
#include "render/OutputWindow.h"
#include <QGuiApplication>
#include <QScreen>
#include <algorithm>

namespace lc {

OutputSupervisor::OutputSupervisor(RenderEngine& engine, QObject* parent) : QObject(parent), engine_(engine) {
    last_.assign(engine_.outputCount(), OutputHealth::Ok);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &OutputSupervisor::check);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &OutputSupervisor::check);
    connect(&timer_, &QTimer::timeout, this, &OutputSupervisor::check);
    timer_.start(500);
}

QString OutputSupervisor::text(int i) const {
    auto* w = dynamic_cast<OutputWindow*>(engine_.output(i));
    return w ? healthText(health(i), w->config()) : QString();
}

int OutputSupervisor::badCount() const {
    int n = 0;
    for (auto h : last_) n += h != OutputHealth::Ok;
    return n;
}

void OutputSupervisor::check() {
    last_.resize(size_t(std::max(0, engine_.outputCount())), OutputHealth::Ok);        // Output co the them/bot luc dang chay
    for (int i = 0; i < engine_.outputCount(); ++i) {
        auto* w = dynamic_cast<OutputWindow*>(engine_.output(i));
        if (!w) continue;
        OutputHealth h = w->health();
        if (w->isFullscreenOutput()) {
            if (h == OutputHealth::NoScreen) {
                if (w->isVisible()) w->hide();                         // khong de hinh LED nhay sang man khac
            } else if (h == OutputHealth::WrongScreen || h == OutputHealth::Hidden) {
                w->applyScreen();                                      // man da quay lai / bi doi cho -> dua ve dung man
            }
            h = w->health();
        }
        if (h != last_[i]) {
            const QString name = w->config().name;
            if (h == OutputHealth::Ok) qInfo().noquote() << QString("Output '%1': hoat dong tro lai").arg(name);
            else {
                i18n::SourceScope logLanguage;
                qWarning().noquote() << QString("Output '%1': %2").arg(name, healthText(h, w->config()));
            }
            last_[i] = h;
            emit healthChanged(i, int(h));
        }
    }
}

} // namespace lc
