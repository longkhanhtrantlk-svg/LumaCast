#include "MainWindow.h"
#include "AboutInfo.h"
#include "live/Ndi.h"
#include "core/Features.h"
#include "AudioPanel.h"
#include "DockChrome.h"
#include "InspectorPanel.h"
#include "MappingWindow.h"
#include "LiveDialogs.h"
#include "LockButton.h"
#include "MonitorWidget.h"
#include "NowShowing.h"
#include "OverlayPanel.h"
#include "PreviewMonitor.h"
#include "Text.h"
#include "Ui.h"
#include "MultiviewWindow.h"
#include "ParamEditor.h"
#include "PlaylistPanel.h"
#include "ScenePanel.h"
#include "ScreenDialog.h"
#include "Icons.h"
#include "SettingsDialog.h"
#include "core/AppSettings.h"
#include "core/Startup.h"
#include "TimelineWidget.h"
#include "live/LiveSource.h"
#include "render/NdiOutput.h"
#include "render/OutputWindow.h"
#include "Theme.h"
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDesktopServices>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QAbstractSpinBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QScreen>
#include <QScrollArea>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolButton>
#include <QUndoCommand>
#include <QUrl>
#include <QVBoxLayout>

namespace lc {

static const QStringList kImageExt = {"png", "jpg", "jpeg", "bmp", "gif", "webp"};
static const QStringList kVideoExt = {"mp4", "mov", "mkv", "avi", "webm"};
// Bo loc hop thoai "Nhap media..."; keo tha chi nhan cung cac phan mo rong nay.
static const QString kMediaFilter = "Media (*." + (kVideoExt + kImageExt).join(" *.") + ");;Tất cả (*.*)";

// File tren may (hoac o mang) trong mot lan keo; rong neu khong phai keo file (vd. keo doi thu tu Layer).
static QStringList draggedFiles(const QMimeData* mime) {
    QStringList files;
    if (mime)
        for (const QUrl& u : mime->urls())
            if (u.isLocalFile()) files << u.toLocalFile();
    return files;
}

namespace {
// Hoan tac/Lam lai cho nhap media / them nguon (PRJ-07). Khong gop buoc.
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

class RemoveSceneCmd : public QUndoCommand {
public:
    RemoveSceneCmd(MainWindow* w, std::shared_ptr<MainWindow::SceneRemoval> r, const QString& text) : QUndoCommand(text), w_(w), r_(std::move(r)) {}
    void redo() override { if (!w_->applyRemoval(r_)) w_->removalRefused(this); }
    void undo() override { w_->undoRemoval(r_); }
    const Scene* scene() const { return r_->scene.get(); }
private:
    MainWindow* w_;
    std::shared_ptr<MainWindow::SceneRemoval> r_;
};

class RenameSceneCmd : public QUndoCommand {
public:
    RenameSceneCmd(MainWindow* w, std::shared_ptr<Scene> s, QString after)
        : QUndoCommand("Đổi tên Scene: " + after), w_(w), s_(std::move(s)), before_(s_->name), after_(std::move(after)) {}
    void redo() override { w_->applySceneName(s_, after_); }
    void undo() override { w_->applySceneName(s_, before_); }
private:
    MainWindow* w_;
    std::shared_ptr<Scene> s_;
    QString before_, after_;
};

class ScreenCmd : public QUndoCommand {
public:
    ScreenCmd(MainWindow* w, MainWindow::ScreenState before, MainWindow::ScreenState after, const QString& text)
        : QUndoCommand(text), w_(w), before_(std::move(before)), after_(std::move(after)) {}
    void redo() override { w_->setScreenState(after_); }
    void undo() override { w_->setScreenState(before_); }
private:
    MainWindow* w_;
    MainWindow::ScreenState before_, after_;
};

bool sameOutput(const OutputConfig& a, const OutputConfig& b) {
    return a.kind == b.kind && a.name == b.name && a.ndiName == b.ndiName && a.ndiAudio == b.ndiAudio && a.channel == b.channel
           && a.screenIndex == b.screenIndex && a.screenName == b.screenName && a.fullscreen == b.fullscreen && a.size == b.size
           && a.scale == b.scale && a.rotation == b.rotation && a.crop == b.crop && a.area == b.area
           && a.cal.brightness == b.cal.brightness && a.cal.contrast == b.cal.contrast && a.cal.gamma == b.cal.gamma
           && a.cal.gainR == b.cal.gainR && a.cal.gainG == b.cal.gainG && a.cal.gainB == b.cal.gainB;
}

bool sameFile(const QString& a, const QString& b) {
    const QString ca = QFileInfo(a).canonicalFilePath(), cb = QFileInfo(b).canonicalFilePath();
#ifdef _WIN32
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    return !ca.isEmpty() && ca.compare(cb, cs) == 0;
}
} // namespace

MainWindow::MainWindow(Project& project, RenderEngine& engine, QStringList pluginDirs)
    : project_(project), engine_(engine), sup_(engine), pm_(engine.registry(), std::move(pluginDirs)) {
    ui::installHints();                             // nut mo noi ly do
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
    statusReopen_ = new QPushButton(text::outClosedStatus());
    statusReopen_->setObjectName("statusReopen");
    statusReopen_->setToolTip(text::outClosedStatusTip());
    statusReopen_->setStyleSheet("background:#8a1c1c;color:white;font-weight:bold;padding:0 8px");
    statusReopen_->setFocusPolicy(Qt::NoFocus);
    statusReopen_->setVisible(false);
    statusBar()->insertPermanentWidget(0, statusReopen_);        // "permanent": thong bao tam thoi khong che mat nut
    connect(statusReopen_, &QPushButton::clicked, this, [this] { reopenOutputs(); });
    connect(ctl_, &ControlService::statusChanged, this, [this] { updateControlStatus(); });
    buildMenus();
    // Keo tha file tu Explorer. Loc o muc ung dung de moi widget trong cua so (ke ca o nhap so, von tu nhan tha) xu ly nhu nhau;
    // dock tach roi la cua so rieng nen cung phai bat nhan tha.
    setAcceptDrops(true);
    for (QDockWidget* d : findChildren<QDockWidget*>()) d->setAcceptDrops(true);
    qApp->installEventFilter(this);
    QSettings st = appSettings();
    if (st.contains("geometry")) restoreGeometry(st.value("geometry").toByteArray());
    else resize(1500, 900);
    // khong de cua so lon hon man hinh (vd. 1920x1080 phong 125% chi con 1536x816 diem logic)
    if (const QScreen* sc = screen(); sc && !isMaximized() && !isFullScreen())
        resize(size().boundedTo(sc->availableGeometry().size() - QSize(2, 34)));
    initLayout();
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
    showLoadWarnings();
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
    ui::setLockedState(on);                       // truoc khi vo hieu cac nut: de tooltip noi dung ly do
    lockDocks(on);                                // khoa luon viec keo khung
    lockBtn_->setLocked(on);
    inspector_->setLocked(on);
    if (mapping_) mapping_->setLocked(on);
    timeline_->setLocked(on);
    playlist_->setLocked(on);
    pluginPanel_->setEnabled(!on);
    audioPanel_->setEditLocked(on);
    overlayPanel_->setLocked(on);              // khoa: chi con hien/an va dong ho dem nguoc
    scenes_->setLocked(on);                    // khoa: khong nhap/doi ten/xoa/lap; phat/tua/tieng van dung duoc
    importAct_->setEnabled(!on);
    for (QAction* a : {addNdiAct_, addCamAct_, addNdiOutAct_}) a->setEnabled(!on);
    openAct_->setEnabled(!on);
    recentMenu_->setEnabled(!on && !appSettings().value("recent").toStringList().isEmpty());
    settingsAct_->setEnabled(!on);
    screenAct_->setEnabled(!on);
    newAct_->setEnabled(!on);
    screenBtn_->setEnabled(!on);
    inputAct_->setEnabled(!on);
    updateUndoActions();
    updateLayoutActions();
    refreshPreviewEditing();
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
    if (midi_ && midi_->config().enabled) parts << (midi_->connected() ? "MIDI: " + midi_->config().device : QString("MIDI: chưa nối"));
    ctlLabel_->setText(parts.isEmpty() ? QString() : "Điều khiển ngoài: " + parts.join(" · "));
}

void MainWindow::showMapping() {
    if (!mapping_) {
        mapping_ = new MappingWindow(project_, engine_, *inspector_, this);
        mapping_->setLocked(locked_);
        mapping_->showChannel(channelIdx());
    }
    mapping_->show();
    mapping_->raise();
    mapping_->activateWindow();
}

// Project mo duoc nhung thieu file: bao ro tung file, khong chan (khong dung hop thoai modal giua show).
void MainWindow::showLoadWarnings() {
    if (project_.loadWarnings.isEmpty()) return;
    for (const QString& w : project_.loadWarnings) qWarning().noquote() << w;
    auto* box = new QMessageBox(QMessageBox::Warning, "Thiếu file media",
                                QString("Project đã mở nhưng %1 file không dùng được (đang hiện hình “THIẾU FILE”):\n\n%2\n\n"
                                        "Chép file về đúng chỗ rồi mở lại project. Lưu project không làm mất tham chiếu.")
                                    .arg(project_.loadWarnings.size()).arg(project_.loadWarnings.join("\n")),
                                QMessageBox::Ok, this);
    box->setObjectName("missingFiles");
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setModal(false);
    box->show();
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
    chanCombo_->setObjectName("chanCombo");
    for (size_t i = 0; i < project_.channels.size(); ++i)
        chanCombo_->addItem(QString("Kênh %1: %2").arg(i + 1).arg(project_.channels[i]->name));
    top->addWidget(chanCombo_);
    top->addStretch();
    layoutBtn_ = new QToolButton;                        // hien ten bo cuc dang dung; menu doi bo cuc
    layoutBtn_->setObjectName("layoutButton");
    layoutBtn_->setIcon(glyphIcon(Glyph::Layout));
    layoutBtn_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    layoutBtn_->setPopupMode(QToolButton::InstantPopup);
    layoutBtn_->setFocusPolicy(Qt::NoFocus);
    layoutBtn_->setToolTip(text::layoutButtonTip());
    top->addWidget(layoutBtn_);
    lockBtn_ = new LockButton;
    top->addWidget(lockBtn_);
    connect(lockBtn_, &LockButton::lockRequested, this, [this] { setLocked(true); });
    connect(lockBtn_, &LockButton::unlockConfirmed, this, [this] { setLocked(false); });
    chanCombo_->setVisible(project_.channels.size() > 1);
    v->addLayout(top);

    // moi man mot cot: duoi Preview la nut chon nhanh khung hinh, duoi Program la dai "Dang hien" (cung chieu cao de hai man deu)
    auto* mon = new QHBoxLayout;
    preview_ = new PreviewMonitor(QColor(colors::kPreview));
    program_ = new MonitorWidget("PROGRAM", QColor(colors::kProgram), true);
    auto* pvCol = new QVBoxLayout;
    pvCol->setSpacing(3);
    pvCol->addWidget(preview_, 1);
    auto* fitHost = new QWidget;
    fitHost->setFixedHeight(NowShowing::kHeight);
    auto* fits = new QHBoxLayout(fitHost);
    fits->setContentsMargins(0, 0, 0, 0);
    fits->setSpacing(4);
    const struct { Glyph g; const char* name; QString tip; LayerFit fit; } fitDefs[3] = {
        {Glyph::Fit, "pvFit", text::pvFit(), LayerFit::Fit}, {Glyph::Fill, "pvFill", text::pvFill(), LayerFit::Fill},
        {Glyph::Stretch, "pvStretch", text::pvStretch(), LayerFit::Stretch}};
    for (int i = 0; i < 3; ++i) {
        fitBtn_[i] = ui::iconButton(fitDefs[i].g, fitDefs[i].name, fitDefs[i].tip);
        ui::setWhy(fitBtn_[i], text::pvNoLayerWhy());
        fits->addWidget(fitBtn_[i]);
        connect(fitBtn_[i], &QPushButton::clicked, this, [this, fit = fitDefs[i].fit] { applyPreviewFit(fit); });
    }
    auto* hint = new QLabel(text::pvDragHint());
    hint->setStyleSheet("color:#8a90a2");
    hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    hint->setToolTip(text::pvDragHint());
    fits->addWidget(hint, 1);
    pvCol->addWidget(fitHost);
    auto* pgCol = new QVBoxLayout;
    pgCol->setSpacing(3);
    pgCol->addWidget(program_, 1);
    nowShowing_ = new NowShowing;
    pgCol->addWidget(nowShowing_);
    mon->addLayout(pvCol, 1);
    mon->addLayout(pgCol, 1);
    v->addLayout(mon, 1);
    preview_->onPick = [this](int layer) {
        if (!locked_) inspector_->selectLayer(previewSceneIndex(), layer);
    };
    preview_->onDrag = [this](int layer, const QRectF& rect, int gesture) {
        inspector_->setLayerRect(previewSceneIndex(), layer, rect, text::pvUndoDrag(), 8000000 + gesture % 1000000);
    };
    nowShowing_->onHide = [this](int id) {
        project_.channels[size_t(channelIdx())]->overlays.setOn(id, false, engine_.now());
        overlayPanel_->refreshLive();
        refresh();
    };
    nowShowing_->onHideAll = [this] {
        auto& stack = project_.channels[size_t(channelIdx())]->overlays;
        const double now = engine_.now();
        for (const OverlayDef& d : stack.defs()) stack.setOn(d.id, false, now);
        overlayPanel_->refreshLive();
        refresh();
    };

    auto* bar = new QHBoxLayout;
    auto* cutB = new QPushButton("CUT");
    cutB->setObjectName("cut");
    auto* takeB = new QPushButton("TAKE");
    takeB->setObjectName("take");
    cutBtn_ = cutB;
    takeBtn_ = takeB;
    transCombo_ = new QComboBox;
    transCombo_->setObjectName("transCombo");
    easingCombo_ = new QComboBox;
    easingCombo_->setObjectName("easingCombo");
    for (auto e : {Easing::EaseInOut, Easing::Linear, Easing::EaseIn, Easing::EaseOut}) easingCombo_->addItem(easingName(e), int(e));
    easingCombo_->setToolTip("Đường cong tốc độ của hiệu ứng (T-bar kéo tay luôn tuyến tính)");
    paramsBtn_ = ui::iconButton(Glyph::Params, "transParams", text::transParams());
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
    durSpin_->setObjectName("transDuration");
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
    ftb_ = new QPushButton("FTB");
    ftb_->setObjectName("ftb");
    ftb_->setCheckable(true);
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
    connect(chanCombo_, &QComboBox::currentIndexChanged, this, [this] { rebuildSceneList(); playlist_->setChannel(channelIdx()); });
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

    statusLabel_ = new QLabel;
    statusBar()->addPermanentWidget(statusLabel_);
}

// Boc noi dung dock trong vung cuon: man hinh nho thi hien thanh cuon thay vi ep cac nut de len nhau / day cua so ra ngoai man.
static QScrollArea* scrollable(QWidget* content) {
    auto* sa = new QScrollArea;
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setWidget(content);
    return sa;
}

void MainWindow::buildDocks() {
    // Outputs
    auto* outDock = new QDockWidget("Outputs", this);
    outDock->setObjectName("dockOutputs");
    auto* outW = new QWidget;
    auto* ov = new QVBoxLayout(outW);
    ov->setContentsMargins(6, 4, 6, 4);
    screenBtn_ = ui::iconToolButton(Glyph::Screen, "screenSetupBtn", text::outScreenSetup());
    connect(screenBtn_, &QToolButton::clicked, this, [this] { showScreenSetup(); });
    auto* setupRow = new QHBoxLayout;
    setupRow->addWidget(screenBtn_);
    auto* allPattern = ui::iconButton(Glyph::PatternAll, "allPattern", text::outPatternAll());
    allPattern->setCheckable(true);
    setupRow->addWidget(allPattern);
    setupRow->addStretch();
    ov->addLayout(setupRow);
    outRowsLayout_ = new QVBoxLayout;
    ov->addLayout(outRowsLayout_);
    for (int i = 0; i < engine_.outputCount(); ++i) addOutputRow(i);
    connect(allPattern, &QPushButton::clicked, this, [this](bool on) {
        for (int i = 0; i < engine_.outputCount(); ++i) engine_.output(i)->testPattern = on;
    });
    ov->addStretch();
    outDock->setWidget(scrollable(outW));

    // Am thanh (M5a)
    auto* audDock = new QDockWidget("Âm thanh", this);
    audDock->setObjectName("dockAudio");
    audioPanel_ = new AudioPanel(project_, engine_);
    audDock->setWidget(scrollable(audioPanel_));

    // Inspector
    auto* inspDock = new QDockWidget("Inspector", this);
    inspDock->setObjectName("dockInspector");
    inspector_ = new InspectorPanel(project_, engine_, &undo_);
    inspDock->setWidget(scrollable(inspector_));
    connect(inspector_, &InspectorPanel::mappingRequested, this, [this] { showMapping(); });

    // Scenes: danh sach o anh thu nho duoi man hinh (nhap media, camera, keo tha; phat/tieng/doi ten/xoa tren tung o)
    auto* scenesDock = new QDockWidget("Scenes", this);
    scenesDock->setObjectName("dockScenes");
    scenes_ = new ScenePanel(project_, engine_);
    sceneList_ = scenes_->list();
    scenesDock->setWidget(scenes_);
    scenes_->renameScene = [this](const std::shared_ptr<Scene>& s, const QString& n) { renameScene(s, n); };
    scenes_->deleteScene = [this](const std::shared_ptr<Scene>& s) { deleteScene(s); };
    scenes_->setSceneLoop = [this](const std::shared_ptr<Scene>& s, bool on) {
        const auto it = std::find(project_.scenes.begin(), project_.scenes.end(), s);
        if (it != project_.scenes.end()) timeline_->setSceneLoop(int(it - project_.scenes.begin()), on);
    };
    connect(scenes_->importButton(), &QToolButton::clicked, this, [this] { chooseMediaFiles(); });
    connect(scenes_->ndiButton(), &QToolButton::clicked, this, [this] { showAddNdiSource(); });
    connect(scenes_->cameraButton(), &QToolButton::clicked, this, [this] { showAddCamera(); });
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

    // Timeline (tab canh Scenes)
    auto* tlDock = new QDockWidget("Timeline", this);
    tlDock->setObjectName("dockTimeline");
    timeline_ = new TimelineWidget(*inspector_, engine_, project_);
    tlDock->setWidget(scrollable(timeline_));
    if (!Entitlements::current().timeline) {                      // goi khong co timeline/keyframe: lam xam, giai thich
        timeline_->setEnabled(false);
        tlDock->setWindowTitle("Timeline (không có trong gói)");
    }

    // Playlist / slide anh tu chuyen (tab canh Scenes)
    auto* plDock = new QDockWidget("Playlist", this);
    plDock->setObjectName("dockPlaylist");
    playlist_ = new PlaylistPanel(project_, engine_, &undo_);
    playlist_->mediaFilter = kMediaFilter;
    playlist_->importFile = [this](const QString& path) { return importMedia(path); };
    playlist_->showMessage = [this](const QString& m) { showMessage(m); };
    plDock->setWidget(playlist_);
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
    auto* rescanB = ui::iconButton(Glyph::Refresh, "pluginRescan", text::pluginRescan());
    auto* openB = ui::iconButton(Glyph::Folder, "pluginFolder", text::pluginFolder());
    prow->addWidget(rescanB);
    prow->addWidget(openB);
    prow->addStretch();
    pv->addLayout(prow);
    auto* hint = new QLabel("Tự nạp lại khi bạn sửa file shader. Xem docs/PLUGIN-SDK.md để viết plug-in.");
    hint->setWordWrap(true);
    pv->addWidget(hint);
    plugDock->setWidget(pw);
    pluginPanel_ = pw;

    // Lop phu chu / logo (SRC-08), theo Channel dang chon
    auto* ovDock = new QDockWidget("Overlay", this);
    ovDock->setObjectName("dockOverlay");
    overlayPanel_ = new OverlayPanel(project_, engine_, &undo_);
    ovDock->setWidget(overlayPanel_);
    connect(chanCombo_, &QComboBox::currentIndexChanged, this, [this] { overlayPanel_->setChannel(channelIdx()); });
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
        playlist_->refreshTransitions();
        inspector_->refreshEffectCatalog();
    });
    rebuildPluginList();
    pm_.watch(true);

    // hai cot canh nhau, gop tab, keo ca nhom
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
    setDockNestingEnabled(true);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    chrome_ = new DockChrome(this);                      // ten khung tren dau (kieu tab), khong lap dong ten
    arrangeDocks("prepare");                             // restoreState() can cac dock da nam trong cua so
    chrome_->adopt();
    rebuildSceneList();
}

