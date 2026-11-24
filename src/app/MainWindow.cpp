#include "MainWindow.h"
#include "AudioPanel.h"
#include "InspectorPanel.h"
#include "LockButton.h"
#include "MonitorWidget.h"
#include "ParamEditor.h"
#include "SettingsDialog.h"
#include "core/AppSettings.h"
#include "core/Startup.h"
#include "TimelineWidget.h"
#include "render/OutputWindow.h"
#include "Theme.h"
#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDesktopServices>
#include <QDialog>
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
#include <QUndoCommand>
#include <QUrl>
#include <QVBoxLayout>

namespace lc {

static const QStringList kImageExt = {"png", "jpg", "jpeg", "bmp", "gif", "webp"};
static const char* kMediaFilter = "Media (*.mp4 *.mov *.mkv *.avi *.webm *.png *.jpg *.jpeg *.bmp *.gif *.webp);;Tất cả (*.*)";

namespace {
// Hoan tac/Lam lai cho nhap media va tao Scene tu Media Pool (PRJ-07). Khong gop buoc.
constexpr int kImportCmdId = 7000001;
class ImportCmd : public QUndoCommand {
public:
    ImportCmd(MainWindow* w, std::shared_ptr<MainWindow::ImportEntry> e, const QString& text) : QUndoCommand(text), w_(w), e_(std::move(e)) {}
    int id() const override { return kImportCmdId; }
    void redo() override { w_->applyImport(e_); }
    void undo() override { w_->unapplyImport(e_, this); }
    const MainWindow::ImportEntry& entry() const { return *e_; }
private:
    MainWindow* w_;
    std::shared_ptr<MainWindow::ImportEntry> e_;
};
} // namespace

MainWindow::MainWindow(Project& project, RenderEngine& engine, QStringList pluginDirs)
    : project_(project), engine_(engine), sup_(engine), pm_(engine.registry(), std::move(pluginDirs)) {
    pm_.rescan();                                   // nap Effect/Transition dung san + plug-in truoc khi dung giao dien
    for (const auto& c : project_.channels) {
        std::vector<QString> names;
        for (const auto& s : c->scenes) names.push_back(s->name);
        sceneNames_.push_back(std::move(names));
    }
    buildUi();
    buildDocks();
    ctlApi_ = new ControlApi(project_, engine_, &sup_, this);
    ctlApi_->setLockProvider([this] { return locked_; });
    ctl_ = new ControlService(*ctlApi_, this);
    ctlLabel_ = new QLabel;
    ctlLabel_->setObjectName("ctlLabel");
    statusBar()->addPermanentWidget(ctlLabel_);
    lockLabel_ = new QLabel;
    lockLabel_->setObjectName("lockLabel");
    statusBar()->addPermanentWidget(lockLabel_);
    connect(ctl_, &ControlService::statusChanged, this, [this] { updateControlStatus(); });
    buildMenus();
    defaultState_ = saveState();
    QSettings st = appSettings();
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
    {
        QSettings cst = appSettings();
        const ControlSettings cs = ControlSettings::load(cst);
        if (cs.httpEnabled || cs.oscEnabled) {
            const QString e = ctl_->apply(cs);
            if (!e.isEmpty()) showMessage("Điều khiển ngoài: " + e.section('\n', 0, 0));
        }
    }
    updateControlStatus();
    setLocked(false);                                  // khoi dong luon mo khoa (khong luu giua cac phien)
}

void MainWindow::updateUndoActions() {
    undoAct_->setEnabled(!locked_ && undo_.canUndo());
    redoAct_->setEnabled(!locked_ && undo_.canRedo());
    undoAct_->setText(undo_.canUndo() ? "Hoàn tác " + undo_.undoText() : "Hoàn tác");
    redoAct_->setText(undo_.canRedo() ? "Làm lại " + undo_.redoText() : "Làm lại");
}

// UI-07. Khoa = vo hieu moi dieu khien SUA (Inspector, sua keyframe/thoi luong, nhap media, Plug-in, Hoan tac/Lam lai, mo project,
// cai dat, dong cua so chinh); GIU dieu khien live (CUT/TAKE/AUTO/T-bar/BLACK ALL/FTB/Den-Dung-Mau, chon Scene, Phat-Tam dung-Ve 0).
void MainWindow::setLocked(bool on) {
    const bool changed = on != locked_;
    locked_ = on;
    lockBtn_->setLocked(on);
    inspector_->setLocked(on);
    timeline_->setLocked(on);
    pluginPanel_->setEnabled(!on);
    audioPanel_->setEditLocked(on);
    importBtn_->setEnabled(!on);
    importAct_->setEnabled(!on);
    openAct_->setEnabled(!on);
    recentMenu_->setEnabled(!on && !appSettings().value("recent").toStringList().isEmpty());
    settingsAct_->setEnabled(!on);
    updateUndoActions();
    lockLabel_->setText(on ? "🔒 KHOÁ VẬN HÀNH" : "🔓 Mở khoá");
    lockLabel_->setStyleSheet(on ? "color:white;background:#8a1c1c;font-weight:bold;padding:0 8px" : "color:#8a90a2");
    updateTitle();
    if (changed) {
        qInfo().noquote() << (on ? "Khoá vận hành: BẬT" : "Khoá vận hành: TẮT");
        showMessage(on ? "Đã khoá vận hành: chỉ còn điều khiển live" : "Đã mở khoá vận hành");
    }
}

void MainWindow::updateControlStatus() {
    QStringList parts;
    if (ctl_->httpRunning()) parts << QString("HTTP :%1%2").arg(ctl_->httpPort()).arg(ctl_->settings().httpLan ? " (LAN)" : "");
    if (ctl_->oscRunning()) parts << QString("OSC :%1%2").arg(ctl_->oscPort()).arg(ctl_->settings().oscLan ? " (LAN)" : "");
    ctlLabel_->setText(parts.isEmpty() ? QString() : "Điều khiển ngoài: " + parts.join(" · "));
}

void MainWindow::showStartupNotice(const QString& msg) { statusBar()->showMessage(msg, 30000); }

void MainWindow::openSettings() {
    SettingsDialog dlg(*ctl_, this);
    dlg.exec();
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
    lockBtn_ = new LockButton;
    top->addWidget(lockBtn_);
    connect(lockBtn_, &LockButton::lockRequested, this, [this] { setLocked(true); });
    connect(lockBtn_, &LockButton::unlockConfirmed, this, [this] { setLocked(false); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), this, [this] { if (!locked_) setLocked(true); });
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
    transCombo_->setObjectName("transCombo");
    easingCombo_ = new QComboBox;
    easingCombo_->setObjectName("easingCombo");
    for (auto e : {Easing::EaseInOut, Easing::Linear, Easing::EaseIn, Easing::EaseOut}) easingCombo_->addItem(easingName(e), int(e));
    easingCombo_->setToolTip("Đường cong tốc độ của hiệu ứng (T-bar kéo tay luôn tuyến tính)");
    paramsBtn_ = new QPushButton("Tham số…");
    paramsBtn_->setObjectName("transParams");
    stingerCombo_ = new QComboBox;
    stingerCombo_->setObjectName("stingerCombo");
    stingerCombo_->setToolTip("Video có kênh alpha chạy đè lên lúc chuyển cảnh");
    cutSpin_ = new QDoubleSpinBox;
    cutSpin_->setObjectName("cutPoint");
    cutSpin_->setRange(0.05, 0.95);
    cutSpin_->setSingleStep(0.05);
    cutSpin_->setValue(0.5);
    cutSpin_->setPrefix("cắt tại ");
    cutSpin_->setToolTip("Thời điểm (tỉ lệ 0–1 của video stinger) đổi cảnh phía dưới");
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
    for (QWidget* w : std::initializer_list<QWidget*>{cutB, takeB, autoBtn_, transCombo_, paramsBtn_, stingerCombo_, cutSpin_, easingCombo_, durSpin_}) bar->addWidget(w);
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
    connect(transCombo_, &QComboBox::currentIndexChanged, this, [this] { onTransitionChanged(); });
    connect(stingerCombo_, &QComboBox::currentIndexChanged, this, [this] { onTransitionChanged(); });
    connect(paramsBtn_, &QPushButton::clicked, this, [this] { editTransitionParams(); });
    rebuildTransitionCombo();
    connect(tbar_, &QSlider::valueChanged, this, [this](int v) {
        tbarMovedMs_ = QDateTime::currentMSecsSinceEpoch();
        auto ch = project_.channels[channelIdx()];
        const TransitionSpec spec = currentSpec();
        const float t = float(v) / 1000.0f;
        engine_.post([ch, spec, t](double) { ch->setManual(spec, t); });
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
            const TransitionSpec spec = currentSpec();
            engine_.post([ch, idx, spec](double now) { ch->setPreview(idx); ch->startTransition(spec, now); });
        } else {
            engine_.post([ch, idx](double) { ch->setPreview(idx); });
        }
    });
    connect(sceneList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        auto ch = project_.channels[channelIdx()];
        const int idx = sceneList_->row(it);
        engine_.post([ch, idx](double now) { ch->setPreview(idx); ch->cut(now); });
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
        r.pattern = new QToolButton;
        r.pattern->setText("Mẫu");
        r.pattern->setCheckable(true);
        r.pattern->setToolTip("Mẫu thử: hiện lưới, thanh màu, thước điểm ảnh và toạ độ 4 góc đúng độ phân giải thật của màn hình này");
        row->addWidget(r.dot);
        row->addWidget(r.info, 1);
        row->addWidget(r.black);
        row->addWidget(r.freeze);
        row->addWidget(r.pattern);
        ov->addLayout(row);
        connect(r.black, &QToolButton::toggled, this, [sink](bool on) { sink->black = on; });
        connect(r.freeze, &QToolButton::toggled, this, [sink](bool on) { sink->freeze = on; });
        connect(r.pattern, &QToolButton::toggled, this, [sink](bool on) { sink->testPattern = on; });
        outRows_.push_back(r);
    }
    auto* allPattern = new QPushButton("Mẫu thử tất cả Output");
    allPattern->setObjectName("allPattern");
    allPattern->setCheckable(true);
    ov->addWidget(allPattern);
    connect(allPattern, &QPushButton::clicked, this, [this](bool on) {
        for (int i = 0; i < engine_.outputCount(); ++i) engine_.output(i)->testPattern = on;
    });
    ov->addStretch();
    outDock->setWidget(outW);
    addDockWidget(Qt::RightDockWidgetArea, outDock);

