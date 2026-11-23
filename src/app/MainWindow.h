#pragma once
#include "render/Engine.h"
#include "render/OutputSupervisor.h"
#include "render/PluginManager.h"
#include "core/Transition.h"
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


namespace lc {

class InspectorPanel;
class MonitorWidget;

// Cua so chinh kieu "live production": Preview/Program, thanh chuyen, Scene list, Media Pool,
// Outputs (Black/Freeze/trang thai), Inspector. Moi thao tac di qua RenderEngine::post().
class MainWindow : public QMainWindow {
public:
    MainWindow(Project& project, RenderEngine& engine, QStringList pluginDirs = PluginManager::defaultDirs());
    PluginManager& plugins() { return pm_; }
    TransitionSpec currentSpec() const;          // hieu ung chuyen dang chon tren thanh chuyen

    void setAskOnClose(bool ask) { askOnClose_ = ask; }
    void saveProject(bool saveAs);
    void autosaveNow();
    bool addMediaFile(const QString& path);        // them nguon + Scene tu file; false neu loi
    QString autosavePath() const;
    InspectorPanel* inspector() const { return inspector_; }
    QUndoStack* undoStack() { return &undo_; }

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    struct OutRow { QLabel* dot; QLabel* info; QToolButton* black; QToolButton* freeze; QToolButton* pattern; uint64_t lastPresented = 0; };

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
    void showMessage(const QString& msg);

    Project& project_;
    RenderEngine& engine_;
    OutputSupervisor sup_;             // theo doi mat man hinh / sai man hinh
    PluginManager pm_;                 // plug-in shader (Effect/Transition)
    QUndoStack undo_;                  // phai khai bao truoc inspector_
    InspectorPanel* inspector_ = nullptr;
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
    std::vector<std::vector<QString>> sceneNames_;     // theo kenh; snapshot de timer khong cham du lieu render thread
    QTimer fastTimer_, slowTimer_, autosaveTimer_;
    QByteArray defaultState_;
    int lastProgram_ = -2, lastPreview_ = -2, lastChannel_ = -1;
    qint64 lastSlowMs_ = 0;
    bool lastStalled_ = false;
    bool askOnClose_ = true;
};

} // namespace lc
