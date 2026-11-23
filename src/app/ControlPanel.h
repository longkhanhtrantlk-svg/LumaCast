#pragma once
#include "render/Engine.h"
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

class QComboBox;

namespace lc {

// Bang dieu khien M2: tab Kenh (Program/Preview/Transition), tab Scene (nhieu Layer), tab Output
// (scale/xoay/mapping/hieu chinh mau). Moi thay doi tu UI di qua RenderEngine::post().
// UI giu ban sao (cache) cua Layer/Output de khong doc du lieu dang bi render thread sua.
class ControlPanel : public QWidget {
public:
    ControlPanel(Project& project, RenderEngine& engine);

private:
    QWidget* buildChannelsTab();
    QWidget* buildSceneTab();
    QWidget* buildOutputTab();
    void rebuildLayerRows();
    void pushLayers(int sceneIdx);
    void pushOutput(int outIdx);
    void refresh();
    void save(bool saveAs);

    Project& project_;
    RenderEngine& engine_;
    QTimer timer_;
    QLabel* fpsLabel_ = nullptr;
    QLabel* status_ = nullptr;
    struct ChanUi { QLabel* program; QLabel* preview; };
    std::vector<ChanUi> chanUi_;

    std::vector<std::vector<Layer>> layerCache_;     // theo project_.scenes
    std::vector<OutputConfig> outCache_;             // theo output
    QComboBox* sceneCombo_ = nullptr;
    QComboBox* addSrcCombo_ = nullptr;
    QWidget* layerHost_ = nullptr;
    QVBoxLayout* layerRows_ = nullptr;
};

} // namespace lc