void MainWindow::addOutputRow(int i) {
    OutputSink* sink = engine_.output(i);
    const OutputConfig& c = sink->config();
    OutRow r;
    r.box = new QWidget;
    auto* row = new QGridLayout(r.box);                 // thong tin o tren, nut o duoi: vua cot hep ~220
    row->setContentsMargins(0, 0, 0, 2);
    row->setHorizontalSpacing(3);
    row->setVerticalSpacing(1);
    row->setColumnStretch(1, 1);
    r.dot = new QLabel("●");
    r.info = new QLabel(QString("<b>%1</b><br><small>%2x%3 · kênh %4</small>")
                            .arg(c.name).arg(c.size.width()).arg(c.size.height()).arg(c.channel + 1));
    r.info->setWordWrap(true);
    r.info->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    r.info->setMinimumWidth(60);
    r.black = ui::iconToolButton(Glyph::Black, QString("outBlack%1").arg(i).toUtf8().constData(), text::outBlack());
    r.black->setCheckable(true);
    r.freeze = ui::iconToolButton(Glyph::Freeze, QString("outFreeze%1").arg(i).toUtf8().constData(), text::outFreeze());
    r.freeze->setCheckable(true);
    r.pattern = ui::iconToolButton(Glyph::Pattern, QString("outPattern%1").arg(i).toUtf8().constData(), text::outPattern());
    r.pattern->setCheckable(true);
    row->addWidget(r.dot, 0, 0);
    row->addWidget(r.info, 0, 1, 1, 4);
    auto* alerts = new QHBoxLayout;
    r.reopen = new QToolButton;
    r.reopen->setText("Mở lại");
    r.reopen->setObjectName(QString("reopenOutput%1").arg(i));
    r.reopen->setToolTip("Cửa sổ Output này đã bị đóng: bấm để hiện lại đúng chỗ");
    r.reopen->setStyleSheet("background:#8a1c1c;color:white;font-weight:bold");
    r.reopen->setVisible(false);
    alerts->addWidget(r.reopen);
    r.warn = new QToolButton;
    r.warn->setText("⚠");
    r.warn->setObjectName(QString("outputWarn%1").arg(i));
    r.warn->setStyleSheet("color:#ffb74d;font-weight:bold");
    r.warn->setFocusPolicy(Qt::NoFocus);
    r.warn->setVisible(false);
    connect(r.warn, &QToolButton::clicked, this, [this] { showScreenSetup(); });
    alerts->addWidget(r.warn);
    alerts->addStretch();
    row->addLayout(alerts, 1, 1);
    row->addWidget(r.black, 1, 2);
    row->addWidget(r.freeze, 1, 3);
    row->addWidget(r.pattern, 1, 4);
    outRowsLayout_->addWidget(r.box);
    connect(r.black, &QToolButton::toggled, this, [sink](bool on) { sink->black = on; });
    connect(r.freeze, &QToolButton::toggled, this, [sink](bool on) { sink->freeze = on; });
    connect(r.pattern, &QToolButton::toggled, this, [sink](bool on) { sink->testPattern = on; });
    connect(r.reopen, &QToolButton::clicked, this, [this, sink] {
        auto* w = dynamic_cast<OutputWindow*>(sink);
        if (w && !w->applyScreen()) showMessage("Không mở lại được: màn hình của Output này không còn");
    });
    outRows_.push_back(r);
}