    // Am thanh (M5a)
    auto* audDock = new QDockWidget("Âm thanh", this);
    audDock->setObjectName("dockAudio");
    audioPanel_ = new AudioPanel(project_, engine_);
    audDock->setWidget(audioPanel_);
    addDockWidget(Qt::RightDockWidgetArea, audDock);

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
    importBtn_ = importB;
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
        if (locked_) { showMessage("Đang khoá vận hành: không tạo Scene mới được"); return; }
        const int k = it->data(Qt::UserRole).toInt();
        if (k < 0 || k >= int(project_.sources.size())) return;
        auto src = project_.sources[k].source;
        auto e = std::make_shared<ImportEntry>();
        e->addsSource = false;
        e->def = project_.sources[k];
        e->scene = std::make_shared<Scene>();
        e->scene->name = src->name();
        e->scene->size = project_.channels[0]->size;
        e->scene->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Fit, true});
        pushImport(e, "Tạo Scene: " + src->name());
    });


    // Timeline (tab canh Media Pool)
    auto* tlDock = new QDockWidget("Timeline", this);
    tlDock->setObjectName("dockTimeline");
    timeline_ = new TimelineWidget(*inspector_, engine_, project_);
    tlDock->setWidget(timeline_);
    addDockWidget(Qt::BottomDockWidgetArea, tlDock);
    tabifyDockWidget(mediaDock, tlDock);
    mediaDock->raise();
    connect(timeline_, &TimelineWidget::playheadChanged, this, [this] { inspector_->refreshProps(); });

    // Plug-in Manager
    auto* plugDock = new QDockWidget("Plug-in", this);
    plugDock->setObjectName("dockPlugins");
    auto* pw = new QWidget;
    auto* pv = new QVBoxLayout(pw);
    pluginList_ = new QListWidget;
    pluginList_->setObjectName("pluginList");
    pluginList_->setWordWrap(true);
    pv->addWidget(pluginList_);
    auto* prow = new QHBoxLayout;
    auto* rescanB = new QPushButton("Quét lại");
    rescanB->setObjectName("pluginRescan");
    auto* openB = new QPushButton("Mở thư mục plug-in");
    prow->addWidget(rescanB);
    prow->addWidget(openB);
    pv->addLayout(prow);
    auto* hint = new QLabel("Tự nạp lại khi bạn sửa file shader. Xem docs/PLUGIN-SDK.md để viết plug-in.");
    hint->setWordWrap(true);
    pv->addWidget(hint);
    plugDock->setWidget(pw);
    pluginPanel_ = pw;
    tabifyDockWidget(inspDock, plugDock);
    inspDock->raise();
    connect(rescanB, &QPushButton::clicked, this, [this] { pm_.rescan(); });
    connect(openB, &QPushButton::clicked, this, [this] {
        const QString dir = pm_.dirs().isEmpty() ? QString() : pm_.dirs().last();
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(pluginList_, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        const QString id = it->data(Qt::UserRole).toString();
        if (!id.isEmpty() && (it->flags() & Qt::ItemIsUserCheckable)) pm_.setEnabled(id, it->checkState() == Qt::Checked);
    });
    connect(&pm_, &PluginManager::changed, this, [this] {
        rebuildPluginList();
        rebuildTransitionCombo();
        inspector_->refreshEffectCatalog();
    });
    rebuildPluginList();
    pm_.watch(true);

    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    rebuildSceneList();
    rebuildMediaList();
}

