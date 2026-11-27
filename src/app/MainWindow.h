#pragma once
#include "Shortcuts.h"
#include "core/ScreenSetup.h"
#include "control/ControlService.h"
#include "control/Midi.h"
#include "render/Engine.h"
#include "render/OutputSupervisor.h"
#include "render/PluginManager.h"
#include "core/Transition.h"
#include "live/Camera.h"
#include <QMainWindow>
#include <QMap>
#include <QTimer>
#include <QUndoStack>
#include <functional>

class QAction;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QMenu;
class QPushButton;
class QSlider;
class QToolButton;
class QUndoCommand;
class QVBoxLayout;


namespace lc {

class PlaylistPanel;
class AudioPanel;
class InspectorPanel;
class LockButton;
class MappingWindow;
class TimelineWidget;
class MonitorWidget;
class OverlayPanel;
class MultiviewWindow;
class ScenePanel;
class ScreenDialog;

// Cua so chinh kieu "live production": Preview/Program, thanh chuyen, danh sach Scenes (ScenePanel),
// Outputs (Black/Freeze/trang thai), Inspector. Moi thao tac di qua RenderEngine::post().
class MainWindow : public QMainWindow {
public:
    // Mot lan nhap: them nguon (tuy chon) + Scene vao project va moi Channel. Dung chung cho Hoan tac/Lam lai (PRJ-07).
    struct ImportEntry {
        SourceDef def;
        bool addsSource = true;               // false: chi tao Scene tu nguon da co (Scene cu cua no da bi xoa)
        std::shared_ptr<Scene> scene;
    };
    // Mot lan xoa Scene: du de dat lai dung cho (project, moi Channel, nguon khong con ai dung, muc playlist).
    struct SceneRemoval {
        std::shared_ptr<Scene> scene;
        int projectIndex = -1;
        std::vector<int> channelIndex;                                      // theo Channel; -1 = kenh khong co Scene nay
        std::vector<std::pair<int, SourceDef>> sources;                     // nguon bi go kem (vi tri cu)
        std::vector<bool> pausedBefore;                                     // theo `sources`
        std::vector<std::vector<std::pair<int, PlaylistItem>>> items;       // theo Channel: muc playlist bi go (vi tri cu)
    };
    bool applyRemoval(const std::shared_ptr<SceneRemoval>& r);   // false: Scene dang On Air, khong go
    void undoRemoval(const std::shared_ptr<SceneRemoval>& r);
    void removalRefused(const QUndoCommand* cmd);
    bool sceneOnAir(const Scene* s) const;
    void applySceneName(const std::shared_ptr<Scene>& s, const QString& name);
    // Sua danh sach Scenes (co Hoan tac, bi Khoa van hanh chan). Xoa bi tu choi khi Scene dang o Program/dang chuyen vao.
    bool deleteScene(const std::shared_ptr<Scene>& s);
    bool renameScene(const std::shared_ptr<Scene>& s, const QString& name);
    ScenePanel* scenePanel() const { return scenes_; }
    // ---- Thiet lap man hinh (UI-08) ----
    struct ScreenState {                                                    // du de ap dung / hoan tac mot lan
        std::vector<OutputConfig> outputs;
        std::vector<QSize> canvases;
        std::vector<std::pair<std::shared_ptr<Scene>, QSize>> scenes;
    };
    ScreenSetup screenSetup() const;                                        // trang thai dang chay
    // Doi Output / khung hinh (mot buoc Hoan tac, bi Khoa van hanh chan, khong doi Preview/Program). allScenes[kenh] = dat MOI
    // Scene cua kenh theo khung hinh moi (khong thi chi Scene dang bang khung hinh cu).
    bool applyScreenSetup(const ScreenSetup& after, const std::vector<char>& allScenes = {}, const QString& text = "Thiết lập màn hình");
    void setScreenState(const ScreenState& state);                          // goi boi Hoan tac/Lam lai
    ScreenDialog* createScreenDialog();                                     // hop thoai da noi voi cua so (nguoi goi huy)
    void showScreenSetup();
    void showNewProject();
    bool createProject(const QString& path, const Project& project);       // ghi file project moi; false neu loi
    // Goi boi lenh Hoan tac/Lam lai; ca hai chay tren render thread qua post(), co ban sao UI cap nhat sau.
    void applyImport(const std::shared_ptr<ImportEntry>& e);
    void unapplyImport(const std::shared_ptr<ImportEntry>& e, const QUndoCommand* cmd);
    bool importInUse(const ImportEntry& e) const;           // Scene dang o Program/dang chuyen cua kenh nao (UI thread)

