#pragma once
#include "render/Engine.h"
#include <QTabWidget>
#include <QVBoxLayout>

class QComboBox;

namespace lc {

// Inspector: tab "Scene / Layer" (nhieu Layer: vi tri, alpha, fit, thu tu, them/xoa) va tab "Output"
// (scale, xoay, mapping crop, hieu chinh mau). Moi thay doi di qua RenderEngine::post().
// UI giu ban sao (cache) Layer/Output de khong doc du lieu render thread dang ghi.
class InspectorPanel : public QTabWidget {
public:
    InspectorPanel(Project& project, RenderEngine& engine, QWidget* parent = nullptr);
    void refreshFromProject();            // goi tren UI thread sau khi them scene/nguon

private:
    QWidget* buildSceneTab();
    QWidget* buildOutputTab();
    void rebuildLayerRows();
    void pushLayers(int sceneIdx);
    void pushOutput(int outIdx);

    Project& project_;
    RenderEngine& engine_;
    std::vector<std::vector<Layer>> layerCache_;     // theo project_.scenes
    std::vector<OutputConfig> outCache_;             // theo output
    QComboBox* sceneCombo_ = nullptr;
    QComboBox* addSrcCombo_ = nullptr;
    QWidget* layerHost_ = nullptr;
    QVBoxLayout* layerRows_ = nullptr;
};

} // namespace lc
