#include "DockChrome.h"
#include "Text.h"
#include "Theme.h"
#include <QContextMenuEvent>
#include <QCursor>
#include <QDockWidget>
#include <QEvent>
#include <QLayout>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QTabBar>
#include <QTimer>

namespace lc {

namespace {
constexpr int kTabPad = 12;
bool inGroupWindow(const QWidget* w) { return w && w->inherits("QDockWidgetGroupWindow"); }
} // namespace

// ---------------------------------------------------------------- DockTitle

DockTitle::DockTitle(QDockWidget* dock) : QWidget(dock), dock_(dock) {
    setObjectName("dockTitle");
    connect(dock, &QWidget::windowTitleChanged, this, [this] { updateGeometry(); update(); });
    syncHint();
}

void DockTitle::setTabbed(bool on) {
    if (on == tabbed_) return;
    tabbed_ = on;
    syncHint();
    updateGeometry();
    if (dock_->layout()) dock_->layout()->invalidate();      // Qt doc lai chieu cao thanh ten
    update();
}

void DockTitle::setLocked(bool on) {
    if (on == locked_) return;
    locked_ = on;
    syncHint();
}

void DockTitle::syncHint() {
    if (tabbed_ && !locked_) {
        setCursor(Qt::OpenHandCursor);
        setToolTip(text::dockDragGroup());
    } else {
        unsetCursor();
        setToolTip(QString());
    }
}

namespace {
QFont boldOf(QFont f) { f.setBold(true); return f; }
// Be rong the: du cho ten (them chut du de net chu khong bi tinh la tran).
int headerTabWidth(const QWidget& w, const QString& title) {
    return std::min(w.width(), QFontMetrics(boldOf(w.font())).horizontalAdvance(title) + 2 * kTabPad + 6);
}
}

QString DockTitle::shownText() const {
    const int room = headerTabWidth(*this, dock_->windowTitle()) - 2 * kTabPad;
    return QFontMetrics(boldOf(font())).elidedText(dock_->windowTitle(), Qt::ElideRight, std::max(0, room));
}

QSize DockTitle::sizeHint() const { return {40, tabbed_ ? kStripHeight : kHeaderHeight}; }
QSize DockTitle::minimumSizeHint() const { return sizeHint(); }

void DockTitle::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QColor body(colors::kPanel);
    if (tabbed_) {
        p.fillRect(rect(), body);                             // lien mau voi tab dang chon va noi dung
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x4d, 0x55, 0x70));
        const int cx = width() / 2, cy = height() / 2;
        for (int dx = -8; dx <= 8; dx += 4) p.drawRect(cx + dx - 1, cy - 1, 2, 2);    // cham nho: cho nam de keo ca nhom
        return;
    }
    p.fillRect(rect(), QColor(colors::kTabStrip));
    p.setFont(boldOf(font()));
    const QRect tab(0, 3, headerTabWidth(*this, dock_->windowTitle()), height() - 3);
    p.fillRect(tab, body);
    p.setPen(Qt::white);
    p.drawText(tab.adjusted(kTabPad, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, shownText());
}

void DockTitle::contextMenuEvent(QContextMenuEvent* e) {
    auto* chrome = dock_->property("lcChrome").value<QObject*>();
    if (!chrome) return;
    std::unique_ptr<QMenu> menu(static_cast<DockChrome*>(chrome)->menuFor(dock_, this));
    menu->exec(e->globalPos());
    e->accept();
}

// ---------------------------------------------------------------- DockChrome

DockChrome::DockChrome(QMainWindow* window) : QObject(window), window_(window) {
    window_->setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    window_->installEventFilter(this);
}

void DockChrome::adopt() {
    for (QDockWidget* d : window_->findChildren<QDockWidget*>()) {
        if (d->property("lcChrome").isValid()) continue;
        d->setProperty("lcChrome", QVariant::fromValue(static_cast<QObject*>(this)));
        d->setTitleBarWidget(new DockTitle(d));
        d->installEventFilter(this);
        connect(d, &QDockWidget::visibilityChanged, this, [this] { schedule(); });
        connect(d, &QDockWidget::topLevelChanged, this, [this] { schedule(); });
        connect(d, &QDockWidget::dockLocationChanged, this, [this] { schedule(); });
    }
    schedule();
}

