#include "Ui.h"
#include "Text.h"
#include <QApplication>
#include <QHelpEvent>
#include <QPointer>
#include <QPushButton>
#include <QToolButton>
#include <QToolTip>
#include <QWidget>

namespace lc::ui {

namespace {
constexpr const char* kWhy = "lcWhy";
constexpr const char* kLocked = "lcLocked";

class HintFilter : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (ev->type() != QEvent::ToolTip) return false;
        auto* w = qobject_cast<QWidget*>(obj);
        if (!w || w->isEnabled()) return false;                    // dang bat: tooltip thuong cua Qt
        const QString t = hintText(w);
        if (t.isEmpty()) return false;
        QToolTip::showText(static_cast<QHelpEvent*>(ev)->globalPos(), t, w);
        ev->accept();
        return true;
    }
};
} // namespace

QPushButton* iconButton(Glyph g, const char* name, const QString& tip, QWidget* parent) {
    auto* b = new QPushButton(parent);
    b->setObjectName(name);
    b->setIcon(glyphIcon(g));
    b->setIconSize(QSize(16, 16));
    b->setToolTip(tip);
    b->setProperty("iconOnly", true);
    b->setAutoDefault(false);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

QToolButton* iconToolButton(Glyph g, const char* name, const QString& tip, QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setObjectName(name);
    b->setIcon(glyphIcon(g));
    b->setIconSize(QSize(16, 16));
    b->setToolTip(tip);
    b->setProperty("iconOnly", true);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

void setWhy(QWidget* w, const QString& why) { w->setProperty(kWhy, why); }

void setLockedState(bool locked) { qApp->setProperty(kLocked, locked); }

QString hintText(const QWidget* w) {
    if (!w) return {};
    const QString tip = w->toolTip();
    if (w->isEnabled()) return tip;
    const bool locked = qApp->property(kLocked).toBool();
    const bool byContainer = w->parentWidget() && !w->parentWidget()->isEnabled();
    QString why = w->property(kWhy).toString();
    if (locked && (byContainer || why.isEmpty())) why = text::lockedWhy();
    if (why.isEmpty()) why = text::unavailableWhy();
    const QString reason = text::dimmedBecause().arg(why);
    return tip.isEmpty() ? reason : tip + "\n" + reason;
}

void installHints() {
    static QPointer<HintFilter> filter;
    if (filter) return;
    filter = new HintFilter(qApp);
    qApp->installEventFilter(filter);
}

} // namespace lc::ui
