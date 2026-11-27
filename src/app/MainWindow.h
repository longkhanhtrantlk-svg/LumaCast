#pragma once
#include "Shortcuts.h"
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

// Cua so chinh kieu "live production": Preview/Program, thanh chuyen, Scene list, Media Pool,
// Outputs (Black/Freeze/trang thai), Inspector. Moi thao tac di qua RenderEngine::post().
class MainWindow : public QMainWindow {
public:
    // Mot lan nhap: them nguon (tuy chon) + Scene vao project va moi Channel. Dung chung cho Hoan tac/Lam lai (PRJ-07).
    struct ImportEntry {
        SourceDef def;
        bool addsSource = true;               // false: chi tao Scene tu nguon da co (Media Pool)
        std::shared_ptr<Scene> scene;
    };
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
    bool addMediaFile(const QString& path);        // them nguon + Scene tu file; false neu loi
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
                    QToolButton* reopen = nullptr; };       // hien khi cua so Output bi dong

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
    void rebuildMediaList();
    void addOutputRow(int i);
    void updateLiveStatus();
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
    QListWidget* sceneList_ = nullptr;
    QListWidget* mediaList_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QMenu* recentMenu_ = nullptr;
    std::vector<OutRow> outRows_;
    QVBoxLayout* outRowsLayout_ = nullptr;
    QAction *addNdiAct_ = nullptr, *addCamAct_ = nullptr, *addNdiOutAct_ = nullptr;
    QPushButton *addNdiBtn_ = nullptr, *addCamBtn_ = nullptr;
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
    QAction *importAct_ = nullptr, *openAct_ = nullptr, *undoAct_ = nullptr, *redoAct_ = nullptr;
    QPushButton* importBtn_ = nullptr;
    QWidget* pluginPanel_ = nullptr;
    LockButton* lockBtn_ = nullptr;
    QLabel* lockLabel_ = nullptr;
    void updateUndoActions();
    void pushImport(std::shared_ptr<ImportEntry> e, const QString& text);
    void doUndo();
    void importDroppedFiles(const QStringList& files, bool toPlaylist);
    std::shared_ptr<Scene> importForPlaylist(const QString& path);
    void updateControlStatus();
    void setupInput(QMenu* settingsMenu);
    void runKeyAction(const LiveAction& a);
    MidiService* midi_ = nullptr;
    ShortcutBinder* binder_ = nullptr;
    QAction* inputAct_ = nullptr;
    MultiviewWindow* multiview_ = nullptr;
};

} // namespace lc