void MainWindow::rebuildPluginList() {
    QSignalBlocker b(pluginList_);
    pluginList_->clear();
    for (const PluginInfo& i : engine_.registry().infos()) {
        const QString kind = i.kind == EffectDef::Effect ? "Effect" : "Transition";
        QListWidgetItem* it;
        if (!i.ok && !i.builtin) {
            it = new QListWidgetItem(QString("⚠ %1\n%2").arg(QFileInfo(i.dir).fileName(), i.error));
            it->setForeground(QColor(colors::kProgram));
            it->setFlags(it->flags() & ~Qt::ItemIsUserCheckable);
        } else {
            it = new QListWidgetItem(QString("%1  ·  %2%3%4").arg(i.name, kind, i.builtin ? "  ·  dựng sẵn" : QString("  ·  v%1  ·  %2").arg(i.version, i.author),
                                                               i.ok ? "" : "\n⚠ " + i.error));
            it->setToolTip(i.description);
            if (!i.builtin) {
                it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
                it->setCheckState(i.enabled ? Qt::Checked : Qt::Unchecked);
            }
            if (!i.ok) it->setForeground(QColor(colors::kWarn));
        }
        it->setData(Qt::UserRole, i.builtin ? QString() : i.id);
        pluginList_->addItem(it);
    }
}

