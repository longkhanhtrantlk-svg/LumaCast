#pragma once
#include "render/Engine.h"
#include <QLabel>
#include <QTimer>
#include <QWidget>

namespace lc {

// Bang dieu khien toi gian cho M1 (UI Qt Quick day du o M3).
class ControlPanel : public QWidget {
public:
    ControlPanel(Project& project, RenderEngine& engine);
private:
    void refresh();
    Project& project_;
    RenderEngine& engine_;
    QTimer timer_;
    QLabel* fpsLabel_ = nullptr;
    struct ChanUi { QLabel* program; QLabel* preview; };
    std::vector<ChanUi> chanUi_;
};

} // namespace lc
