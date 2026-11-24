#pragma once
#include "render/Engine.h"
#include <QTabWidget>
#include <QVBoxLayout>
#include <functional>

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
    Q_OBJECT
public:
    InspectorPanel(Project& project, RenderEngine& engine, QUndoStack* undo, QWidget* parent = nullptr);
    void refreshFromProject();            // goi tren UI thread sau khi them scene/nguon

    // Goi boi lenh Undo/Redo (khong ghi them vao stack):
    void applyLayers(int sceneIdx, const std::vector<Layer>& layers);
    void applyOutput(int outIdx, const OutputConfig& cfg);

    QListWidget* layerList() const { return layerList_; }       // cho test

    // --- Dung chung voi Timeline (moi chinh sua di qua editLayers => co Hoan tac/Lam lai) ---
    int currentSceneIndex() const { return currentScene(); }
    int currentLayerIndex() const { return currentLayerIdx(); }
    std::shared_ptr<Scene> currentScenePtr() const;
    const Layer* currentLayerData() const;
    double playheadTime() const;                                   // thoi gian dong ho cua Scene dang chon
    void editCurrentLayer(const std::function<void(Layer&)>& mutate, const QString& text, int mergeId = -1);
    void refreshProps() { syncProps(); }                           // doc lai o nhap (gia tri hieu dung tai playhead)
    QListWidget* effectList() const { return fxList_; }

signals:
    void layerDataChanged();       // moi khi du lieu/lua chon layer doi (de Timeline ve lai)

public:

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
    void setLayerProp(AnimProp p, double value);                   // gia tri tinh, hoac tu dat keyframe tai playhead neu thuoc tinh co track
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
    QDoubleSpinBox* propSpin_[kAnimPropCount] = {};                // x, y, w, h, alpha, xoay (theo AnimProp)
    QDoubleSpinBox* cropSpin_[4] = {};
    QComboBox* fitCombo_ = nullptr;
    QComboBox* blendCombo_ = nullptr;

    // tab Effect
    QLabel* fxTitle_ = nullptr;
    QLabel* fxNote_ = nullptr;
    QListWidget* fxList_ = nullptr;
    QComboBox* fxAddCombo_ = nullptr;
    QGroupBox* fxParamBox_ = nullptr;
    ParamEditor* fxParams_ = nullptr;
};

} // namespace lc