void MainWindow::buildMenus() {
    auto* file = menuBar()->addMenu("&File");
    openAct_ = file->addAction("Mở project...", QKeySequence::Open, this, [this] { openProject(); });
    recentMenu_ = file->addMenu("Gần đây");
    file->addSeparator();
    file->addAction("Lưu", QKeySequence::Save, this, [this] { saveProject(false); });
    file->addAction("Lưu thành...", QKeySequence::SaveAs, this, [this] { saveProject(true); });
    file->addSeparator();
    importAct_ = file->addAction("Nhập media...", this, [this] {
        for (const QString& f : QFileDialog::getOpenFileNames(this, "Nhập media", QString(), kMediaFilter)) addMediaFile(f);
    });
    file->addSeparator();
    file->addAction("Thoát", QKeySequence::Quit, this, [this] { close(); });
    rebuildRecentMenu();

    auto* edit = menuBar()->addMenu("&Edit");
    // Hoan tac/Lam lai tu dinh nghia (khong dung createUndoAction) de khoa van hanh vo hieu duoc chung.
    undoAct_ = new QAction("Hoàn tác", this);
    undoAct_->setShortcut(QKeySequence::Undo);
    redoAct_ = new QAction("Làm lại", this);
    redoAct_->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    connect(undoAct_, &QAction::triggered, this, [this] { if (!locked_) doUndo(); });
    connect(redoAct_, &QAction::triggered, this, [this] { if (!locked_) undo_.redo(); });
    for (auto sig : {&QUndoStack::canUndoChanged, &QUndoStack::canRedoChanged}) connect(&undo_, sig, this, [this] { updateUndoActions(); });
    for (auto sig : {&QUndoStack::undoTextChanged, &QUndoStack::redoTextChanged}) connect(&undo_, sig, this, [this] { updateUndoActions(); });
    edit->addAction(undoAct_);
    edit->addAction(redoAct_);
    updateUndoActions();

    auto* view = menuBar()->addMenu("&View");
    for (QDockWidget* d : findChildren<QDockWidget*>()) view->addAction(d->toggleViewAction());
    view->addSeparator();
    view->addAction("Đặt lại bố cục", this, [this] {
        restoreState(defaultState_);
        for (QDockWidget* d : findChildren<QDockWidget*>()) d->show();
    });

    auto* settings = menuBar()->addMenu("&Cài đặt");
    settingsAct_ = settings->addAction("Cài đặt vận hành…", this, [this] { openSettings(); });

    auto* help = menuBar()->addMenu("&Help");
    help->addAction("Giới thiệu", this, [this] {
        QMessageBox::about(this, "LumaCast", "<b>LumaCast</b> — LED Display Controller<br>Phiên bản M3a");
    });
}