void MainWindow::rebuildOutputRows() {
    for (OutRow& r : outRows_) {
        r.box->hide();
        r.box->setParent(nullptr);
        r.box->deleteLater();
    }
    outRows_.clear();
    for (int i = 0; i < engine_.outputCount(); ++i) addOutputRow(i);
    if (binder_) updateKeyHints();
}

void MainWindow::reopenOutputs() {
    bool failed = false;
    for (int i = 0; i < engine_.outputCount(); ++i)
        if (auto* w = dynamic_cast<OutputWindow*>(engine_.output(i)); w && !w->isVisible() && w->canShow())
            failed = !w->applyScreen() || failed;
    if (failed) showMessage("Không mở lại được: màn hình của Output này không còn");
}

// ---------------- Preview: keo hinh ----------------

int MainWindow::previewSceneIndex() const {
    const Channel& ch = *project_.channels[size_t(channelIdx())];
    const int pv = ch.preview;
    if (pv < 0 || pv >= int(ch.scenes.size())) return -1;
    const auto it = std::find(project_.scenes.begin(), project_.scenes.end(), ch.scenes[size_t(pv)]);
    return it == project_.scenes.end() ? -1 : int(it - project_.scenes.begin());
}

// Layer ma Fit/Fill/Stretch tac dong: layer dang chon o Inspector neu no dang xem dung Scene o Preview, khong thi layer tren cung.
int MainWindow::previewTargetLayer(int si) const {
    const std::vector<Layer>* layers = inspector_->layersOf(si);
    if (!layers) return -1;
    if (inspector_->currentSceneIndex() == si) {
        const int li = inspector_->currentLayerIndex();
        if (li >= 0 && li < int(layers->size())) return li;
    }
    for (int li = int(layers->size()) - 1; li >= 0; --li)
        if ((*layers)[size_t(li)].visible && (*layers)[size_t(li)].source) return li;
    return -1;
}

