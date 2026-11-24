#pragma once
#include "core/Animation.h"
#include "render/Engine.h"
#include <QTimer>
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

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
    struct SceneProps { double duration = 10; bool loop = true; bool restart = true; };
    SceneProps& props();                         // cache cua Scene dang chon (UI thread)
    void post(std::function<void(double)> fn) { engine_.post(std::move(fn)); }
    void afterEdit();

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
    int dragSession_ = 0;
    int lastScene_ = -2, lastLayer_ = -2;
};

} // namespace lc