// ---------------- thao tac live ----------------

void MainWindow::cut() {
    auto ch = project_.channels[channelIdx()];
    engine_.post([ch](double now) { ch->cut(now); });
}

void MainWindow::take() {
    auto ch = project_.channels[channelIdx()];
    const TransitionSpec spec = currentSpec();
    engine_.post([ch, spec](double now) { ch->startTransition(spec, now); });
}

TransitionSpec MainWindow::currentSpec() const {
    TransitionSpec s;
    s.easing = Easing(easingCombo_->currentData().toInt());
    s.duration = durSpin_->value();
    const QString key = transCombo_->currentData().toString();
    if (key.startsWith("b:")) {
        s.type = TransitionType(key.mid(2).toInt());
    } else if (key.startsWith("p:")) {
        s.type = TransitionType::Plugin;
        s.pluginId = key.mid(2);
        s.params = transParams_.value(s.pluginId);
    } else if (key == "s") {
        s.type = TransitionType::Stinger;
        const int k = stingerCombo_->currentData().toInt();
        if (stingerCombo_->currentIndex() >= 0 && k >= 0 && k < int(project_.sources.size())) {
            s.stinger = project_.sources[k].source;
            s.cutPoint = cutSpin_->value();
            if (s.stinger && s.stinger->durationSeconds() > 0) s.duration = s.stinger->durationSeconds();
        }
    }
    return s;
}

void MainWindow::rebuildTransitionCombo() {
    const QString keep = transCombo_->currentData().toString();
    QSignalBlocker b(transCombo_);
    transCombo_->clear();
    for (auto t : {TransitionType::Fade, TransitionType::DipToBlack, TransitionType::WipeLeft, TransitionType::WipeRight,
                   TransitionType::WipeUp, TransitionType::WipeDown})
        transCombo_->addItem(transitionName(t), "b:" + QString::number(int(t)));
    for (const auto& d : engine_.registry().all(EffectDef::Transition)) transCombo_->addItem(d->name, "p:" + d->id);
    transCombo_->addItem("Stinger (video alpha)", "s");
    const int k = transCombo_->findData(keep);
    transCombo_->setCurrentIndex(k >= 0 ? k : 0);

    {
        QSignalBlocker bs(stingerCombo_);
        const QVariant keepSt = stingerCombo_->currentData();
        stingerCombo_->clear();
        for (size_t i = 0; i < project_.sources.size(); ++i)
            if (project_.sources[i].type == "video") stingerCombo_->addItem(project_.sources[i].source ? project_.sources[i].source->name() : project_.sources[i].id, int(i));
        if (stingerCombo_->count() == 0) stingerCombo_->addItem("(chưa có video)", -1);
        const int ks = stingerCombo_->findData(keepSt);
        stingerCombo_->setCurrentIndex(ks >= 0 ? ks : 0);
    }
    onTransitionChanged();
}

