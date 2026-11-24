#pragma once
#include "core/Animation.h"
#include "render/Engine.h"
#include <QTimer>
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QScrollBar;

namespace lc {

class InspectorPanel;
class TrackView;

// Timeline: dieu khien phat (Play/Pause/Ve 0), thuoc/playhead, cac lan keyframe cua LAYER dang chon trong SCENE dang chon.
// Moi chinh sua keyframe di qua InspectorPanel::editCurrentLayer => co Hoan tac/Lam lai.
// Thoi luong/lap/phat-lai-khi-len-Program la thuoc tinh Scene (chua hoan tac duoc).
class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    TimelineWidget(InspectorPanel& inspector, RenderEngine& engine, Project& project, QWidget* parent = nullptr);

    // ---- Lenh (cung la diem vao cho test) ----
    void play();
    void pause();
    void stopTransport();                        // dung va ve 0
    void seek(double t);
    void toggleKeyAtPlayhead(AnimProp p);        // them key tai playhead (gia tri hien tai) hoac xoa neu da co
    void addKey(AnimProp p, double t);
    void beginKeyDrag() { ++dragSession_; }       // moi lan nhan chuot de keo = mot buoc hoan tac rieng
    int moveKey(AnimProp p, int index, double newTime, bool merge);   // tra ve chi so moi sau khi sap xep (-1 neu khong doi duoc)
    void deleteKey(AnimProp p, int index);
    void setKeyInterp(AnimProp p, int index, Interp interp);
    void setDuration(double seconds);
    void setLoop(bool on);
    void setRestartOnAir(bool on);

    // ---- Zoom / cuon ngang (SCN-07): zoom 1 = vua khit toan bo thoi luong; toi da 64 ----
    struct KeyRef { AnimProp prop; double time; };
    void setZoom(double z, double anchorTime = -1);          // giu nguyen thoi diem anchorTime (mac dinh: giua vung nhin) tai cung vi tri tren man hinh
    double zoom() const { return zoom_; }
    void zoomFit() { setZoom(1); }
    void setViewStart(double t);                             // thoi diem o mep trai vung nhin (giay)
    double viewStart() const { return viewStart_; }
    double visibleDuration() const;
    double pxPerSec() const;

    // ---- Chon nhieu keyframe, di chuyen nhom, sao chep/dan (moi thao tac = mot buoc Hoan tac; keo lien tuc = mot buoc) ----
    const std::vector<KeyRef>& selection() const { return sel_; }
    bool isSelected(AnimProp p, double time) const;
    void clearSelection();
    void selectKey(AnimProp p, int index, bool additive);
    void toggleKeySelected(AnimProp p, double time);
    void selectRect(double t0, double t1, int row0, int row1, const std::vector<KeyRef>& base = {});   // chon moi key trong khung (hang row0..row1, thoi gian t0..t1)
    void selectAllKeys();
    double moveSelection(double dt, bool merge);             // dich ca nhom dt giay (kep trong [0, thoi luong], khong de de len key khac); tra ve dt thuc su ap dung
    void deleteSelection();
    int copySelection();                                     // tra ve so key da chep
    int paste(double atTime = -1);                           // dan tai atTime (mac dinh: playhead) vao layer dang chon; tra ve so key da dan
    bool hasClipboard() const;
    void setSelectionInterp(Interp interp);

    // Khoa van hanh: chan sua keyframe/thoi luong/lap va keo thuoc tua; Phat/Tam dung/Ve 0 van dung duoc.
    void setLocked(bool on) { locked_ = on; refresh(); }
    bool locked() const { return locked_; }
    void sceneRemoved(int sceneIdx);             // UI thread: bo cache cua Scene da go (hoan tac nhap media)
    struct SceneProps { double duration = 10; bool loop = true; bool restart = true; };
    void applySceneProps(int sceneIdx, std::shared_ptr<Scene> scene, const SceneProps& p);   // dung cho Hoan tac/Lam lai
    double duration() const;
    double playhead() const;
    bool playing() const;
    int timeToX(double t) const;
    double xToTime(int x) const;
    int rowTop(int row) const;
    QWidget* trackView() const;

    static constexpr int kLabelW = 140, kRulerH = 22, kRowH = 24, kRightPad = 14;

signals:
    void playheadChanged();                      // nguoi dung tua -> Inspector doc lai gia tri hieu dung

public slots:
    void refresh();                              // du lieu/lua chon layer doi

private:
    friend class TrackView;
    SceneProps& props();                         // cache cua Scene dang chon (UI thread)
    void post(std::function<void(double)> fn) { engine_.post(std::move(fn)); }
    void afterEdit();
    void editSceneProps(const std::function<void(SceneProps&)>& mutate, const QString& text, int propId);

    InspectorPanel& insp_;
    RenderEngine& engine_;
    Project& project_;
    std::vector<SceneProps> cache_;
    TrackView* view_ = nullptr;
    QPushButton* playBtn_ = nullptr;
    QPushButton* stopBtn_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QLabel* warnLabel_ = nullptr;
    QDoubleSpinBox* durSpin_ = nullptr;
    QCheckBox* loopChk_ = nullptr;
    QCheckBox* restartChk_ = nullptr;
    QTimer timer_;
    double scrubTime_ = 0;
    bool scrubbing_ = false;
    bool locked_ = false;
    double zoom_ = 1.0, viewStart_ = 0.0;
    std::vector<KeyRef> sel_;
    QScrollBar* hbar_ = nullptr;
    QPushButton *zoomIn_ = nullptr, *zoomOut_ = nullptr, *zoomFitBtn_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
    void clampView();
    void syncScroll();
    void pruneSelection();
    int dragSession_ = 0;
    int lastScene_ = -2, lastLayer_ = -2;
};

} // namespace lc
