// Phan "bo cuc" cua MainWindow: ba bo cuc dung san (Ctrl+F1/F2/F3), bo cuc cua nguoi dung, F9 an cot phai, khoa keo khung.
// Chi sap lai khung: KHONG dung toi Program / Preview / Output.
#include "MainWindow.h"
#include "DockChrome.h"
#include "OverlayPanel.h"
#include "Text.h"
#include "core/AppSettings.h"
#include <QAction>
#include <QActionGroup>
#include <QDockWidget>
#include <QMenu>
#include <QScreen>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <algorithm>

namespace lc {

namespace {
// Tang so nay MOI KHI them/bot dock hoac doi bo cuc dung san: bo cuc da luu cua ban khac se bi bo qua.
constexpr int kLayoutVersion = 4;
constexpr int kBigScreenWidth = 1800;            // diem logic: tu muc nay lan mo dau dung "Man lon"
const char* const kBuiltin[] = {"prepare", "live", "big"};
}

QString MainWindow::layoutName(const QString& id) {
    return id == "live" ? text::layoutLive() : id == "big" ? text::layoutBig() : id == "mine" ? text::layoutMine() : text::layoutPrepare();
}

// Xep lai tu dau (khong dung restoreState de bo cuc dung san khong bao gio bi bo cuc da luu ghi de).
void MainWindow::arrangeDocks(const QString& id) {
    auto dock = [this](const char* n) { return findChild<QDockWidget*>(n); };
    QDockWidget *out = dock("dockOutputs"), *aud = dock("dockAudio"), *insp = dock("dockInspector"), *scenes = dock("dockScenes"),
                *tl = dock("dockTimeline"), *pl = dock("dockPlaylist"), *plug = dock("dockPlugins"), *ov = dock("dockOverlay");
    const QList<QDockWidget*> all{out, aud, insp, scenes, tl, pl, plug, ov};
    for (QDockWidget* d : all) {
        if (d->isFloating()) d->setFloating(false);
        removeDockWidget(d);
    }
    auto tabbed = [this](QDockWidget* first, QDockWidget* next) {
        addDockWidget(dockWidgetArea(first), next);
        tabifyDockWidget(first, next);
    };

    addDockWidget(Qt::BottomDockWidgetArea, scenes);
    tabbed(scenes, tl);
    tabbed(tl, pl);

    QList<QDockWidget*> shown{scenes, pl}, front{scenes};
    if (id == "live") {                      // hinh to, cot hep: Outputs / Am thanh / Overlay; an Inspector, Timeline
        addDockWidget(Qt::RightDockWidgetArea, out);
        splitDockWidget(out, aud, Qt::Vertical);
        splitDockWidget(aud, ov, Qt::Vertical);
        tabbed(ov, insp);
        tabbed(insp, plug);
        shown << out << aud << ov;
        front << ov;
    } else if (id == "big") {                // hai cot: cot hep Outputs / Am thanh / Overlay, cot Inspector / Plug-in
        addDockWidget(Qt::RightDockWidgetArea, out);
        splitDockWidget(out, insp, Qt::Horizontal);
        splitDockWidget(out, aud, Qt::Vertical);
        splitDockWidget(aud, ov, Qt::Vertical);
        tabbed(insp, plug);
        shown << tl << out << aud << ov << insp << plug;
        front << insp;
    } else {                                 // "prepare": mot cot: Inspector (co tab Effect) va Overlay
        addDockWidget(Qt::RightDockWidgetArea, insp);
        tabbed(insp, ov);
        tabbed(ov, plug);
        tabbed(plug, out);
        tabbed(out, aud);
        shown << tl << insp << ov;
        front << insp;
    }
    for (QDockWidget* d : all) d->setVisible(shown.contains(d));
    for (QDockWidget* d : front) d->raise();
}

void MainWindow::sizeDocks(const QString& id) {
    auto dock = [this](const char* n) { return findChild<QDockWidget*>(n); };
    QDockWidget *out = dock("dockOutputs"), *aud = dock("dockAudio"), *insp = dock("dockInspector"), *scenes = dock("dockScenes"),
                *ov = dock("dockOverlay");
    if (id == "live") {
        resizeDocks({out}, {220}, Qt::Horizontal);
        resizeDocks({out, aud, ov}, {225, 180, 330}, Qt::Vertical);
        resizeDocks({scenes}, {250}, Qt::Vertical);
    } else if (id == "big") {
        resizeDocks({out, insp}, {220, 270}, Qt::Horizontal);
        resizeDocks({out, aud, ov}, {225, 180, 330}, Qt::Vertical);
        resizeDocks({scenes}, {330}, Qt::Vertical);
    } else {
        resizeDocks({insp}, {300}, Qt::Horizontal);
        resizeDocks({scenes}, {330}, Qt::Vertical);          // du cao cho 6 lan keyframe cua Timeline
    }
}

bool MainWindow::applyLayout(const QString& id) {
    rightHidden_.clear();
    if (id == "mine") {
        const QByteArray state = appSettings().value("layout/mine").toByteArray();
        if (state.isEmpty()) { showMessage(text::layoutNoMineWhy()); return false; }
        if (!restoreState(state, kLayoutVersion)) { showMessage(text::layoutStale()); return false; }
    } else {
        const bool known = std::any_of(std::begin(kBuiltin), std::end(kBuiltin), [&](const char* b) { return id == b; });
        if (!known) return false;
        arrangeDocks(id);
        sizeDocks(id);
        overlayPanel_->preferList(id != "prepare");
    }
    layoutId_ = id;
    layoutShape_.clear();
    if (chrome_) chrome_->schedule();
    // kich thuoc chi ap duoc sau khi Qt dung xong bo cuc moi; hinh dang chuan lay sau do
    QTimer::singleShot(0, this, [this, id] {
        if (layoutId_ != id) return;
        if (id != "mine") {
            sizeDocks(id);
            overlayPanel_->preferList(id != "prepare");
        }
        layoutShape_ = arrangement();
        updateLayoutLabel();
    });
    updateLayoutActions();
    updateLayoutLabel();
    showMessage(text::layoutApplied().arg(layoutName(id)));
    return true;
}

void MainWindow::initLayout() {
    QSettings st = appSettings();
    // bo cuc luu boi ban co bo dock khac (phien ban khac) bi bo qua: ap lai se lam vo cac tab va dock de len nhau
    if (st.contains("state") && restoreState(st.value("state").toByteArray(), kLayoutVersion)) {
        layoutId_ = st.value("layout/name", "prepare").toString();
        overlayPanel_->preferList(layoutId_ != "prepare");
        rightHidden_ = st.value("layout/rightHidden").toStringList();
        QTimer::singleShot(0, this, [this] { layoutShape_ = arrangement(); updateLayoutLabel(); });
        updateLayoutActions();
        updateLayoutLabel();
        return;
    }
    const QScreen* sc = screen();                                   // lan mo dau: chon theo kich thuoc man
    applyLayout(sc && sc->availableGeometry().width() >= kBigScreenWidth ? "big" : "prepare");
    statusBar()->clearMessage();
}

void MainWindow::saveLayout(QSettings& st) const {
    st.setValue("state", saveState(kLayoutVersion));
    st.setValue("layout/name", layoutId_);
    st.setValue("layout/rightHidden", rightHidden_);
}

bool MainWindow::saveMyLayout() {
    if (locked_) { showMessage(text::lockedWhy()); return false; }
    QSettings st = appSettings();
    st.setValue("layout/mine", saveState(kLayoutVersion));
    layoutId_ = "mine";
    layoutShape_ = arrangement();
    updateLayoutActions();
    updateLayoutLabel();
    showMessage(text::layoutSaved());
    return true;
}

void MainWindow::toggleRightColumn() {
    QList<QDockWidget*> open;
    for (QDockWidget* d : findChildren<QDockWidget*>())
        if (!d->isFloating() && dockWidgetArea(d) == Qt::RightDockWidgetArea && !d->isHidden()) open << d;
    if (!open.isEmpty()) {
        // tab dang xem de o cuoi: hien lai thi raise() sau cung
        std::stable_sort(open.begin(), open.end(), [](QDockWidget* a, QDockWidget* b) {
            return a->visibleRegion().isEmpty() && !b->visibleRegion().isEmpty();
        });
        rightHidden_.clear();
        rightWidths_.clear();
        for (QDockWidget* d : open) {
            rightHidden_ << d->objectName();
            rightWidths_ << d->width();
        }
        for (QDockWidget* d : open) d->hide();
    } else if (!rightHidden_.isEmpty()) {
        QList<QDockWidget*> docks;
        QList<int> widths;
        for (int i = 0; i < rightHidden_.size(); ++i)
            if (auto* d = findChild<QDockWidget*>(rightHidden_[i])) {
                d->show();
                docks << d;
                widths << (i < rightWidths_.size() ? rightWidths_[i] : 0);
            }
        for (QDockWidget* d : docks) d->raise();
        rightHidden_.clear();
        QTimer::singleShot(0, this, [this, docks, widths] {
            for (int i = 0; i < docks.size(); ++i)
                if (widths[i] > 0 && !docks[i]->visibleRegion().isEmpty()) resizeDocks({docks[i]}, {widths[i]}, Qt::Horizontal);
        });
    } else {
        applyLayout(layoutId_);                 // khong co gi de hien lai (vd. dong tung khung bang tay): xep lai bo cuc
    }
    updateLayoutLabel();
}

// Khoa van hanh: khong keo / tach / dong khung. Doi bo cuc bang phim / menu van duoc (khong dung toi hinh).
void MainWindow::lockDocks(bool on) {
    for (QDockWidget* d : findChildren<QDockWidget*>())
        d->setFeatures(on ? QDockWidget::NoDockWidgetFeatures
                          : QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    if (chrome_) chrome_->setLocked(on);                  // keo tab / vach cung bi khoa
}

QString MainWindow::arrangement() const {
    QStringList parts;
    for (QDockWidget* d : findChildren<QDockWidget*>()) {
        QStringList mates;
        for (QDockWidget* m : tabifiedDockWidgets(d))
            if (!m->isHidden()) mates << m->objectName();
        mates.sort();
        parts << QString("%1:%2:%3:%4:%5").arg(d->objectName()).arg(int(dockWidgetArea(d))).arg(d->isHidden()).arg(d->isFloating())
                     .arg(d->isHidden() ? QString() : mates.join(","));
    }
    parts.sort();
    return parts.join(";");
}

void MainWindow::updateLayoutLabel() {
    if (!layoutBtn_) return;
    QString name = layoutName(layoutId_);
    if (!layoutShape_.isEmpty() && rightHidden_.isEmpty() && arrangement() != layoutShape_) name = text::layoutModified().arg(name);
    const QString label = text::layoutButton().arg(name);
    if (layoutBtn_->text() != label) layoutBtn_->setText(label);
}

void MainWindow::buildLayoutMenu(QMenu* view) {
    auto* menu = view->addMenu(text::layoutMenu());
    menu->setObjectName("layoutMenu");
    menu->setToolTipsVisible(true);
    auto* group = new QActionGroup(this);
    const char* ids[4] = {"prepare", "live", "big", "mine"};
    for (int i = 0; i < 4; ++i) {
        const QString id = ids[i];
        if (i == 3) menu->addSeparator();
        layoutAct_[i] = menu->addAction(layoutName(id));
        layoutAct_[i]->setObjectName("layout_" + id);
        layoutAct_[i]->setCheckable(true);
        group->addAction(layoutAct_[i]);
        if (i < 3) layoutAct_[i]->setShortcut(QKeySequence(Qt::CTRL | (Qt::Key_F1 + i)));
        connect(layoutAct_[i], &QAction::triggered, this, [this, id] {
            applyLayout(id);
            updateLayoutActions();               // ap dung khong duoc: dau chon ve dung bo cuc dang dung
        });
    }
    menu->addSeparator();
    saveLayoutAct_ = menu->addAction(text::layoutSaveMine(), this, [this] { saveMyLayout(); });
    saveLayoutAct_->setObjectName("layoutSaveMine");
    menu->addAction(text::layoutReset(), this, [this] { applyLayout(layoutId_); })->setObjectName("layoutReset");
    QAction* right = view->addAction(text::layoutRightColumn(), QKeySequence(Qt::Key_F9), this, [this] { toggleRightColumn(); });
    right->setObjectName("toggleRightColumn");
    layoutBtn_->setMenu(menu);
    updateLayoutActions();
    updateLayoutLabel();
}

void MainWindow::updateLayoutActions() {
    if (!saveLayoutAct_) return;
    const char* ids[4] = {"prepare", "live", "big", "mine"};
    const bool haveMine = appSettings().contains("layout/mine");
    layoutAct_[3]->setEnabled(haveMine);
    layoutAct_[3]->setToolTip(haveMine ? QString() : text::dimmedBecause().arg(text::layoutNoMineWhy()));
    for (int i = 0; i < 4; ++i) layoutAct_[i]->setChecked(layoutId_ == ids[i]);
    saveLayoutAct_->setEnabled(!locked_);
    saveLayoutAct_->setToolTip(locked_ ? text::dimmedBecause().arg(text::lockedWhy()) : QString());
}

} // namespace lc