void MainWindow::onTransitionChanged() {
    const QString key = transCombo_->currentData().toString();
    const bool stinger = key == "s";
    stingerCombo_->setVisible(stinger);
    cutSpin_->setVisible(stinger);
    durSpin_->setEnabled(!stinger || !(stingerCombo_->currentData().toInt() >= 0 && stingerCombo_->currentIndex() >= 0 &&
                                      project_.sources[stingerCombo_->currentData().toInt()].source &&
                                      project_.sources[stingerCombo_->currentData().toInt()].source->durationSeconds() > 0));
    bool hasParams = false;
    if (key.startsWith("p:")) {
        auto d = engine_.registry().find(key.mid(2));
        hasParams = d && !d->params.isEmpty();
    }
    paramsBtn_->setVisible(hasParams);
}

void MainWindow::editTransitionParams() {
    const QString key = transCombo_->currentData().toString();
    if (!key.startsWith("p:")) return;
    auto def = engine_.registry().find(key.mid(2));
    if (!def) return;
    QDialog dlg(this);
    dlg.setWindowTitle("Tham số: " + def->name);
    auto* lay = new QVBoxLayout(&dlg);
    auto* ed = new ParamEditor(def, transParams_.value(def->id));
    ed->setObjectName("transitionParams");
    lay->addWidget(ed);
    connect(ed, &ParamEditor::changed, this, [this, id = def->id](const QVariantMap& v, int) { transParams_[id] = v; });
    auto* close = new QPushButton("Đóng");
    lay->addWidget(close);
    connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
    transParams_[def->id] = ed->values();
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
    audioPanel_->refreshMeters();
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
        const OutputHealth hl = sup_.health(i);
        const bool exposed = hl == OutputHealth::Ok;
        const uint64_t pr = sink->presented;
        const double fps = dt > 0 ? double(pr - r.lastPresented) / dt : 0;
        r.lastPresented = pr;
        if (exposed) ++ready;
        const char* col = !exposed ? colors::kProgram : sink->black ? "#8a90a2" : sink->freeze ? colors::kWarn : colors::kPreview;
        r.dot->setStyleSheet(QString("color:%1;font-size:16px").arg(col));
        const OutputConfig& c = sink->config();
        QString extra;
        if (!exposed) extra = " · " + sup_.text(i);
        else if (sink->testPattern) extra = " · MẪU THỬ";
        else if (sink->freeze) extra = " · DỪNG HÌNH";
        r.info->setText(QString("<b>%1</b><br><small>%2x%3 · kênh %4 · %5 fps%6</small>")
                            .arg(c.name).arg(c.size.width()).arg(c.size.height()).arg(c.channel + 1)
                            .arg(fps, 0, 'f', 0).arg(extra));
        QSignalBlocker bb(r.black), bf(r.freeze), bp(r.pattern);
        r.black->setChecked(sink->black);
        r.freeze->setChecked(sink->freeze);
        r.pattern->setChecked(sink->testPattern);
    }
    inspector_->updateEffectNote();
    const bool stalled = engine_.stalled();
    if (stalled != lastStalled_) {
        lastStalled_ = stalled;
        if (stalled) qCritical() << "WATCHDOG: render thread khong phan hoi - man hinh giu khung hinh cuoi";
        else qInfo() << "WATCHDOG: render thread hoat dong tro lai";
    }
    statusLabel_->setStyleSheet(stalled ? "color:#ff4d4d;font-weight:bold" : QString());
    statusLabel_->setText(QString("%1  |  GPU: %2  |  FPS: %3  |  Khung p50/p99: %7/%8 ms  |  Rơi khung: %4  |  Output: %5/%6")
                              .arg(stalled ? "⚠ RENDER THREAD KHÔNG PHẢN HỒI" : engine_.error().isEmpty() ? "System Ready" : "LỖI: " + engine_.error())
                              .arg(engine_.gpuName().isEmpty() ? "-" : engine_.gpuName())
                              .arg(engine_.fps(), 0, 'f', 1).arg(engine_.droppedFrames()).arg(ready).arg(engine_.outputCount())
                              .arg(engine_.frameP50(), 0, 'f', 1).arg(engine_.frameP99(), 0, 'f', 1));
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
    rebuildTransitionCombo();                              // danh sach nguon Stinger co the doi
    timeline_->refresh();
    ctlApi_->refreshProject();
}

// ---------------- media ----------------