void MainWindow::refreshPreviewEditing() {
    if (!inspector_) return;
    const int si = previewSceneIndex();
    const std::vector<Layer>* layers = inspector_->layersOf(si);
    std::vector<PreviewMonitor::Box> boxes;
    std::shared_ptr<Scene> scene;
    bool onAir = false;
    if (layers) {
        scene = project_.scenes[size_t(si)];
        const double t = double(scene->clock.time);
        for (int li = 0; li < int(layers->size()); ++li) {
            const Layer& l = (*layers)[size_t(li)];
            if (l.visible && l.source) boxes.push_back({li, evalLayer(l, t).rect});
        }
        onAir = sceneOnAir(scene.get());                  // ca khi dang chuyen canh (TAKE / T-bar / playlist)
    }
    const bool editable = !locked_ && !boxes.empty();
    preview_->setBoxes(scene.get(), std::move(boxes), inspector_->currentSceneIndex() == si ? inspector_->currentLayerIndex() : -1, onAir, editable);
    for (QPushButton* b : fitBtn_) {
        ui::setWhy(b, locked_ ? text::lockedWhy() : text::pvNoLayerWhy());
        b->setEnabled(editable);
    }
}

void MainWindow::applyPreviewFit(LayerFit fit) {
    const int si = previewSceneIndex(), li = previewTargetLayer(si);
    if (locked_ || li < 0) return;
    inspector_->selectLayer(si, li);
    if (sceneOnAir(project_.scenes[size_t(si)].get())) showMessage(text::pvOnAir());
    const char* name = fit == LayerFit::Fit ? "Fit" : fit == LayerFit::Fill ? "Fill" : "Stretch";
    inspector_->setLayerRect(si, li, QRectF(0, 0, 1, 1), text::pvUndoFit().arg(name), -1, int(fit));
    refreshPreviewEditing();
}

// ---------------- thiet lap man hinh ----------------

ScreenSetup MainWindow::screenSetup() const {
    ScreenSetup s;
    for (int i = 0; i < engine_.outputCount(); ++i) s.outputs.push_back(engine_.output(i)->config());
    for (const auto& ch : project_.channels) s.canvases.push_back(ch->size);
    return s;
}

bool MainWindow::applyScreenSetup(const ScreenSetup& after, const std::vector<char>& allScenes, const QString& text) {
    if (locked_) { showMessage("Đang khoá vận hành: không đổi màn hình / khung hình được"); return false; }
    const ScreenSetup now = screenSetup();
    auto reject = [this](const QString& why) { showMessage("Không áp dụng được: " + why); return false; };
    if (after.outputs.empty()) return reject("cần ít nhất một Output");
    if (after.canvases.size() != project_.channels.size()) return reject("số kênh không khớp");
    for (const QSize& s : after.canvases)
        if (s.width() < kMinCanvasSide || s.height() < kMinCanvasSide || s.width() > kMaxCanvasSide || s.height() > kMaxCanvasSide)
            return reject("khung hình không hợp lệ");
    int ndiBefore = 0, ndiAfter = 0;
    for (const OutputConfig& o : now.outputs) ndiBefore += o.kind == OutputKind::Ndi;
    for (const OutputConfig& o : after.outputs) {
        const int limit = o.kind == OutputKind::Ndi ? 8192 : kMaxCanvasSide;
        if (o.channel < 0 || o.channel >= int(project_.channels.size())) return reject("Output trỏ tới kênh không có");
        if (o.size.width() < 16 || o.size.height() < 16 || o.size.width() > limit || o.size.height() > limit) return reject("kích thước Output không hợp lệ");
        if (o.crop.width() <= 0 || o.crop.height() <= 0) return reject("vùng cắt không hợp lệ");
        ndiAfter += o.kind == OutputKind::Ndi;
    }
    const Entitlements& ent = Entitlements::current();
    if (ndiAfter > ndiBefore && !ent.ndi) return reject("NDI không có trong gói giấy phép hiện tại");
    if (ent.maxOutputs > 0 && after.outputs.size() > now.outputs.size() && int(after.outputs.size()) > ent.maxOutputs)
        return reject(QString("gói giấy phép tối đa %1 Output").arg(ent.maxOutputs));

    ScreenState from{now.outputs, now.canvases, {}}, to{after.outputs, after.canvases, {}};
    for (size_t c = 0; c < project_.channels.size(); ++c) {
        const bool all = c < allScenes.size() && allScenes[c];
        const QSize old = now.canvases[c], next = after.canvases[c];
        if (old == next && !all) continue;
        for (const auto& s : project_.channels[c]->scenes) {
            if (s->size == next || (!all && s->size != old)) continue;
            if (std::any_of(from.scenes.begin(), from.scenes.end(), [&](const auto& e) { return e.first == s; })) continue;
            from.scenes.push_back({s, s->size});
            to.scenes.push_back({s, next});
        }
    }
    const bool sameOutputs = now.outputs.size() == after.outputs.size() &&
                             std::equal(now.outputs.begin(), now.outputs.end(), after.outputs.begin(), sameOutput);
    if (sameOutputs && now.canvases == after.canvases && to.scenes.empty()) return true;      // khong co gi doi
    undo_.push(new ScreenCmd(this, std::move(from), std::move(to), text));
    showMessage("Đã áp dụng: " + text);
    return true;
}

void MainWindow::setScreenState(const ScreenState& state) {
    // canvas + Scene + Output doi trong CUNG mot khung hinh
    engine_.applyOutputs(state.outputs, [channels = project_.channels, canvases = state.canvases, scenes = state.scenes] {
        for (size_t i = 0; i < channels.size() && i < canvases.size(); ++i) channels[i]->size = canvases[i];
        for (const auto& [scene, size] : scenes) scene->size = size;
    });
    rebuildOutputRows();
    inspector_->rebuildOutputs();
    inspector_->refreshFromProject();
    if (mapping_) {                                        // cua so Mapping giu danh sach Output / co kenh cu: tao lai
        const bool visible = mapping_->isVisible();
        mapping_->hide();
        mapping_->setObjectName("mappingWindowOld");
        mapping_->deleteLater();
        mapping_ = nullptr;
        if (visible) showMapping();
    }
    ctlApi_->refreshProject();
    sup_.check();
    timeline_->refresh();
    refreshSlow();
    QStringList desc;
    for (const OutputConfig& o : state.outputs)
        desc << QString("%1 %2x%3%4").arg(o.name).arg(o.size.width()).arg(o.size.height()).arg(o.fullscreen ? " toàn màn hình" : "");
    qInfo().noquote() << "Thiết lập màn hình:" << desc.join("; ");
}