    MainWindow(Project& project, RenderEngine& engine, QStringList pluginDirs = PluginManager::defaultDirs());
    PluginManager& plugins() { return pm_; }
    TransitionSpec currentSpec() const;          // hieu ung chuyen dang chon tren thanh chuyen

    void setAskOnClose(bool ask) { askOnClose_ = ask; }
    void saveProject(bool saveAs);
    void autosaveNow();
    bool addMediaFile(const QString& path);        // them nguon + Scene tu file (file da co Scene: khong tao them); false neu loi
    // M5b: nguon truc tiep (them nguon + Scene giong nhap media, hoan tac duoc) va Output NDI. Nguon luon them duoc ke ca khi chua co NDI/camera
    // (trang thai "khong kha dung"). Tra false khi bi khoa van hanh / du lieu sai.
    bool addNdiSource(const QString& name, const QString& url = {}, const QString& label = {});
    bool addCameraSource(const QString& deviceId, const QString& deviceName, CameraFormat fmt, const QString& label = {});
    bool addNdiOutput(const QString& name, int channel, QSize size, bool audio);   // khong hoan tac duoc; false neu loi
    void showAddNdiSource();
    void showAddCamera();
    void showAddNdiOutput();
    QString autosavePath() const;
    InspectorPanel* inspector() const { return inspector_; }
    AudioPanel* audioPanel() const { return audioPanel_; }
    OverlayPanel* overlayPanel() const { return overlayPanel_; }
    TimelineWidget* timeline() const { return timeline_; }
    void showMapping();                                            // cua so Mapping truc quan (tao khi can)
    MappingWindow* mappingWindow() const { return mapping_; }
    QUndoStack* undoStack() { return &undo_; }
    ControlService& control() { return *ctl_; }
    ControlApi& controlApi() { return *ctlApi_; }
    PlaylistPanel* playlistPanel() const { return playlist_; }
    void showStartupNotice(const QString& msg);    // thong bao khong chan (thanh trang thai) khi tu mo project that bai/thieu file
    void openSettings();
    // Phim tat tu dat (UI-07) va MIDI (CTL-01): xem MainWindowInput.cpp
    void openInputDialog();
    MidiService& midi() { return *midi_; }
    const KeyMap& keyMap() const { return binder_->map(); }
    void applyKeys(const KeyMap& keys);            // doi phim tat dang chay (khong luu)
    void showMultiview();                          // UI-06: cua so luoi Preview/Program/Scene (tao khi mo lan dau)
    // UI-07: Khoa van hanh (theo phien, khong luu vao project). Khoa = vo hieu moi dieu khien SUA, giu dieu khien live.
    void setLocked(bool on);
    bool isLocked() const { return locked_; }                           // hop thoai Cai dat van hanh (dieu khien ngoai...)

protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;   // keo tha file anh/video vao cua so

private:
    struct OutRow { QLabel* dot; QLabel* info; QToolButton* black; QToolButton* freeze; QToolButton* pattern; uint64_t lastPresented = 0;
                    QToolButton* reopen = nullptr;          // hien khi cua so Output bi dong
                    QToolButton* warn = nullptr;            // hien khi hinh bi co gian / cat / phong to
                    QWidget* box = nullptr; };

    void buildUi();
    void buildMenus();
    void buildDocks();
    void rebuildTransitionCombo();
    void rebuildPluginList();
    void onTransitionChanged();
    void editTransitionParams();
    void refresh();
    void refreshSlow();
    void onProjectChanged();
    void rebuildSceneList();
    void addOutputRow(int i);
    void rebuildOutputRows();
    std::shared_ptr<Source> stingerSource() const;
    int channelIdx() const;
    QString sceneName(int ch, int idx) const;
    void take();
    void cut();
    void setBlackAll(bool on);
    void toggleFtb();
    void openProject();
    void launchProject(const QString& path);
    void addRecent(const QString& path);
    void rebuildRecentMenu();
    void updateTitle();
    void showLoadWarnings();
    void showMessage(const QString& msg);

