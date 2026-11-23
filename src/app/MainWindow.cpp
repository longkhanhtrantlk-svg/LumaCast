#include "MainWindow.h"
#include "InspectorPanel.h"
#include "MonitorWidget.h"
#include "render/OutputWindow.h"
#include "Theme.h"
#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace lc {

static const QStringList kImageExt = {"png", "jpg", "jpeg", "bmp", "gif", "webp"};
static const char* kMediaFilter = "Media (*.mp4 *.mov *.mkv *.avi *.webm *.png *.jpg *.jpeg *.bmp *.gif *.webp);;Tất cả (*.*)";

MainWindow::MainWindow(Project& project, RenderEngine& engine) : project_(project), engine_(engine) {
    for (const auto& c : project_.channels) {
        std::vector<QString> names;
        for (const auto& s : c->scenes) names.push_back(s->name);
        sceneNames_.push_back(std::move(names));
    }
    buildUi();
    buildDocks();
    buildMenus();
    defaultState_ = saveState();
    QSettings st("LumaCast", "LumaCast");
    if (st.contains("geometry")) restoreGeometry(st.value("geometry").toByteArray());
    else resize(1500, 900);
    if (st.contains("state")) restoreState(st.value("state").toByteArray());
    updateTitle();

    connect(&fastTimer_, &QTimer::timeout, this, [this] { refresh(); });
    fastTimer_.start(66);
    connect(&slowTimer_, &QTimer::timeout, this, [this] { refreshSlow(); });
    slowTimer_.start(500);
    connect(&autosaveTimer_, &QTimer::timeout, this, [this] { autosaveNow(); });
    autosaveTimer_.start(60 * 1000);
    refresh();
    refreshSlow();
}

int MainWindow::channelIdx() const { return std::max(0, chanCombo_->currentIndex()); }

QString MainWindow::sceneName(int ch, int idx) const {
    if (ch < 0 || ch >= int(sceneNames_.size()) || idx < 0 || idx >= int(sceneNames_[ch].size())) return "-";
    return sceneNames_[ch][idx];
}

void MainWindow::showMessage(const QString& msg) { statusBar()->showMessage(msg, 6000); }

// ---------------- dung giao dien ----------------