ScreenDialog* MainWindow::createScreenDialog() {
    auto* dlg = new ScreenDialog(project_, this);
    dlg->current = [this] { return screenSetup(); };
    dlg->apply = [this](const ScreenSetup& s, const std::vector<char>& all) { return applyScreenSetup(s, all); };
    dlg->testPattern = [this](bool on) {
        for (int i = 0; i < engine_.outputCount(); ++i) engine_.output(i)->testPattern = on;
    };
    dlg->reload();
    return dlg;
}

void MainWindow::showScreenSetup() {
    if (locked_) { showMessage("Đang khoá vận hành: không đổi màn hình / khung hình được"); return; }
    std::unique_ptr<ScreenDialog> dlg(createScreenDialog());
    dlg->exec();
}

void MainWindow::showNewProject() {
    if (locked_) { showMessage("Đang khoá vận hành: không tạo project mới được"); return; }
    NewProjectDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString path = QFileDialog::getSaveFileName(this, "Lưu project mới", "su-kien-moi.lumacast.json", "LumaCast project (*.json)");
    if (!path.isEmpty() && createProject(path, dlg.project())) launchProject(path);      // hoi xac nhan truoc khi dong project hien tai
}

bool MainWindow::createProject(const QString& path, const Project& project) {
    if (locked_) return false;
    if (QFileInfo(path).absoluteFilePath() == QFileInfo(project_.filePath).absoluteFilePath()) {
        showMessage("Chọn tên file khác với project đang mở");
        return false;
    }
    QString err;
    if (!lc::saveProject(project, path, &err)) { showMessage("Lỗi lưu: " + err); return false; }
    QFile::remove(path + ".autosave");                      // ghi de project cu cung ten: bo ban tu luu cu
    return true;
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
    newAct_ = file->addAction("Project mới…", this, [this] { showNewProject(); });
    openAct_ = file->addAction("Mở project...", QKeySequence::Open, this, [this] { openProject(); });
    recentMenu_ = file->addMenu("Gần đây");
    file->addSeparator();
    file->addAction("Lưu", QKeySequence::Save, this, [this] { saveProject(false); });
    file->addAction("Lưu thành...", QKeySequence::SaveAs, this, [this] { saveProject(true); });
    file->addSeparator();
    importAct_ = file->addAction("Nhập media...", this, [this] { chooseMediaFiles(); });
    addNdiAct_ = file->addAction("Thêm nguồn NDI...", this, [this] { showAddNdiSource(); });
    addCamAct_ = file->addAction("Thêm camera...", this, [this] { showAddCamera(); });
    addNdiOutAct_ = file->addAction("Thêm Output NDI...", this, [this] { showAddNdiOutput(); });
    if (!ndiCompiledIn()) {                                       // ban dich khong co NDI: an moi muc NDI
        addNdiAct_->setVisible(false);
        addNdiOutAct_->setVisible(false);
        scenes_->ndiButton()->setVisible(false);
    }
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
    connect(redoAct_, &QAction::triggered, this, [this] { if (!locked_) doRedo(); });
    for (auto sig : {&QUndoStack::canUndoChanged, &QUndoStack::canRedoChanged}) connect(&undo_, sig, this, [this] { updateUndoActions(); });
    for (auto sig : {&QUndoStack::undoTextChanged, &QUndoStack::redoTextChanged}) connect(&undo_, sig, this, [this] { updateUndoActions(); });
    edit->addAction(undoAct_);
    edit->addAction(redoAct_);
    updateUndoActions();

    auto* view = menuBar()->addMenu("&View");
    for (QDockWidget* d : findChildren<QDockWidget*>()) view->addAction(d->toggleViewAction());
    view->addSeparator();
    view->addAction("Mapping trực quan…", QKeySequence(Qt::CTRL | Qt::Key_M), this, [this] { showMapping(); });
    view->addAction("Multiview…", this, [this] { showMultiview(); })->setObjectName("multiviewAction");
    view->addSeparator();
    buildLayoutMenu(view);

    auto* settings = menuBar()->addMenu("&Cài đặt");
    settingsAct_ = settings->addAction("Cài đặt vận hành…", this, [this] { openSettings(); });
    screenAct_ = settings->addAction("Màn hình và khung hình…", this, [this] { showScreenSetup(); });
    setupInput(settings);                                 // phim tat tu dat + MIDI (MainWindowInput.cpp)

    auto* help = menuBar()->addMenu("&Help");
    help->addAction("Giới thiệu", this, [this] { QMessageBox::about(this, "LumaCast", aboutHtml()); });
}

// Multiview chi chon Preview / Channel dang xem: khong co duong nao tu day len Program (ke ca khi AUTO dang bat).
void MainWindow::showMultiview() {
    if (!multiview_) {
        multiview_ = new MultiviewWindow(project_, engine_, this);
        connect(multiview_, &MultiviewWindow::channelPicked, this, [this](int c) { chanCombo_->setCurrentIndex(c); refresh(); });
        connect(multiview_, &MultiviewWindow::scenePicked, this, [this](int idx) {
            auto ch = project_.channels[channelIdx()];
            engine_.post([ch, idx](double) { ch->setPreview(idx); });
        });
    }
    multiview_->show();
    multiview_->raise();
    multiview_->activateWindow();
    multiview_->refresh(channelIdx(), sceneNames_);
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
        if (auto src = stingerSource()) {
            s.stinger = src;
            s.cutPoint = cutSpin_->value();
            if (s.stinger->durationSeconds() > 0) s.duration = s.stinger->durationSeconds();
        }
    }
    return s;
}

// Video stinger dang chon (theo id nguon: danh sach nguon co the doi khi xoa Scene).
std::shared_ptr<Source> MainWindow::stingerSource() const {
    const QString id = stingerCombo_->currentData().toString();
    if (id.isEmpty()) return nullptr;
    for (const SourceDef& d : project_.sources)
        if (d.id == id) return d.source;
    return nullptr;
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
            if (project_.sources[i].type == "video") stingerCombo_->addItem(project_.sources[i].source ? project_.sources[i].source->name() : project_.sources[i].id, project_.sources[i].id);
        if (stingerCombo_->count() == 0) stingerCombo_->addItem("(chưa có video)", QString());
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
    const auto st = stinger ? stingerSource() : nullptr;
    durSpin_->setEnabled(!(st && st->durationSeconds() > 0));
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
    overlayPanel_->refreshLive();
    const int ci = channelIdx();
    const Channel& ch = *project_.channels[ci];
    const auto mf = engine_.monitor(ci);
    program_->setImage(mf.program);
    preview_->setImage(mf.preview);
    const int pg = ch.program, pv = ch.preview;
    program_->setSceneName(sceneName(ci, pg) + (ch.inTransition() ? "  (đang chuyển)" : ""));
    preview_->setSceneName(sceneName(ci, pv));
    program_->setOnAir(ch.ftbAmount < 1.0f);
    scenes_->setMarks(pg, pv);
    scenes_->refresh();
    nowShowing_->refresh(ch.overlays.frames(engine_.now()));
    refreshPreviewEditing();
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
    if (multiview_ && multiview_->isVisible()) multiview_->refresh(ci, sceneNames_);
}

