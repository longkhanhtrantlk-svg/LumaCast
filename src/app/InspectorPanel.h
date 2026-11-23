#pragma once
#include "render/Engine.h"
#include <QTabWidget>
#include <QVBoxLayout>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QListWidget;
class QUndoStack;

namespace lc {

class ParamEditor;

// Inspector: tab "Scene / Layer" (danh sach Layer keo-tha, an/hien, thuoc tinh layer dang chon) va tab "Output"
// (scale, xoay, mapping crop, hieu chinh mau). Moi thay doi di qua RenderEngine::post() va duoc ghi vao
// QUndoStack (Hoan tac / Lam lai; cac chinh sua lien tiep cung mot truong gop thanh mot buoc).
// UI giu ban sao (cache) Layer/Output de khong doc du lieu render thread dang ghi.
class InspectorPanel : public QTabWidget {
public:
    InspectorPanel(Project& project, RenderEngine& engine, QUndoStack* undo, QWidget* parent = nullptr);
    void refreshFromProject();            // goi tren UI thread sau khi them scene/nguon

    // Goi boi lenh Undo/Redo (khong ghi them vao stack):
    void applyLayers(int sceneIdx, const std::vector<Layer>& layers);
    void applyOutput(int outIdx, const OutputConfig& cfg);

    QListWidget* layerList() const { return layerList_; }       // cho test
    QListWidget* effectList() const { return fxList_; }

    // Danh sach Effect co the them + ghi chu loi: goi khi kho Effect/plug-in thay doi.
    void refreshEffectCatalog();
    void updateEffectNote();

private:
    QWidget* buildSceneTab();
    QWidget* buildOutputTab();
    QWidget* buildEffectTab();
    void syncEffectList(int selectRow);
    void syncEffectParams();
    int currentScene() const;
    int currentLayerIdx() const;                                 // chi so trong vector (-1 neu chua chon)
    void syncLayerList(int selectIdx);
    void syncProps();
    void pushLayers(int sceneIdx);
    void pushOutput(int outIdx);
    void editLayers(int si, std::vector<Layer> after, const QString& text, int mergeId = -1);
    void editOutput(int i, OutputConfig after, const QString& text, int mergeId = -1);
    void onLayersReordered();
    void syncOutputWidgets(int i);

    struct OutW {
        QComboBox* scale = nullptr;
        QComboBox* rot = nullptr;
        QDoubleSpinBox* crop[4] = {};
        QDoubleSpinBox* cal[6] = {};
    };

    Project& project_;
    RenderEngine& engine_;
    QUndoStack* undo_;
    std::vector<std::vector<Layer>> layerCache_;     // theo project_.scenes
    std::vector<OutputConfig> outCache_;             // theo output
    std::vector<OutW> outW_;
    QComboBox* sceneCombo_ = nullptr;
    QComboBox* addSrcCombo_ = nullptr;
    QListWidget* layerList_ = nullptr;
    QGroupBox* propBox_ = nullptr;
    QDoubleSpinBox* rectSpin_[4] = {};
    QDoubleSpinBox* alphaSpin_ = nullptr;
    QComboBox* fitCombo_ = nullptr;

    // tab Effect
    QLabel* fxTitle_ = nullptr;
    QLabel* fxNote_ = nullptr;
    QListWidget* fxList_ = nullptr;
    QComboBox* fxAddCombo_ = nullptr;
    QGroupBox* fxParamBox_ = nullptr;
    ParamEditor* fxParams_ = nullptr;
};

} // namespace lc