void MainWindow::buildUi() {
    auto* central = new QWidget;
    auto* v = new QVBoxLayout(central);

    auto* top = new QHBoxLayout;
    chanCombo_ = new QComboBox;
    for (size_t i = 0; i < project_.channels.size(); ++i)
        chanCombo_->addItem(QString("Kênh %1: %2").arg(i + 1).arg(project_.channels[i]->name));
    top->addWidget(chanCombo_);
    top->addStretch();
    chanCombo_->setVisible(project_.channels.size() > 1);
    v->addLayout(top);

    auto* mon = new QHBoxLayout;
    preview_ = new MonitorWidget("PREVIEW", QColor(colors::kPreview), false);
    program_ = new MonitorWidget("PROGRAM", QColor(colors::kProgram), true);
    mon->addWidget(preview_, 1);
    mon->addWidget(program_, 1);
    v->addLayout(mon, 1);

    auto* bar = new QHBoxLayout;
    auto* cutB = new QPushButton("CUT");
    cutB->setObjectName("cut");
    auto* takeB = new QPushButton("TAKE");
    takeB->setObjectName("take");
    transCombo_ = new QComboBox;
    for (auto t : {TransitionType::Fade, TransitionType::DipToBlack, TransitionType::WipeLeft, TransitionType::WipeRight,
                   TransitionType::WipeUp, TransitionType::WipeDown})
        transCombo_->addItem(transitionName(t), int(t));
    durSpin_ = new QDoubleSpinBox;
    durSpin_->setRange(0.1, 10);
    durSpin_->setSingleStep(0.1);
    durSpin_->setDecimals(1);
    durSpin_->setValue(1.0);
    durSpin_->setSuffix(" s");
    autoBtn_ = new QPushButton("AUTO");
    autoBtn_->setObjectName("auto");
    autoBtn_->setCheckable(true);
    autoBtn_->setToolTip("AUTO bật: bấm một Scene = chuyển thẳng lên Program bằng hiệu ứng đang chọn (không qua Preview).\nTắt (mặc định): bấm Scene chỉ đưa vào Preview.");
    tbar_ = new QSlider(Qt::Horizontal);
    tbar_->setObjectName("tbar");
    tbar_->setRange(0, 1000);
    tbar_->setFixedWidth(220);
    tbar_->setToolTip("T-bar: kéo tay để chuyển Preview → Program. Kéo hết = hoàn tất, kéo về 0 = huỷ.");
    blackAll_ = new QPushButton("BLACK ALL");
    blackAll_->setObjectName("blackAll");
    blackAll_->setCheckable(true);
    blackAll_->setToolTip("Đặt đen TẤT CẢ output (bấm lại để trả hình)");
    ftb_ = new QPushButton("FTB");
    ftb_->setObjectName("ftb");
    ftb_->setCheckable(true);
    ftb_->setToolTip("Fade-To-Black: làm mờ dần Program của kênh đang chọn về đen (bấm lại để trở lại)");
    for (QWidget* w : std::initializer_list<QWidget*>{cutB, takeB, autoBtn_, transCombo_, durSpin_}) bar->addWidget(w);
    bar->addSpacing(12);
    bar->addWidget(new QLabel("T-bar"));
    bar->addWidget(tbar_);
    bar->addStretch();
    bar->addWidget(blackAll_);
    bar->addWidget(ftb_);
    v->addLayout(bar);
    setCentralWidget(central);

    connect(cutB, &QPushButton::clicked, this, [this] { cut(); });
    connect(takeB, &QPushButton::clicked, this, [this] { take(); });
    connect(blackAll_, &QPushButton::clicked, this, [this](bool on) { setBlackAll(on); });
    connect(ftb_, &QPushButton::clicked, this, [this] { toggleFtb(); });
    connect(chanCombo_, &QComboBox::currentIndexChanged, this, [this] { rebuildSceneList(); });
    connect(tbar_, &QSlider::valueChanged, this, [this](int v) {
        tbarMovedMs_ = QDateTime::currentMSecsSinceEpoch();
        auto ch = project_.channels[channelIdx()];
        const auto tt = TransitionType(transCombo_->currentData().toInt());
        const float t = float(v) / 1000.0f;
        engine_.post([ch, tt, t](double) { ch->setManual(tt, t); });
    });

    // Phim tat theo dac ta: Space = TAKE, Enter = CUT, B = BLACK ALL, 1-9 = chon Preview
    new QShortcut(Qt::Key_Space, this, [this] { take(); });
    new QShortcut(Qt::Key_Return, this, [this] { cut(); });
    new QShortcut(Qt::Key_Enter, this, [this] { cut(); });
    new QShortcut(Qt::Key_B, this, [this] { setBlackAll(!blackAll_->isChecked()); });
    for (int i = 0; i < 9; ++i)
        new QShortcut(QKeySequence(Qt::Key_1 + i), this, [this, i] {
            if (i < sceneList_->count()) { sceneList_->setCurrentRow(i); emit sceneList_->itemClicked(sceneList_->item(i)); }
        });

    statusLabel_ = new QLabel;
    statusBar()->addPermanentWidget(statusLabel_);
}