void MainWindow::refreshSlow() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double dt = lastSlowMs_ ? (now - lastSlowMs_) / 1000.0 : 0;
    lastSlowMs_ = now;
    int ready = 0;
    const std::vector<OutputConfig> configs = screenSetup().outputs;
    for (int i = 0; i < engine_.outputCount() && i < int(outRows_.size()); ++i) {
        OutputSink* sink = engine_.output(i);
        OutRow& r = outRows_[i];
        const OutputHealth hl = sup_.health(i);
        const bool exposed = hl == OutputHealth::Ok;
        const uint64_t pr = sink->presented;
        const double fps = dt > 0 ? double(pr - r.lastPresented) / dt : 0;
        r.lastPresented = pr;
        if (exposed) ++ready;
        auto* ndiSink = dynamic_cast<NdiOutputSink*>(sink);
        const char* col = !exposed ? colors::kProgram : sink->black ? "#8a90a2" : sink->freeze ? colors::kWarn : colors::kPreview;
        if (ndiSink && !ndiSink->sending()) col = colors::kWarn;
        r.dot->setStyleSheet(QString("color:%1;font-size:16px").arg(col));
        const OutputConfig& c = sink->config();
        QString extra;
        if (ndiSink) extra = " · NDI: " + ndiSink->statusText().toHtmlEscaped() + (sink->testPattern ? " · MẪU THỬ" : sink->freeze ? " · DỪNG HÌNH" : "");
        else if (!exposed) extra = " · " + sup_.text(i);
        else if (sink->testPattern) extra = " · MẪU THỬ";
        else if (sink->freeze) extra = " · DỪNG HÌNH";
        const QSize signal = outputSignalSize(c);
        if (!c.area.isEmpty()) extra.prepend(QString(" · LED %1x%2").arg(c.area.width()).arg(c.area.height()));
        r.info->setText(QString("<b>%1</b><br><small>%2x%3 · kênh %4 · %5 fps%6</small>")
                            .arg(c.name.toHtmlEscaped()).arg(signal.width()).arg(signal.height()).arg(c.channel + 1)
                            .arg(fps, 0, 'f', 0).arg(extra));
        QStringList warnings;
        if (c.channel >= 0 && c.channel < int(project_.channels.size())) {
            const Channel& channel = *project_.channels[size_t(c.channel)];
            warnings = outputWarnings(configs, i, channel.size, signal);
            const QString scenes = sceneSizeWarning(channel.scenes, channel.size);
            if (!scenes.isEmpty()) warnings << scenes;
        }
        r.warn->setVisible(!warnings.isEmpty());
        r.warn->setToolTip(warnings.join("\n"));
        QSignalBlocker bb(r.black), bf(r.freeze), bp(r.pattern);
        r.black->setChecked(sink->black);
        r.freeze->setChecked(sink->freeze);
        r.pattern->setChecked(sink->testPattern);
        auto* win = dynamic_cast<OutputWindow*>(sink);
        r.reopen->setVisible(win && !win->isVisible() && win->canShow());
    }
    inspector_->updateEffectNote();
    // Output bi dong trong khi khung Outputs dang an (vd. bo cuc "Chuan bi"): van co cho de mo lai
    bool closed = false;
    for (int i = 0; i < engine_.outputCount(); ++i)
        if (auto* w = dynamic_cast<OutputWindow*>(engine_.output(i))) closed = closed || (!w->isVisible() && w->canShow());
    auto* outDock = findChild<QDockWidget*>("dockOutputs");
    statusReopen_->setVisible(closed && outDock && (outDock->isHidden() || outDock->visibleRegion().isEmpty()));
    updateLayoutLabel();
    chrome_->refresh();                      // luoi an toan: nguoi dung vua keo khung bang chuot
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
}

void MainWindow::rebuildSceneList() {
    const int ci = channelIdx();
    const Channel& ch = *project_.channels[size_t(ci)];
    scenes_->setScenes(ci, ch.scenes, sceneNames_[size_t(ci)]);
    scenes_->setMarks(ch.program, ch.preview);
}

void MainWindow::onProjectChanged() {
    sceneNames_.clear();
    for (const auto& c : project_.channels) {
        std::vector<QString> names;
        for (const auto& s : c->scenes) names.push_back(s->name);
        sceneNames_.push_back(std::move(names));
    }
    rebuildSceneList();
    inspector_->refreshFromProject();
    rebuildTransitionCombo();                              // danh sach nguon Stinger co the doi
    timeline_->refresh();
    playlist_->refresh();
    ctlApi_->refreshProject();
}

// ---------------- media ----------------

bool MainWindow::addMediaFile(const QString& path) { return importMedia(path) != nullptr; }

void MainWindow::chooseMediaFiles() {
    for (const QString& f : QFileDialog::getOpenFileNames(this, "Nhập media", QString(), kMediaFilter)) addMediaFile(f);
}

// Scene "cua rieng" mot nguon: dung mot Layer va nguon con dung duoc.
std::shared_ptr<Scene> MainWindow::homeScene(const std::function<bool(const SourceDef&)>& same) const {
    for (const auto& s : project_.scenes) {
        if (s->layers.size() != 1) continue;
        const SourceDef* d = project_.findSource(s->layers[0].source.get());
        if (d && d->source && d->source->error().isEmpty() && same(*d)) return s;
    }
    return nullptr;
}

void MainWindow::announceExisting(const std::shared_ptr<Scene>& s) {
    scenes_->reveal(s.get());
    showMessage("Đã có trong danh sách Scenes: " + s->name);
}

static std::shared_ptr<MainWindow::ImportEntry> importEntry(const SourceDef& d, std::shared_ptr<Source> src, QSize canvas) {
    auto e = std::make_shared<MainWindow::ImportEntry>();
    e->def = d;
    e->def.source = src;
    e->scene = std::make_shared<Scene>();
    e->scene->name = src->name();
    e->scene->size = canvas;
    e->scene->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Fit, true});
    return e;
}

std::shared_ptr<Scene> MainWindow::importMedia(const QString& path, bool* created) {
    if (created) *created = false;
    if (locked_) { showMessage("Đang khoá vận hành: không nhập media được"); return nullptr; }
    SourceDef d;
    d.path = QFileInfo(path).absoluteFilePath();
    d.type = kImageExt.contains(QFileInfo(path).suffix().toLower()) ? "image" : "video";
    const QString file = d.path;
    auto same = [&file](const SourceDef& o) { return (o.type == "video" || o.type == "image") && sameFile(o.path, file); };
    if (auto s = homeScene(same)) { announceExisting(s); return s; }
    std::shared_ptr<Source> src;
    bool addsSource = true;
    for (const SourceDef& o : project_.sources)                    // nguon con (Scene ghep dang dung) nhung Scene rieng da bi xoa
        if (same(o) && o.source && o.source->error().isEmpty()) { d = o; src = o.source; addsSource = false; break; }
    if (!src) {
        QString err;
        src = createSource(d, &err);
        if (!src) { showMessage("Không nhập được: " + err); return nullptr; }
    }
    auto e = importEntry(d, src, project_.channels[0]->size);
    e->addsSource = addsSource;
    auto scene = e->scene;
    pushImport(e, "Nhập media: " + src->name());
    if (created) *created = true;
    scenes_->reveal(scene.get());
    return scene;
}