bool MainWindow::addMediaFile(const QString& path) {
    if (locked_) { showMessage("Đang khoá vận hành: không nhập media được"); return false; }
    SourceDef d;
    d.path = QFileInfo(path).absoluteFilePath();
    d.type = kImageExt.contains(QFileInfo(path).suffix().toLower()) ? "image" : "video";
    QString err;
    auto src = createSource(d, &err);
    if (!src) { showMessage("Không nhập được: " + err); return false; }
    d.source = src;
    auto e = std::make_shared<ImportEntry>();
    e->def = d;
    e->scene = std::make_shared<Scene>();
    e->scene->name = src->name();
    e->scene->size = project_.channels[0]->size;
    e->scene->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Fit, true});
    pushImport(e, "Nhập media: " + src->name());
    return true;
}

void MainWindow::pushImport(std::shared_ptr<ImportEntry> e, const QString& text) { undo_.push(new ImportCmd(this, std::move(e), text)); }

void MainWindow::doUndo() {
    // Khong hoan tac nhap media khi Scene do dang On Air (go ra se doi hinh dang len song): bao va giu nguyen.
    if (undo_.canUndo())
        if (auto* c = dynamic_cast<const ImportCmd*>(undo_.command(undo_.index() - 1)); c && importInUse(c->entry())) {
            showMessage("Không hoàn tác được: Scene này đang ở PROGRAM/đang chuyển cảnh. Đưa Scene khác lên Program rồi hoàn tác lại.");
            qWarning().noquote() << "Hoàn tác nhập media bị từ chối: Scene đang On Air";
            return;
        }
    undo_.undo();
}

bool MainWindow::importInUse(const ImportEntry& e) const {
    for (const auto& c : project_.channels) {
        const int pg = c->program, pv = c->preview;
        const auto& sc = c->scenes;
        if (pg >= 0 && pg < int(sc.size()) && sc[pg] == e.scene) return true;
        if (c->inTransition() && pv >= 0 && pv < int(sc.size()) && sc[pv] == e.scene) return true;
    }
    return false;
}

// Them (hoac them lai khi Lam lai) nguon + Scene. Doi render thread chay xong roi cap nhat ban sao UI NGAY, de cac buoc Hoan tac/Lam lai
// ke tiep (vd. sua layer cua Scene nay) thay du lieu nhat quan. Bo qua phan da co (an toan khi lap).
void MainWindow::applyImport(const std::shared_ptr<ImportEntry>& e) {
    engine_.postAndWait([this, e](double) {
        if (e->addsSource) {
            bool have = false;
            for (const auto& s : project_.sources) have = have || (s.source == e->def.source);
            if (!have) {
                if (e->def.id.isEmpty()) {
                    int n = int(project_.sources.size()) + 1;
                    auto taken = [&](const QString& id) { for (const auto& s : project_.sources) if (s.id == id) return true; return false; };
                    while (taken(QString("src%1").arg(n))) ++n;
                    e->def.id = QString("src%1").arg(n);
                }
                project_.sources.push_back(e->def);
            }
        }
        if (std::find(project_.scenes.begin(), project_.scenes.end(), e->scene) == project_.scenes.end()) {
            project_.scenes.push_back(e->scene);
            for (auto& c : project_.channels) c->scenes.push_back(e->scene);
        }
    });
    onProjectChanged();
}