void MainWindow::buildDocks() {
    // Scenes
    auto* scenesDock = new QDockWidget("Scenes", this);
    scenesDock->setObjectName("dockScenes");
    sceneList_ = new QListWidget;
    sceneList_->setObjectName("sceneList");
    sceneList_->setTextElideMode(Qt::ElideMiddle);
    sceneList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scenesDock->setWidget(sceneList_);
    addDockWidget(Qt::LeftDockWidgetArea, scenesDock);
    connect(sceneList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        auto ch = project_.channels[channelIdx()];
        const int idx = sceneList_->row(it);
        if (autoBtn_->isChecked()) {                       // AUTO: chon scene = chuyen thang len Program
            const auto tt = TransitionType(transCombo_->currentData().toInt());
            const double d = durSpin_->value();
            engine_.post([ch, idx, tt, d](double now) { ch->setPreview(idx); ch->startTransition(tt, d, now); });
        } else {
            engine_.post([ch, idx](double) { ch->setPreview(idx); });
        }
    });
    connect(sceneList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        auto ch = project_.channels[channelIdx()];
        const int idx = sceneList_->row(it);
        engine_.post([ch, idx](double) { ch->setPreview(idx); ch->cut(); });
    });

    // Outputs
    auto* outDock = new QDockWidget("Outputs", this);
    outDock->setObjectName("dockOutputs");
    auto* outW = new QWidget;
    auto* ov = new QVBoxLayout(outW);
    for (int i = 0; i < engine_.outputCount(); ++i) {
        OutputSink* sink = engine_.output(i);
        const OutputConfig& c = sink->config();
        OutRow r;
        auto* row = new QHBoxLayout;
        r.dot = new QLabel("●");
        r.info = new QLabel(QString("<b>%1</b><br><small>%2x%3 · kênh %4</small>")
                                .arg(c.name).arg(c.size.width()).arg(c.size.height()).arg(c.channel + 1));
        r.black = new QToolButton;
        r.black->setText("Đen");
        r.black->setCheckable(true);
        r.freeze = new QToolButton;
        r.freeze->setText("Dừng");
        r.freeze->setCheckable(true);
        r.freeze->setToolTip("Dừng hình: giữ nguyên khung hình cuối trên màn hình này");
        row->addWidget(r.dot);
        row->addWidget(r.info, 1);
        row->addWidget(r.black);
        row->addWidget(r.freeze);
        ov->addLayout(row);
        connect(r.black, &QToolButton::toggled, this, [sink](bool on) { sink->black = on; });
        connect(r.freeze, &QToolButton::toggled, this, [sink](bool on) { sink->freeze = on; });
        outRows_.push_back(r);
    }
    ov->addStretch();
    outDock->setWidget(outW);
    addDockWidget(Qt::RightDockWidgetArea, outDock);

    // Inspector
    auto* inspDock = new QDockWidget("Inspector", this);
    inspDock->setObjectName("dockInspector");
    inspector_ = new InspectorPanel(project_, engine_, &undo_);
    inspDock->setWidget(inspector_);
    addDockWidget(Qt::RightDockWidgetArea, inspDock);
    resizeDocks({inspDock}, {430}, Qt::Horizontal);

    // Media Pool
    auto* mediaDock = new QDockWidget("Media Pool", this);
    mediaDock->setObjectName("dockMedia");
    auto* mw = new QWidget;
    auto* mv = new QVBoxLayout(mw);
    auto* mtop = new QHBoxLayout;
    auto* importB = new QPushButton("Nhập media...");
    mtop->addWidget(importB);
    mtop->addWidget(new QLabel("Nhấp đúp để tạo Scene từ nguồn"));
    mtop->addStretch();
    mv->addLayout(mtop);
    mediaList_ = new QListWidget;
    mediaList_->setObjectName("mediaList");
    mediaList_->setViewMode(QListView::IconMode);
    mediaList_->setIconSize(QSize(128, 72));
    mediaList_->setResizeMode(QListView::Adjust);
    mediaList_->setMovement(QListView::Static);
    mediaList_->setWordWrap(true);
    mediaList_->setTextElideMode(Qt::ElideMiddle);
    mv->addWidget(mediaList_);
    mediaDock->setWidget(mw);
    addDockWidget(Qt::BottomDockWidgetArea, mediaDock);
    connect(importB, &QPushButton::clicked, this, [this] {
        for (const QString& f : QFileDialog::getOpenFileNames(this, "Nhập media", QString(), kMediaFilter)) addMediaFile(f);
    });
    connect(mediaList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        const int k = it->data(Qt::UserRole).toInt();
        if (k < 0 || k >= int(project_.sources.size())) return;
        auto src = project_.sources[k].source;
        auto scene = std::make_shared<Scene>();
        scene->name = src->name();
        scene->size = project_.channels[0]->size;
        scene->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Fit, true});
        engine_.post([this, scene](double) {
            project_.scenes.push_back(scene);
            for (auto& c : project_.channels) c->scenes.push_back(scene);
            QMetaObject::invokeMethod(this, [this] { onProjectChanged(); }, Qt::QueuedConnection);
        });
    });

    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    rebuildSceneList();
    rebuildMediaList();
}