// Keo tha file = lenh "Nhap media" cho tung file (cung duong addMediaFile: cung khoa van hanh, hoan tac duoc, khong dung Program/Preview).
bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
    const QEvent::Type t = ev->type();
    // Enter trong o dang GO (so, chu) chi xac nhan gia tri, KHONG kich hoat phim tat CUT. Loc o muc ung dung nen moi o nhap,
    // ke ca o them sau nay, deu theo mot quy tac. Ngoai o nhap, Enter van la CUT.
    if (t == QEvent::ShortcutOverride) {
        const int key = static_cast<QKeyEvent*>(ev)->key();
        if ((key == Qt::Key_Return || key == Qt::Key_Enter) && (qobject_cast<QAbstractSpinBox*>(obj) || qobject_cast<QLineEdit*>(obj))) {
            ev->accept();
            return true;
        }
    }
    if (t != QEvent::DragEnter && t != QEvent::DragMove && t != QEvent::Drop) return QMainWindow::eventFilter(obj, ev);
    // chi cua so chinh va dock tach roi cua no (khong gom hop thoai)
    auto* w = qobject_cast<QWidget*>(obj);
    QWidget* top = w ? w->window() : nullptr;
    // dock tach roi, hoac nhom dock tach roi (keo ca nhom), deu co cha la cua so chinh
    if (!top || !(top == this || (top->parentWidget() == this && (qobject_cast<QDockWidget*>(top) || top->inherits("QDockWidgetGroupWindow")))))
        return QMainWindow::eventFilter(obj, ev);
    auto* de = static_cast<QDropEvent*>(ev);
    const QStringList files = draggedFiles(de->mimeData());
    if (files.isEmpty()) return QMainWindow::eventFilter(obj, ev);
    // Chi nhan SAO CHEP: neu bao "di chuyen", Explorer co the xoa file goc.
    if (QApplication::activeModalWidget() || !(de->possibleActions() & Qt::CopyAction)) {
        de->ignore();
    } else if (locked_) {
        showMessage("Đang khoá vận hành: không nhập media được");
        de->ignore();
    } else {
        de->setDropAction(Qt::CopyAction);
        de->accept();
        // Qt giao su kien tha cho widget nhan tha gan nhat (o day thuong la chinh dock), nen xet theo dock chua khung Playlist.
        QWidget* plDock = playlist_->parentWidget();
        if (t == QEvent::Drop) importDroppedFiles(files, w == plDock || plDock->isAncestorOf(w));
    }
    return true;
}

// toPlaylist: tha vao khung Playlist -> nhap roi them luon vao cuoi playlist.
void MainWindow::importDroppedFiles(const QStringList& files, bool toPlaylist) {
    int added = 0, existing = 0;
    QStringList skipped;
    std::vector<std::shared_ptr<Scene>> imported;
    for (const QString& f : files) {
        const QFileInfo fi(f);
        const QString ext = fi.suffix().toLower();
        std::shared_ptr<Scene> scene;
        bool created = false;
        if (fi.isFile() && (kImageExt.contains(ext) || kVideoExt.contains(ext))) scene = importMedia(f, &created);
        if (scene) { created ? ++added : ++existing; imported.push_back(scene); }
        else skipped << (fi.fileName().isEmpty() ? f : fi.fileName());
    }
    if (toPlaylist) playlist_->addScenes(imported);
    QString msg;
    if (skipped.isEmpty()) {
        msg = QString("Đã nhập %1 file bằng kéo thả").arg(added);
    } else {
        qWarning().noquote() << "Kéo thả: bỏ qua file không hỗ trợ hoặc không đọc được:" << skipped.join(", ");
        QString names = skipped.mid(0, 3).join(", ");
        if (skipped.size() > 3) names += ", …";
        msg = QString("Đã nhập %1 file; bỏ qua %2 file không hỗ trợ hoặc không đọc được: %3").arg(added).arg(skipped.size()).arg(names);
    }
    if (existing) msg += QString(" · %1 file đã có trong danh sách Scenes").arg(existing);
    showMessage(msg);
}

bool MainWindow::addLiveSource(const SourceDef& def, const QString& what) {
    QString err;
    auto src = createSource(def, &err);
    if (!src) { showMessage("Không thêm được " + what + ": " + err); return false; }
    auto e = importEntry(def, src, project_.channels[0]->size);
    auto scene = e->scene;
    pushImport(e, "Thêm " + what + ": " + src->name());
    scenes_->reveal(scene.get());
    return true;
}

bool MainWindow::addNdiSource(const QString& name, const QString& url, const QString& label) {
    if (locked_) { showMessage("Đang khoá vận hành: không thêm nguồn được"); return false; }
    if (name.trimmed().isEmpty()) { showMessage("Chưa chọn nguồn NDI"); return false; }
    SourceDef d;
    d.type = "ndi";
    d.device = name.trimmed();
    d.url = url;
    d.label = label;
    if (auto s = homeScene([&](const SourceDef& o) { return o.type == "ndi" && o.device == d.device; })) { announceExisting(s); return true; }
    return addLiveSource(d, "nguồn NDI");
}

bool MainWindow::addCameraSource(const QString& deviceId, const QString& deviceName, CameraFormat fmt, const QString& label) {
    if (locked_) { showMessage("Đang khoá vận hành: không thêm nguồn được"); return false; }
    SourceDef d;
    d.type = "camera";
    d.device = deviceId;
    d.deviceName = deviceName;
    d.size = QSize(fmt.w, fmt.h);
    d.fps = fmt.fps;
    d.label = label;
    // mot thiet bi chi mo duoc mot lan: dung lai Scene da co
    if (!deviceId.isEmpty())
        if (auto s = homeScene([&](const SourceDef& o) { return o.type == "camera" && o.device == d.device; })) { announceExisting(s); return true; }
    return addLiveSource(d, "camera");
}

bool MainWindow::addNdiOutput(const QString& name, int channel, QSize size, bool audio) {
    if (locked_) { showMessage("Đang khoá vận hành: không thêm Output được"); return false; }
    if (channel < 0 || channel >= int(project_.channels.size()) || size.width() < 16 || size.height() < 16 || size.width() > 8192 || size.height() > 8192) {
        showMessage("Output NDI: kênh hoặc kích thước không hợp lệ");
        return false;
    }
    OutputConfig c;
    c.kind = OutputKind::Ndi;
    c.name = name.isEmpty() ? QString("NDI %1").arg(engine_.outputCount() + 1) : name;
    c.ndiName = name;
    c.channel = channel;
    c.size = size;
    c.scale = ScaleMode::Stretch;
    c.ndiAudio = audio;
    const Entitlements& ent = Entitlements::current();
    ScreenSetup setup = screenSetup();
    setup.outputs.push_back(c);
    if (!ent.ndi || (ent.maxOutputs > 0 && int(setup.outputs.size()) > ent.maxOutputs) ||
        !applyScreenSetup(setup, {}, "Thêm Output NDI: " + c.name)) { showMessage("Không thêm được Output NDI"); return false; }
    qInfo().noquote() << QString("Thêm Output NDI '%1' (kênh %2, %3x%4)").arg(c.name).arg(channel + 1).arg(size.width()).arg(size.height());
    showMessage("Đã thêm Output NDI: " + c.name);
    return true;
}

void MainWindow::showAddNdiSource() {
    if (!Entitlements::current().ndi) { showMessage("NDI không có trong gói giấy phép hiện tại"); return; }
    if (locked_) { showMessage("Đang khoá vận hành: không thêm nguồn được"); return; }
    NdiSourceDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) addNdiSource(dlg.name(), dlg.url(), dlg.label());
}

void MainWindow::showAddCamera() {
    if (!Entitlements::current().camera) { showMessage("Camera/capture không có trong gói giấy phép hiện tại"); return; }
    if (locked_) { showMessage("Đang khoá vận hành: không thêm nguồn được"); return; }
    CameraSourceDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted && !dlg.deviceId().isEmpty()) addCameraSource(dlg.deviceId(), dlg.deviceName(), dlg.format(), dlg.label());
}