bool DockChrome::eventFilter(QObject* obj, QEvent* ev) {
    switch (ev->type()) {
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::ParentChange:
    case QEvent::ChildAdded:               // Qt tao hang tab moi khi gop khung
        schedule();
        break;
    default: break;
    }
    return QObject::eventFilter(obj, ev);
}

void DockChrome::schedule() {
    if (pending_) return;
    pending_ = true;
    QTimer::singleShot(0, this, [this] { pending_ = false; refresh(); });
}

void DockChrome::setLocked(bool on) {
    locked_ = on;
    refresh();
}

bool DockChrome::isDockTabBar(const QTabBar* bar) {
    const QWidget* p = bar->parentWidget();
    return qobject_cast<const QMainWindow*>(p) || inGroupWindow(p);
}

bool DockChrome::tabbed(const QDockWidget* dock) const {
    if (inGroupWindow(dock->parentWidget())) {              // nhom da tach roi: dem cac khung trong cung cua so nhom
        int n = 0;
        for (const auto* d : dock->parentWidget()->findChildren<QDockWidget*>(Qt::FindDirectChildrenOnly)) n += !d->isHidden();
        return n > 1;
    }
    for (const QDockWidget* m : window_->tabifiedDockWidgets(const_cast<QDockWidget*>(dock)))
        if (!m->isHidden()) return true;
    return false;
}

void DockChrome::style(QTabBar* bar) {
    if (!bar->property("lcStyled").toBool()) {
        bar->setProperty("lcStyled", true);
        QFont f = bar->font();
        f.setBold(true);                                    // do be rong tab theo chu dam: ten khong bi cat
        bar->setFont(f);
        bar->setElideMode(Qt::ElideNone);
        bar->setExpanding(false);
        bar->setAttribute(Qt::WA_StyledBackground);
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(bar, &QWidget::customContextMenuRequested, this, [this, bar](const QPoint& pos) {
            const int i = bar->tabAt(pos);
            QDockWidget* d = i >= 0 ? dockNamed(bar->tabText(i)) : nullptr;
            if (!d) return;
            std::unique_ptr<QMenu> menu(menuFor(d, bar));
            menu->exec(bar->mapToGlobal(pos));
        });
    }
    if (bar->drawBase()) bar->setDrawBase(false);                      // Qt bat lai khi dung hang tab: bo duong ke trang
    if (bar->isMovable() == locked_) bar->setMovable(!locked_);        // khoa: keo tab khong tach / doi cho duoc
}

void DockChrome::refresh() {
    for (QDockWidget* d : window_->findChildren<QDockWidget*>()) {
        auto* title = dynamic_cast<DockTitle*>(d->titleBarWidget());
        if (!title) continue;
        title->setTabbed(tabbed(d));
        title->setLocked(locked_);
    }
    for (QTabBar* bar : window_->findChildren<QTabBar*>())
        if (isDockTabBar(bar)) style(bar);
}

QDockWidget* DockChrome::dockNamed(const QString& title) const {
    for (QDockWidget* d : window_->findChildren<QDockWidget*>())
        if (d->windowTitle() == title) return d;
    return nullptr;
}

QMenu* DockChrome::menuFor(QDockWidget* dock, QWidget* parent) {
    auto* menu = new QMenu(parent);
    menu->setObjectName("dockMenu");
    menu->setToolTipsVisible(true);
    const bool out = dock->isFloating() || inGroupWindow(dock->parentWidget());
    QAction* move = menu->addAction(out ? text::dockAttach() : text::dockDetach());
    move->setObjectName(out ? "dockAttach" : "dockDetach");
    connect(move, &QAction::triggered, dock, [this, dock, out] {
        if (out) {
            if (!dock->isFloating()) dock->setFloating(true);      // trong nhom noi: tach ra truoc roi moi gan lai duoc
            dock->setFloating(false);
        } else {
            dock->setFloating(true);
            dock->move(QCursor::pos() - QPoint(20, 10));
        }
        dock->show();
        dock->raise();
        schedule();
    });
    QAction* close = menu->addAction(text::dockClose());
    close->setObjectName("dockClose");
    connect(close, &QAction::triggered, dock, [this, dock] {
        dock->close();
        schedule();
    });
    if (locked_) {
        for (QAction* a : {move, close}) {
            a->setEnabled(false);
            a->setToolTip(text::dimmedBecause().arg(text::lockedWhy()));
        }
    }
    return menu;
}

} // namespace lc