void MainWindow::buildMenus() {
    auto* file = menuBar()->addMenu("&File");
    auto* openA = file->addAction("Mở project...", QKeySequence::Open, this, [this] { openProject(); });
    Q_UNUSED(openA);
    recentMenu_ = file->addMenu("Gần đây");
    file->addSeparator();
    file->addAction("Lưu", QKeySequence::Save, this, [this] { saveProject(false); });
    file->addAction("Lưu thành...", QKeySequence::SaveAs, this, [this] { saveProject(true); });
    file->addSeparator();
    file->addAction("Nhập media...", this, [this] {
        for (const QString& f : QFileDialog::getOpenFileNames(this, "Nhập media", QString(), kMediaFilter)) addMediaFile(f);
    });
    file->addSeparator();
    file->addAction("Thoát", QKeySequence::Quit, this, [this] { close(); });
    rebuildRecentMenu();

    auto* edit = menuBar()->addMenu("&Edit");
    QAction* undoA = undo_.createUndoAction(this, "Hoàn tác");
    undoA->setShortcut(QKeySequence::Undo);
    QAction* redoA = undo_.createRedoAction(this, "Làm lại");
    redoA->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    edit->addAction(undoA);
    edit->addAction(redoA);

    auto* view = menuBar()->addMenu("&View");
    for (QDockWidget* d : findChildren<QDockWidget*>()) view->addAction(d->toggleViewAction());
    view->addSeparator();
    view->addAction("Đặt lại bố cục", this, [this] {
        restoreState(defaultState_);
        for (QDockWidget* d : findChildren<QDockWidget*>()) d->show();
    });

    auto* help = menuBar()->addMenu("&Help");
    help->addAction("Giới thiệu", this, [this] {
        QMessageBox::about(this, "LumaCast", "<b>LumaCast</b> — LED Display Controller<br>Phiên bản M3a");
    });
}

// ---------------- thao tac live ----------------

void MainWindow::cut() {
    auto ch = project_.channels[channelIdx()];
    engine_.post([ch](double) { ch->cut(); });
}

void MainWindow::take() {
    auto ch = project_.channels[channelIdx()];
    const auto tt = TransitionType(transCombo_->currentData().toInt());
    const double d = durSpin_->value();
    engine_.post([ch, tt, d](double now) { ch->startTransition(tt, d, now); });
}

void MainWindow::setBlackAll(bool on) {
    for (int i = 0; i < engine_.outputCount(); ++i) engine_.output(i)->black = on;
    blackAll_->setChecked(on);
}

void MainWindow::toggleFtb() {
    auto ch = project_.channels[channelIdx()];
    const double d = durSpin_->value();
    engine_.post([ch, d](double) { ch->toggleFtb(d); });
}

// ---------------- cap nhat ----------------

void MainWindow::refresh() {
    const int ci = channelIdx();
    const Channel& ch = *project_.channels[ci];
    const auto mf = engine_.monitor(ci);
    program_->setImage(mf.program);
    preview_->setImage(mf.preview);
    const int pg = ch.program, pv = ch.preview;
    program_->setSceneName(sceneName(ci, pg) + (ch.inTransition() ? "  (đang chuyển)" : ""));
    preview_->setSceneName(sceneName(ci, pv));
    program_->setOnAir(ch.ftbAmount < 1.0f);

    if (pg != lastProgram_ || pv != lastPreview_ || ci != lastChannel_) {
        lastProgram_ = pg; lastPreview_ = pv; lastChannel_ = ci;
        for (int i = 0; i < sceneList_->count(); ++i) {
            QListWidgetItem* it = sceneList_->item(i);
            const bool isPg = i == pg, isPv = i == pv;
            it->setForeground(isPg ? QColor(colors::kProgram) : isPv ? QColor(colors::kPreview) : QColor(0xe6, 0xe8, 0xee));
            it->setText(QString("%1  %2%3").arg(i + 1, 2, 10, QChar('0')).arg(sceneName(ci, i))
                            .arg(isPg ? "   ● PGM" : isPv ? "   ○ PVW" : ""));
        }
    }
    if (!ch.manualActive() && tbar_->value() != 0 && !tbar_->isSliderDown() &&
        QDateTime::currentMSecsSinceEpoch() - tbarMovedMs_ > 400) {      // T-bar da hoan tat/huy -> ve 0
        QSignalBlocker bt(tbar_);
        tbar_->setValue(0);
    }
    QSignalBlocker b1(ftb_), b2(blackAll_);
    ftb_->setChecked(ch.ftbTarget);
    bool allBlack = engine_.outputCount() > 0;
    for (int i = 0; i < engine_.outputCount(); ++i) allBlack = allBlack && engine_.output(i)->black;
    blackAll_->setChecked(allBlack);
}

