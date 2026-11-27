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
class QPushButton;
class QSpinBox;
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
    void rebuildOutputs();                // danh sach Output / canvas vua doi (thiet lap man hinh): doc lai va dung lai tab Output
    void sceneRemoved(int sceneIdx);      // UI thread: Scene sceneIdx da bi go khoi project (xoa, hoan tac nhap media) -> bo ban sao tuong ung
    void sceneInserted(int sceneIdx);     // UI thread: Scene duoc chen lai vao giua danh sach (hoan tac xoa)

    // Goi boi lenh Undo/Redo (khong ghi them vao stack):
    void applyLayers(int sceneIdx, const std::vector<Layer>& layers);
    void applyOutput(int outIdx, const OutputConfig& cfg);

    // --- Dung chung voi cua so Mapping truc quan (MappingWindow) ---
    static constexpr int kMappingMergeBase = 900000000;            // lenh Output co mergeId >= moc nay tu bien mat neu ket qua = ban dau
    int outputCount() const { return int(outCache_.size()); }
    const OutputConfig& outputConfig(int outIdx) const { return outCache_[outIdx]; }
    // Doi vung cat cua Output (co Hoan tac, ton trong Khoa van hanh). Cung mergeId >= 0 lien tiep = mot buoc.
    void setOutputCrop(int outIdx, const QRectF& crop, const QString& text, int mergeId);

    QListWidget* layerList() const { return layerList_; }       // cho test
    // Dung chung voi viec keo hinh tren man Preview (ban sao UI la noi duy nhat sua Layer).
    const std::vector<Layer>* layersOf(int sceneIdx) const;
    void selectLayer(int sceneIdx, int layerIdx);
    // Doi khung (va kieu fit neu fit >= 0) cua mot Layer: co Hoan tac, ton trong Khoa van hanh, thuoc tinh co keyframe thi sua key.
    void setLayerRect(int sceneIdx, int layerIdx, const QRectF& rect, const QString& text, int mergeId, int fit = -1);
    void reorderLayers(const std::vector<int>& rows, int before);   // theo hang cua danh sach (keo tha)
    QUndoStack* undoStack() const { return undo_; }
    // Khoa van hanh: vo hieu ca panel VA chan moi chinh sua bang lenh (defence in depth).
    void setLocked(bool on) { locked_ = on; setEnabled(!on); }
    bool locked() const { return locked_; }

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
    void outputConfigChanged(int outIdx);                          // sua / Hoan tac / Lam lai cau hinh Output
    void mappingRequested();                                       // nut "Mapping truc quan..." o tab Output

public:

    // Danh sach Effect co the them + ghi chu loi: goi khi kho Effect/plug-in thay doi.
    void refreshEffectCatalog();
    void updateEffectNote();

    // --- Bu khe video wall (OUT-04, InspectorWall.cpp) ---
    struct WallState {                                             // du de ap dung / hoan tac mot lan doi khe
        QSize canvas;
        std::vector<std::pair<std::shared_ptr<Scene>, QSize>> scenes;
        std::vector<std::pair<int, QRectF>> crops;                 // chi so Output -> crop
    };
    void applyWall(int channel, const WallState& state);           // goi boi Undo/Redo
    // Doi khe cua wall tren Channel (co Hoan tac, ton trong Khoa van hanh, khong doi Program/Preview). Canvas va
    // cac Scene cung co canvas doi co theo; Output giu 1:1. Cung axis (0 = ngang, 1 = doc) lien tiep = mot buoc.
    void setWallGap(int channel, QSize gap, int axis);

private:
    static void narrowFields(QWidget* root);
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
    bool locked_ = false;
    std::vector<std::vector<Layer>> layerCache_;     // theo project_.scenes
    std::vector<const Scene*> comboScenes_;          // Scene cua tung muc trong sceneCombo_ (giu lua chon khi danh sach doi)
    std::vector<OutputConfig> outCache_;             // theo output
    std::vector<OutW> outW_;
    QComboBox* sceneCombo_ = nullptr;
    QComboBox* addSrcCombo_ = nullptr;
    QListWidget* layerList_ = nullptr;
    QPushButton *layerAdd_ = nullptr, *layerDel_ = nullptr;
    std::vector<QPushButton*> fxOps_;                              // Effect len / xuong / go
    QGroupBox* propBox_ = nullptr;
    QDoubleSpinBox* propSpin_[kAnimPropCount] = {};                // x, y, w, h, alpha, xoay (theo AnimProp)
    QDoubleSpinBox* cropSpin_[4] = {};
    QComboBox* fitCombo_ = nullptr;
    QComboBox* blendCombo_ = nullptr;
    QDoubleSpinBox* volumeSpin_ = nullptr;           // am luong nguon trong Layer (%)

    // tab Effect
    QLabel* fxTitle_ = nullptr;
    QLabel* fxNote_ = nullptr;
    QListWidget* fxList_ = nullptr;
    QComboBox* fxAddCombo_ = nullptr;
    QGroupBox* fxParamBox_ = nullptr;
    ParamEditor* fxParams_ = nullptr;

    // bu khe video wall (tab Output)
    struct WallW {
        int channel = 0;
        QGroupBox* box = nullptr;
        QSpinBox* gap[2] = {};
        QLabel* info = nullptr;
    };
    void addWallBoxes(QVBoxLayout* root);
    void syncWallBoxes();
    std::vector<WallW> wallW_;
    std::vector<QSize> canvasCache_;                  // theo project_.channels
};

} // namespace lc