// Go Scene (va nguon neu khong con ai dung) ra khoi project + moi Channel; dieu chinh chi so Program/Preview.
// Neu luc nay Scene dang On Air (lenh live xen vao giua kiem tra UI va render thread) thi KHONG go, bao lai va khoi phuc buoc hoan tac.
void MainWindow::unapplyImport(const std::shared_ptr<ImportEntry>& e, const QUndoCommand* cmd) {
    auto removedIdx = std::make_shared<std::atomic<int>>(-1);
    auto busy = std::make_shared<std::atomic<bool>>(false);
    engine_.postAndWait([this, e, removedIdx, busy](double) {
        auto sit = std::find(project_.scenes.begin(), project_.scenes.end(), e->scene);
        if (sit == project_.scenes.end()) return;
        for (const auto& c : project_.channels) {
            auto it = std::find(c->scenes.begin(), c->scenes.end(), e->scene);
            if (it == c->scenes.end()) continue;
            const int k = int(it - c->scenes.begin());
            if (c->program == k || (c->inTransition() && c->preview == k)) { *busy = true; return; }
        }
        for (auto& c : project_.channels) {
            auto it = std::find(c->scenes.begin(), c->scenes.end(), e->scene);
            if (it == c->scenes.end()) continue;
            const int k = int(it - c->scenes.begin());
            c->scenes.erase(it);
            const int pv = c->preview;
            if (pv == k) c->preview = -1;
            else if (pv > k) c->preview = pv - 1;
            const int pg = c->program;
            if (pg > k) c->program = pg - 1;
        }
        *removedIdx = int(sit - project_.scenes.begin());
        project_.scenes.erase(sit);
        if (e->addsSource) {                                   // chi go nguon neu khong Scene nao khac con dung
            bool used = false;
            for (const auto& sc : project_.scenes)
                for (const auto& l : sc->layers) used = used || l.source == e->def.source;
            if (!used)
                project_.sources.erase(std::remove_if(project_.sources.begin(), project_.sources.end(),
                                                      [&](const SourceDef& s) { return s.source == e->def.source; }), project_.sources.end());
        }
    });
    if (*busy) {
        QMetaObject::invokeMethod(this, [this, cmd] {
            showMessage("Hoàn tác nhập media bị huỷ: Scene vừa được đưa lên PROGRAM");
            qWarning().noquote() << "Hoàn tác nhập media bị huỷ giữa chừng: Scene đang On Air";
            if (undo_.index() < undo_.count() && undo_.command(undo_.index()) == cmd) undo_.redo();   // giu ngan xep nhat quan voi project
        }, Qt::QueuedConnection);
        return;
    }
    if (*removedIdx >= 0) {
        inspector_->sceneRemoved(*removedIdx);
        timeline_->sceneRemoved(*removedIdx);
    }
    onProjectChanged();
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
            if (ok) { addRecent(path); updateTitle(); QFile::remove(path + ".autosave"); QSettings stl = appSettings(); rememberProject(stl, path); }
        }, Qt::QueuedConnection);
    });
}

void MainWindow::updateTitle() {
    setWindowTitle(QString("LumaCast — %1%2").arg(project_.filePath.isEmpty() ? "Chưa đặt tên" : QFileInfo(project_.filePath).fileName(), locked_ ? "  [KHOÁ VẬN HÀNH]" : ""));
}

void MainWindow::addRecent(const QString& path) {
    QSettings st = appSettings();
    QStringList l = st.value("recent").toStringList();
    l.removeAll(path);
    l.prepend(path);
    while (l.size() > 8) l.removeLast();
    st.setValue("recent", l);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu() {
    recentMenu_->clear();
    const QStringList l = appSettings().value("recent").toStringList();
    for (const QString& p : l) recentMenu_->addAction(p, this, [this, p] { launchProject(p); });
    recentMenu_->setEnabled(!l.isEmpty() && !locked_);
}

void MainWindow::openProject() {
    const QString p = QFileDialog::getOpenFileName(this, "Mở project", QString(), "LumaCast project (*.json)");
    if (!p.isEmpty()) launchProject(p);
}

// Mo project khac = khoi dong lai voi project do (engine/output dung lai tu dau, don gian va chac chan).
void MainWindow::launchProject(const QString& path) {
    if (locked_) { showMessage("Đang khoá vận hành: không mở project khác được"); return; }
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
    if (locked_ && askOnClose_) {                          // khoa: khong dong duoc cho den khi mo khoa (mo khoa = xac nhan)
        qWarning().noquote() << "Khoá vận hành: từ chối đóng cửa sổ chính";
        showMessage("Đang KHOÁ vận hành — giữ nút khoá 1 giây để mở khoá rồi mới thoát được");
        e->ignore();
        return;
    }
    if (askOnClose_ &&
        QMessageBox::question(this, "Thoát LumaCast", "Thoát sẽ tắt tất cả output.\nBạn chắc chắn?",
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        e->ignore();
        return;
    }
    QSettings st = appSettings();
    st.setValue("geometry", saveGeometry());
    st.setValue("state", saveState());
    e->accept();
    QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);   // cua so output khong tu thoat
}

} // namespace lc