void MainWindow::refreshSlow() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double dt = lastSlowMs_ ? (now - lastSlowMs_) / 1000.0 : 0;
    lastSlowMs_ = now;
    int ready = 0;
    for (int i = 0; i < engine_.outputCount(); ++i) {
        OutputSink* sink = engine_.output(i);
        OutRow& r = outRows_[i];
        auto* win = dynamic_cast<OutputWindow*>(sink);
        const bool exposed = win && win->isExposed();
        const uint64_t pr = sink->presented;
        const double fps = dt > 0 ? double(pr - r.lastPresented) / dt : 0;
        r.lastPresented = pr;
        if (exposed) ++ready;
        const char* col = !exposed ? colors::kProgram : sink->black ? "#8a90a2" : sink->freeze ? colors::kWarn : colors::kPreview;
        r.dot->setStyleSheet(QString("color:%1;font-size:16px").arg(col));
        const OutputConfig& c = sink->config();
        r.info->setText(QString("<b>%1</b><br><small>%2x%3 · kênh %4 · %5 fps%6</small>")
                            .arg(c.name).arg(c.size.width()).arg(c.size.height()).arg(c.channel + 1)
                            .arg(fps, 0, 'f', 0).arg(!exposed ? " · KHÔNG HIỂN THỊ" : sink->freeze ? " · DỪNG HÌNH" : ""));
        QSignalBlocker bb(r.black), bf(r.freeze);
        r.black->setChecked(sink->black);
        r.freeze->setChecked(sink->freeze);
    }
    statusLabel_->setText(QString("%1  |  GPU: %2  |  FPS: %3  |  Rơi khung: %4  |  Output: %5/%6")
                              .arg(engine_.error().isEmpty() ? "System Ready" : "LỖI: " + engine_.error())
                              .arg(engine_.gpuName().isEmpty() ? "-" : engine_.gpuName())
                              .arg(engine_.fps(), 0, 'f', 1).arg(engine_.droppedFrames()).arg(ready).arg(engine_.outputCount()));
    // anh thu nho cua nguon dong (video) cap nhat cham
    static int tick = 0;
    if (++tick % 4 == 0)
        for (int i = 0; i < mediaList_->count(); ++i) {
            const int k = mediaList_->item(i)->data(Qt::UserRole).toInt();
            if (k >= 0 && k < int(project_.sources.size()) && project_.sources[k].type == "video") {
                const QImage im = project_.sources[k].source->snapshot(160);
                if (!im.isNull()) mediaList_->item(i)->setIcon(QIcon(QPixmap::fromImage(im)));
            }
        }
}

void MainWindow::rebuildSceneList() {
    const int ci = channelIdx();
    sceneList_->clear();
    for (size_t i = 0; i < sceneNames_[ci].size(); ++i) sceneList_->addItem(QString());
    lastProgram_ = lastPreview_ = -2;
}

void MainWindow::rebuildMediaList() {
    mediaList_->clear();
    for (size_t k = 0; k < project_.sources.size(); ++k) {
        const SourceDef& d = project_.sources[k];
        auto* it = new QListWidgetItem(d.source ? d.source->name() : d.id);
        it->setData(Qt::UserRole, int(k));
        const QImage im = d.source ? d.source->snapshot(160) : QImage();
        if (!im.isNull()) it->setIcon(QIcon(QPixmap::fromImage(im)));
        mediaList_->addItem(it);
    }
}

void MainWindow::onProjectChanged() {
    sceneNames_.clear();
    for (const auto& c : project_.channels) {
        std::vector<QString> names;
        for (const auto& s : c->scenes) names.push_back(s->name);
        sceneNames_.push_back(std::move(names));
    }
    rebuildSceneList();
    rebuildMediaList();
    inspector_->refreshFromProject();
}

// ---------------- media ----------------