void MainWindow::showAddNdiOutput() {
    if (!Entitlements::current().ndi) { showMessage("NDI không có trong gói giấy phép hiện tại"); return; }
    if (locked_) { showMessage("Đang khoá vận hành: không thêm Output được"); return; }
    QStringList names;
    for (const auto& c : project_.channels) names << c->name;
    NdiOutputDialog dlg(names, this);
    if (dlg.exec() == QDialog::Accepted) addNdiOutput(dlg.name(), dlg.channel(), dlg.size(), dlg.audio());
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

void MainWindow::doRedo() {
    // Lam lai lenh Xoa: nhu lenh Xoa, khong go Scene dang On Air.
    if (undo_.canRedo())
        if (auto* c = dynamic_cast<const RemoveSceneCmd*>(undo_.command(undo_.index())); c && sceneOnAir(c->scene())) {
            showMessage("Không làm lại được: Scene này đang ở PROGRAM/đang chuyển cảnh");
            return;
        }
    undo_.redo();
}

bool MainWindow::sceneOnAir(const Scene* s) const {
    for (const auto& c : project_.channels) {
        const int pg = c->program, pv = c->preview;
        const auto& sc = c->scenes;
        if (pg >= 0 && pg < int(sc.size()) && sc[size_t(pg)].get() == s) return true;
        if (c->inTransition() && !c->autoTransition() && pv >= 0 && pv < int(sc.size()) && sc[size_t(pv)].get() == s) return true;
        if (c->isIncoming(s)) return true;
    }
    return false;
}

bool MainWindow::renameScene(const std::shared_ptr<Scene>& s, const QString& name) {
    if (locked_) { showMessage("Đang khoá vận hành: không đổi tên Scene được"); return false; }
    const QString n = name.trimmed();
    if (!s || n.isEmpty() || n == s->name || std::find(project_.scenes.begin(), project_.scenes.end(), s) == project_.scenes.end()) return false;
    undo_.push(new RenameSceneCmd(this, s, n));
    return true;
}

void MainWindow::applySceneName(const std::shared_ptr<Scene>& s, const QString& name) {
    engine_.postAndWait([s, name](double) { s->name = name; });          // render thread cung doc ten (thong bao playlist)
    onProjectChanged();
}

bool MainWindow::deleteScene(const std::shared_ptr<Scene>& s) {
    if (locked_) { showMessage("Đang khoá vận hành: không xoá Scene được"); return false; }
    if (!s || std::find(project_.scenes.begin(), project_.scenes.end(), s) == project_.scenes.end()) return false;
    if (sceneOnAir(s.get())) {
        showMessage("Không xoá được: Scene đang ở PROGRAM/đang chuyển cảnh. Đưa Scene khác lên Program rồi xoá.");
        return false;
    }
    auto r = std::make_shared<SceneRemoval>();
    r->scene = s;
    const QString name = s->name;
    undo_.push(new RemoveSceneCmd(this, r, "Xoá Scene: " + name));
    if (std::find(project_.scenes.begin(), project_.scenes.end(), s) != project_.scenes.end()) return false;
    showMessage("Đã xoá Scene: " + name + " (Ctrl+Z để lấy lại)");
    return true;
}

// Go Scene khoi project + moi Channel + playlist; nguon khong con Scene nao dung thi go theo (video duoc tam dung, giu lai cho Hoan tac).
bool MainWindow::applyRemoval(const std::shared_ptr<SceneRemoval>& r) {
    auto done = std::make_shared<std::atomic<bool>>(false);
    engine_.postAndWait([this, r, done](double) {
        const auto sit = std::find(project_.scenes.begin(), project_.scenes.end(), r->scene);
        if (sit == project_.scenes.end() || sceneOnAir(r->scene.get())) return;
        r->projectIndex = int(sit - project_.scenes.begin());
        r->channelIndex.assign(project_.channels.size(), -1);
        r->items.assign(project_.channels.size(), {});
        r->sources.clear();
        r->pausedBefore.clear();
        for (size_t c = 0; c < project_.channels.size(); ++c) {
            Channel& ch = *project_.channels[c];
            auto& items = ch.playlist.data.items;
            for (int i = 0; i < int(items.size()); ++i)
                if (items[size_t(i)].scene == r->scene) r->items[c].push_back({i, items[size_t(i)]});
            items.erase(std::remove_if(items.begin(), items.end(), [&](const PlaylistItem& it) { return it.scene == r->scene; }), items.end());
            const auto it = std::find(ch.scenes.begin(), ch.scenes.end(), r->scene);
            if (it == ch.scenes.end()) continue;
            const int k = int(it - ch.scenes.begin());
            r->channelIndex[c] = k;
            ch.scenes.erase(it);
            const int pv = ch.preview, pg = ch.program;
            if (pv == k) ch.preview = -1;
            else if (pv > k) ch.preview = pv - 1;
            if (pg > k) ch.program = pg - 1;
        }
        project_.scenes.erase(sit);
        auto usedElsewhere = [&](const Source* src) {
            for (const auto& sc : project_.scenes)
                for (const Layer& l : sc->layers) if (l.source.get() == src) return true;
            return false;
        };
        for (int i = 0; i < int(project_.sources.size()); ++i) {
            const Source* src = project_.sources[size_t(i)].source.get();
            bool mine = false;
            for (const Layer& l : r->scene->layers) mine = mine || l.source.get() == src;
            if (mine && !usedElsewhere(src)) {
                r->sources.push_back({i, project_.sources[size_t(i)]});
                r->pausedBefore.push_back(src && src->paused());
            }
        }
        for (auto it = r->sources.rbegin(); it != r->sources.rend(); ++it) {
            project_.sources.erase(project_.sources.begin() + it->first);
            if (it->second.source) it->second.source->setPaused(true);       // khong ai xem: thoi giai ma
        }
        *done = true;
    });
    if (!*done) return false;
    inspector_->sceneRemoved(r->projectIndex);
    timeline_->sceneRemoved(r->projectIndex);
    onProjectChanged();
    return true;
}

// Lenh live (CUT...) xen vao giua kiem tra UI va render thread: bo luon buoc nay khoi ngan xep Hoan tac.
void MainWindow::removalRefused(const QUndoCommand* cmd) {
    QMetaObject::invokeMethod(this, [this, cmd] {
        showMessage("Xoá Scene bị huỷ: Scene vừa được đưa lên PROGRAM");
        if (undo_.index() > 0 && undo_.command(undo_.index() - 1) == cmd) {
            const_cast<QUndoCommand*>(cmd)->setObsolete(true);
            undo_.undo();
        }
    }, Qt::QueuedConnection);
}

void MainWindow::undoRemoval(const std::shared_ptr<SceneRemoval>& r) {
    if (r->projectIndex < 0) return;
    engine_.postAndWait([this, r](double) {
        if (std::find(project_.scenes.begin(), project_.scenes.end(), r->scene) != project_.scenes.end()) return;
        for (size_t i = 0; i < r->sources.size(); ++i) {
            const int at = std::min(r->sources[i].first, int(project_.sources.size()));
            project_.sources.insert(project_.sources.begin() + at, r->sources[i].second);
            if (r->sources[i].second.source) r->sources[i].second.source->setPaused(r->pausedBefore[i]);
        }
        project_.scenes.insert(project_.scenes.begin() + std::min(r->projectIndex, int(project_.scenes.size())), r->scene);
        for (size_t c = 0; c < project_.channels.size() && c < r->channelIndex.size(); ++c) {
            Channel& ch = *project_.channels[c];
            if (const int k = r->channelIndex[c]; k >= 0) {
                const int at = std::min(k, int(ch.scenes.size()));
                ch.scenes.insert(ch.scenes.begin() + at, r->scene);
                const int pv = ch.preview, pg = ch.program;
                if (pv >= at) ch.preview = pv + 1;
                if (pg >= at) ch.program = pg + 1;
            }
            auto& items = ch.playlist.data.items;
            for (const auto& [pos, item] : r->items[c]) items.insert(items.begin() + std::min(pos, int(items.size())), item);
        }
    });
    inspector_->sceneInserted(r->projectIndex);
    timeline_->sceneInserted(r->projectIndex);
    onProjectChanged();
}

bool MainWindow::importInUse(const ImportEntry& e) const {
    for (const auto& c : project_.channels) {
        const int pg = c->program, pv = c->preview;
        const auto& sc = c->scenes;
        if (pg >= 0 && pg < int(sc.size()) && sc[pg] == e.scene) return true;
        if (c->inTransition() && !c->autoTransition() && pv >= 0 && pv < int(sc.size()) && sc[pv] == e.scene) return true;
        if (c->isIncoming(e.scene.get())) return true;
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
            if (c->program == k || (c->inTransition() && !c->autoTransition() && c->preview == k) || c->isIncoming(e->scene.get())) { *busy = true; return; }
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
    saveLayout(st);
    e->accept();
    // cua so output khong tu thoat; dung exit() vi quit() se thu dong chung truoc (chung chi an di nen quit() bi huy)
    QMetaObject::invokeMethod(qApp, [] { QCoreApplication::exit(0); }, Qt::QueuedConnection);
}

} // namespace lc