    Project& project_;
    RenderEngine& engine_;
    OutputSupervisor sup_;             // theo doi mat man hinh / sai man hinh
    PluginManager pm_;                 // plug-in shader (Effect/Transition)
    QUndoStack undo_;                  // phai khai bao truoc inspector_
    InspectorPanel* inspector_ = nullptr;
    AudioPanel* audioPanel_ = nullptr;
    OverlayPanel* overlayPanel_ = nullptr;
    TimelineWidget* timeline_ = nullptr;
    MappingWindow* mapping_ = nullptr;
    MonitorWidget* preview_ = nullptr;
    MonitorWidget* program_ = nullptr;
    QComboBox* chanCombo_ = nullptr;
    QComboBox* transCombo_ = nullptr;
    QDoubleSpinBox* durSpin_ = nullptr;
    QComboBox* easingCombo_ = nullptr;
    QPushButton* paramsBtn_ = nullptr;
    QComboBox* stingerCombo_ = nullptr;
    QDoubleSpinBox* cutSpin_ = nullptr;
    QListWidget* pluginList_ = nullptr;
    QMap<QString, QVariantMap> transParams_;      // tham so da chinh cua tung transition plug-in
    QPushButton* blackAll_ = nullptr;
    QPushButton* ftb_ = nullptr;
    QPushButton* autoBtn_ = nullptr;
    QSlider* tbar_ = nullptr;
    qint64 tbarMovedMs_ = 0;
    ScenePanel* scenes_ = nullptr;
    QListWidget* sceneList_ = nullptr;             // scenes_->list()
    QLabel* statusLabel_ = nullptr;
    QMenu* recentMenu_ = nullptr;
    std::vector<OutRow> outRows_;
    QVBoxLayout* outRowsLayout_ = nullptr;
    QAction *addNdiAct_ = nullptr, *addCamAct_ = nullptr, *addNdiOutAct_ = nullptr;
    std::vector<std::vector<QString>> sceneNames_;     // theo kenh; snapshot de timer khong cham du lieu render thread
    QTimer fastTimer_, slowTimer_, autosaveTimer_;
    QByteArray defaultState_;
    int lastProgram_ = -2, lastPreview_ = -2, lastChannel_ = -1;
    qint64 lastSlowMs_ = 0;
    bool lastStalled_ = false;
    bool askOnClose_ = true;
    bool locked_ = false;
    ControlApi* ctlApi_ = nullptr;
    ControlService* ctl_ = nullptr;
    QLabel* ctlLabel_ = nullptr;
    PlaylistPanel* playlist_ = nullptr;
    QAction* settingsAct_ = nullptr;
    QAction *screenAct_ = nullptr, *newAct_ = nullptr;
    QToolButton* screenBtn_ = nullptr;
    QAction *importAct_ = nullptr, *openAct_ = nullptr, *undoAct_ = nullptr, *redoAct_ = nullptr;
    QWidget* pluginPanel_ = nullptr;
    LockButton* lockBtn_ = nullptr;
    QLabel* lockLabel_ = nullptr;
    void updateUndoActions();
    void pushImport(std::shared_ptr<ImportEntry> e, const QString& text);
    void doUndo();
    void doRedo();
    void chooseMediaFiles();
    void importDroppedFiles(const QStringList& files, bool toPlaylist);
    // Mot file = mot Scene: file da co Scene thi tra ve Scene do (created = false). nullptr = loi/bi khoa.
    std::shared_ptr<Scene> importMedia(const QString& path, bool* created = nullptr);
    std::shared_ptr<Scene> homeScene(const std::function<bool(const SourceDef&)>& same) const;
    bool addLiveSource(const SourceDef& def, const QString& what);
    void announceExisting(const std::shared_ptr<Scene>& s);
    void updateControlStatus();
    void setupInput(QMenu* settingsMenu);
    void runKeyAction(const LiveAction& a);
    MidiService* midi_ = nullptr;
    ShortcutBinder* binder_ = nullptr;
    QAction* inputAct_ = nullptr;
    MultiviewWindow* multiview_ = nullptr;
};

} // namespace lc