bool MainWindow::addMediaFile(const QString& path) {
    SourceDef d;
    d.path = QFileInfo(path).absoluteFilePath();
    d.type = kImageExt.contains(QFileInfo(path).suffix().toLower()) ? "image" : "video";
    QString err;
    auto src = createSource(d, &err);
    if (!src) { showMessage("Không nhập được: " + err); return false; }
    d.source = src;
    auto scene = std::make_shared<Scene>();
    scene->name = src->name();
    scene->size = project_.channels[0]->size;
    scene->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Fit, true});
    engine_.post([this, d, scene](double) mutable {
        int n = int(project_.sources.size()) + 1;
        auto taken = [&](const QString& id) { for (const auto& s : project_.sources) if (s.id == id) return true; return false; };
        while (taken(QString("src%1").arg(n))) ++n;
        d.id = QString("src%1").arg(n);
        project_.sources.push_back(d);
        project_.scenes.push_back(scene);
        for (auto& c : project_.channels) c->scenes.push_back(scene);
        QMetaObject::invokeMethod(this, [this] { onProjectChanged(); }, Qt::QueuedConnection);
    });
    return true;
}

// ---------------- project ----------------

QString MainWindow::autosavePath() const {
    if (!project_.filePath.isEmpty()) return project_.filePath + ".autosave";
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/untitled.autosave.json";
}

void MainWindow::autosaveNow() {
    const QString path = autosavePath();
    engine_.post([this, path](double) {
        engine_.syncOutputsToProject();
        QString err;
        lc::saveProject(project_, path, &err);
    });
}

void MainWindow::saveProject(bool saveAs) {
    QString path = project_.filePath;
    if (path.isEmpty() || saveAs) {
        path = QFileDialog::getSaveFileName(this, "Lưu project", path.isEmpty() ? "project.lumacast.json" : path,
                                            "LumaCast project (*.json)");
        if (path.isEmpty()) return;
    }
    project_.filePath = path;
    engine_.post([this, path](double) {
        engine_.syncOutputsToProject();             // lay cau hinh Output hien tai, tren render thread
        QString err;
        const bool ok = lc::saveProject(project_, path, &err);
        QMetaObject::invokeMethod(this, [this, ok, err, path] {
            showMessage(ok ? "Đã lưu: " + path : "Lỗi lưu: " + err);
            if (ok) { addRecent(path); updateTitle(); QFile::remove(path + ".autosave"); }
        }, Qt::QueuedConnection);
    });
}

void MainWindow::updateTitle() {
    setWindowTitle(QString("LumaCast — %1").arg(project_.filePath.isEmpty() ? "Chưa đặt tên" : QFileInfo(project_.filePath).fileName()));
}

void MainWindow::addRecent(const QString& path) {
    QSettings st("LumaCast", "LumaCast");
    QStringList l = st.value("recent").toStringList();
    l.removeAll(path);
    l.prepend(path);
    while (l.size() > 8) l.removeLast();
    st.setValue("recent", l);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu() {
    recentMenu_->clear();
    const QStringList l = QSettings("LumaCast", "LumaCast").value("recent").toStringList();
    for (const QString& p : l) recentMenu_->addAction(p, this, [this, p] { launchProject(p); });
    recentMenu_->setEnabled(!l.isEmpty());
}

void MainWindow::openProject() {
    const QString p = QFileDialog::getOpenFileName(this, "Mở project", QString(), "LumaCast project (*.json)");
    if (!p.isEmpty()) launchProject(p);
}

// Mo project khac = khoi dong lai voi project do (engine/output dung lai tu dau, don gian va chac chan).
void MainWindow::launchProject(const QString& path) {
    if (QMessageBox::question(this, "Mở project",
                              "Mở project này sẽ ĐÓNG project hiện tại và tắt các output.\nTiếp tục?",
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--project", path})) {
        showMessage("Không khởi động được LumaCast mới");
        return;
    }
    askOnClose_ = false;
    close();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (askOnClose_ &&
        QMessageBox::question(this, "Thoát LumaCast", "Thoát sẽ tắt tất cả output.\nBạn chắc chắn?",
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        e->ignore();
        return;
    }
    QSettings st("LumaCast", "LumaCast");
    st.setValue("geometry", saveGeometry());
    st.setValue("state", saveState());
    e->accept();
    QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);   // cua so output khong tu thoat
}

} // namespace lc
