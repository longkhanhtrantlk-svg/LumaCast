#include "TimelineWidget.h"
#include "InspectorPanel.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace lc {

namespace {
double valueOf(const Layer& l, AnimProp p, double t) {
    const LayerState s = evalLayer(l, t);
    switch (p) {
    case AnimProp::X: return s.rect.x();
    case AnimProp::Y: return s.rect.y();
    case AnimProp::W: return s.rect.width();
    case AnimProp::H: return s.rect.height();
    case AnimProp::Opacity: return s.opacity;
    case AnimProp::Rotation: return s.rotation;
    }
    return 0;
}
void setBase(Layer& l, AnimProp p, double v) {
    switch (p) {
    case AnimProp::X: l.rect.moveLeft(v); break;
    case AnimProp::Y: l.rect.moveTop(v); break;
    case AnimProp::W: l.rect.setWidth(v); break;
    case AnimProp::H: l.rect.setHeight(v); break;
    case AnimProp::Opacity: l.opacity = float(v); break;
    case AnimProp::Rotation: l.rotation = float(v); break;
    }
}
double snap(double t) { return std::round(t / 0.05) * 0.05; }
} // namespace

// ---------------------------------------------------------------- TrackView

class TrackView : public QWidget {
public:
    explicit TrackView(TimelineWidget* o) : o_(o) {
        setMinimumHeight(TimelineWidget::kRulerH + kAnimPropCount * TimelineWidget::kRowH + 6);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(false);
    }
    QSize sizeHint() const override { return {640, minimumHeight()}; }
    void clearSelection() { selRow_ = selIdx_ = -1; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(0x10, 0x13, 0x19));
        const Layer* layer = o_->insp_.currentLayerData();
        const double dur = o_->duration(), ph = o_->playhead();
        const int x0 = o_->timeToX(0), x1 = o_->timeToX(dur);

        // thuoc thoi gian
        p.fillRect(QRect(0, 0, width(), TimelineWidget::kRulerH), QColor(0x1b, 0x1e, 0x27));
        const double steps[] = {0.1, 0.25, 0.5, 1, 2, 5, 10, 30, 60, 120};
        double step = steps[9];
        for (double s : steps) if ((x1 - x0) * s / std::max(0.001, dur) >= 56) { step = s; break; }
        p.setFont(QFont(font().family(), 8));
        for (double t = 0; t <= dur + 1e-9; t += step) {
            const int x = o_->timeToX(t);
            p.setPen(QColor(0x5a, 0x61, 0x78));
            p.drawLine(x, TimelineWidget::kRulerH - 6, x, TimelineWidget::kRulerH);
            p.setPen(QColor(0x9a, 0xa0, 0xb4));
            p.drawText(x + 3, TimelineWidget::kRulerH - 8, QString::number(t, 'f', step < 1 ? 2 : 0) + "s");
        }

        for (int r = 0; r < kAnimPropCount; ++r) {
            const int top = o_->rowTop(r);
            p.fillRect(QRect(0, top, width(), TimelineWidget::kRowH), r % 2 ? QColor(0x14, 0x17, 0x1e) : QColor(0x11, 0x14, 0x1a));
            const Track* tr = layer ? layer->track(AnimProp(r)) : nullptr;
            p.setPen(tr ? QColor(0xe6, 0xe8, 0xee) : QColor(0x7a, 0x80, 0x94));
            p.setFont(QFont(font().family(), 9));
            p.drawText(QRect(8, top, TimelineWidget::kLabelW - 30, TimelineWidget::kRowH), Qt::AlignVCenter | Qt::AlignLeft, animPropLabel(AnimProp(r)));
            // nut ◆ them/xoa key tai playhead
            const QPointF bc(TimelineWidget::kLabelW - 14, top + TimelineWidget::kRowH / 2.0);
            const bool atPh = tr && findKey(*tr, ph) >= 0;
            drawDiamond(p, bc, 5, atPh ? QColor(0xf5, 0xc5, 0x18) : QColor(0x1b, 0x1e, 0x27), QColor(0xf5, 0xc5, 0x18));
            if (!tr) continue;
            const double cy = top + TimelineWidget::kRowH / 2.0;
            for (size_t i = 0; i + 1 < tr->keys.size(); ++i) {                       // doan noi giua hai key
                const int xa = o_->timeToX(tr->keys[i].time), xb = o_->timeToX(tr->keys[i + 1].time);
                const bool hold = tr->keys[i].interp == Interp::Hold;
                p.fillRect(QRectF(xa, cy - (hold ? 1 : 2), xb - xa, hold ? 2 : 4), hold ? QColor(0x7a, 0x80, 0x94) : QColor(0x2f, 0x7d, 0xe1, 150));
            }
            for (size_t i = 0; i < tr->keys.size(); ++i) {
                const bool sel = (selRow_ == r && selIdx_ == int(i));
                drawDiamond(p, QPointF(o_->timeToX(tr->keys[i].time), cy), 6, sel ? QColor(0xf5, 0xc5, 0x18) : QColor(0xe0, 0xa5, 0x26), QColor(0x10, 0x13, 0x19));
            }
        }
        if (!layer) {
            p.setPen(QColor(0x7a, 0x80, 0x94));
            p.drawText(QRect(TimelineWidget::kLabelW, TimelineWidget::kRulerH, width() - TimelineWidget::kLabelW, kAnimPropCount * TimelineWidget::kRowH),
                       Qt::AlignCenter, "Chọn một layer (tab Scene / Layer) để xem và đặt keyframe");
        }
        // het thoi luong + playhead
        p.setPen(QPen(QColor(0x4d, 0x55, 0x70), 1, Qt::DashLine));
        p.drawLine(x1, TimelineWidget::kRulerH, x1, height());
        const int px = o_->timeToX(ph);
        p.setPen(QPen(QColor(0xe5, 0x32, 0x2d), 2));
        p.drawLine(px, 0, px, height());
        p.setBrush(QColor(0xe5, 0x32, 0x2d));
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygon({QPoint(px - 6, 0), QPoint(px + 6, 0), QPoint(px, 9)}));
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (o_->locked_) return;                       // khoa van hanh: khong sua, khong keo thuoc
        setFocus();
        const QPoint pos = e->position().toPoint();
        const int r = rowAt(pos.y());
        if (e->button() == Qt::LeftButton) {
            if (pos.y() < TimelineWidget::kRulerH && pos.x() >= TimelineWidget::kLabelW) {          // tua
                o_->scrubbing_ = true;
                o_->seek(o_->xToTime(pos.x()));
                return;
            }
            if (r >= 0 && pos.x() >= TimelineWidget::kLabelW - 24 && pos.x() < TimelineWidget::kLabelW) {
                o_->toggleKeyAtPlayhead(AnimProp(r));
                return;
            }
            int hr, hi;
            if (hitKey(pos, hr, hi)) { selRow_ = hr; selIdx_ = hi; dragging_ = true; o_->beginKeyDrag(); update(); return; }
            clearSelection();
            update();
        } else if (e->button() == Qt::RightButton) {
            int hr, hi;
            if (!hitKey(pos, hr, hi)) return;
            selRow_ = hr; selIdx_ = hi;
            update();
            const Layer* l = o_->insp_.currentLayerData();
            const Track* tr = l ? l->track(AnimProp(hr)) : nullptr;
            if (!tr) return;
            QMenu menu(this);
            for (int i = 0; i < kInterpCount; ++i) {
                QAction* a = menu.addAction(interpLabel(Interp(i)));
                a->setCheckable(true);
                a->setChecked(tr->keys[hi].interp == Interp(i));
                connect(a, &QAction::triggered, this, [this, hr, hi, i] { o_->setKeyInterp(AnimProp(hr), hi, Interp(i)); });
            }
            menu.addSeparator();
            connect(menu.addAction("Xoá keyframe"), &QAction::triggered, this, [this, hr, hi] { o_->deleteKey(AnimProp(hr), hi); clearSelection(); });
            menu.exec(e->globalPosition().toPoint());
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        const QPoint pos = e->position().toPoint();
        if (o_->scrubbing_) { o_->seek(o_->xToTime(pos.x())); return; }
        if (dragging_ && selRow_ >= 0 && selIdx_ >= 0) {
            const int ni = o_->moveKey(AnimProp(selRow_), selIdx_, snap(o_->xToTime(pos.x())), true);
            if (ni >= 0) selIdx_ = ni;
        }
    }

    void mouseReleaseEvent(QMouseEvent*) override { o_->scrubbing_ = false; dragging_ = false; }

    void mouseDoubleClickEvent(QMouseEvent* e) override {
        if (o_->locked_) return;
        const QPoint pos = e->position().toPoint();
        const int r = rowAt(pos.y());
        int hr, hi;
        if (r >= 0 && pos.x() >= TimelineWidget::kLabelW && !hitKey(pos, hr, hi)) o_->addKey(AnimProp(r), std::clamp(snap(o_->xToTime(pos.x())), 0.0, o_->duration()));
    }

    void keyPressEvent(QKeyEvent* e) override {
        if (o_->locked_) return;
        if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && selRow_ >= 0 && selIdx_ >= 0) {
            o_->deleteKey(AnimProp(selRow_), selIdx_);
            clearSelection();
        } else {
            QWidget::keyPressEvent(e);
        }
    }

private:
    int rowAt(int y) const {
        const int r = (y - TimelineWidget::kRulerH) / TimelineWidget::kRowH;
        return y >= TimelineWidget::kRulerH && r >= 0 && r < kAnimPropCount ? r : -1;
    }
    bool hitKey(const QPoint& pos, int& row, int& idx) const {
        const Layer* l = o_->insp_.currentLayerData();
        row = rowAt(pos.y());
        if (!l || row < 0) return false;
        const Track* tr = l->track(AnimProp(row));
        if (!tr) return false;
        int best = -1, bestD = 9;
        for (size_t i = 0; i < tr->keys.size(); ++i) {
            const int d = std::abs(o_->timeToX(tr->keys[i].time) - pos.x());
            if (d < bestD) { bestD = d; best = int(i); }
        }
        idx = best;
        return best >= 0;
    }
    static void drawDiamond(QPainter& p, QPointF c, double r, QColor fill, QColor line) {
        p.setBrush(fill);
        p.setPen(QPen(line, 1));
        p.drawPolygon(QPolygonF({QPointF(c.x(), c.y() - r), QPointF(c.x() + r, c.y()), QPointF(c.x(), c.y() + r), QPointF(c.x() - r, c.y())}));
    }

    TimelineWidget* o_;
    int selRow_ = -1, selIdx_ = -1;
    bool dragging_ = false;
};

// ---------------------------------------------------------------- TimelineWidget

TimelineWidget::TimelineWidget(InspectorPanel& inspector, RenderEngine& engine, Project& project, QWidget* parent)
    : QWidget(parent), insp_(inspector), engine_(engine), project_(project) {
    for (const auto& s : project_.scenes) cache_.push_back({s->duration, s->loop, s->restartOnAir});

    auto* v = new QVBoxLayout(this);
    auto* bar = new QHBoxLayout;
    stopBtn_ = new QPushButton("⏮ Về 0");
    stopBtn_->setObjectName("tlStop");
    playBtn_ = new QPushButton("▶ Phát");
    playBtn_->setObjectName("tlPlay");
    timeLabel_ = new QLabel;
    timeLabel_->setObjectName("tlTime");
    timeLabel_->setMinimumWidth(130);
    durSpin_ = new QDoubleSpinBox;
    durSpin_->setObjectName("tlDuration");
    durSpin_->setRange(0.1, 3600);
    durSpin_->setDecimals(2);
    durSpin_->setSingleStep(0.5);
    durSpin_->setPrefix("dài ");
    durSpin_->setSuffix(" s");
    loopChk_ = new QCheckBox("Lặp");
    loopChk_->setObjectName("tlLoop");
    restartChk_ = new QCheckBox("Phát lại khi lên Program");
    restartChk_->setObjectName("tlRestart");
    restartChk_->setToolTip("Mỗi lần Scene lên Program (hoặc bắt đầu chuyển vào), đồng hồ chạy lại từ 0.");
    for (QWidget* w : std::initializer_list<QWidget*>{stopBtn_, playBtn_, timeLabel_, durSpin_, loopChk_, restartChk_}) bar->addWidget(w);
    warnLabel_ = new QLabel;
    warnLabel_->setObjectName("tlWarn");
    warnLabel_->setStyleSheet("color:#e0a526;font-weight:bold");
    bar->addWidget(warnLabel_);
    bar->addStretch();
    v->addLayout(bar);
    view_ = new TrackView(this);
    view_->setObjectName("trackView");
    v->addWidget(view_);
    v->addStretch();

    connect(playBtn_, &QPushButton::clicked, this, [this] { playing() ? pause() : play(); });
    connect(stopBtn_, &QPushButton::clicked, this, [this] { stopTransport(); });
    connect(durSpin_, &QDoubleSpinBox::valueChanged, this, [this](double d) { setDuration(d); });
    connect(loopChk_, &QCheckBox::toggled, this, [this](bool b) { setLoop(b); });
    connect(restartChk_, &QCheckBox::toggled, this, [this](bool b) { setRestartOnAir(b); });
    connect(&insp_, &InspectorPanel::layerDataChanged, this, [this] { refresh(); });
    connect(&timer_, &QTimer::timeout, this, [this] { if (isVisible()) refresh(); });
    timer_.start(33);
    refresh();
}

TimelineWidget::SceneProps& TimelineWidget::props() {
    const int si = std::max(0, insp_.currentSceneIndex());
    while (cache_.size() <= size_t(si) && cache_.size() < project_.scenes.size()) {
        const auto& s = project_.scenes[cache_.size()];
        cache_.push_back({s->duration, s->loop, s->restartOnAir});
    }
    if (cache_.empty()) cache_.push_back({});
    return cache_[std::min(size_t(si), cache_.size() - 1)];
}

double TimelineWidget::duration() const { return const_cast<TimelineWidget*>(this)->props().duration; }
double TimelineWidget::playhead() const { return scrubbing_ ? scrubTime_ : insp_.playheadTime(); }
bool TimelineWidget::playing() const { auto s = insp_.currentScenePtr(); return s && s->clock.playing; }
QWidget* TimelineWidget::trackView() const { return view_; }

int TimelineWidget::timeToX(double t) const {
    const int w = std::max(1, view_->width() - kLabelW - kRightPad);
    return kLabelW + int(std::round(t / std::max(0.001, duration()) * w));
}
double TimelineWidget::xToTime(int x) const {
    const int w = std::max(1, view_->width() - kLabelW - kRightPad);
    return std::clamp(double(x - kLabelW) / w * duration(), 0.0, duration());
}
int TimelineWidget::rowTop(int row) const { return kRulerH + row * kRowH; }

void TimelineWidget::refresh() {
    const int si = insp_.currentSceneIndex(), li = insp_.currentLayerIndex();
    if (si != lastScene_ || li != lastLayer_) {                 // doi layer/scene: bo chon key
        lastScene_ = si; lastLayer_ = li;
        view_->clearSelection();
    }
    const SceneProps& sp = props();
    timeLabel_->setText(QString("%1 / %2 s").arg(playhead(), 0, 'f', 2).arg(sp.duration, 0, 'f', 2));
    playBtn_->setText(playing() ? "⏸ Tạm dừng" : "▶ Phát");
    const bool has = insp_.currentScenePtr() != nullptr;
    for (QWidget* w : std::initializer_list<QWidget*>{playBtn_, stopBtn_}) w->setEnabled(has);
    for (QWidget* w : std::initializer_list<QWidget*>{durSpin_, loopChk_, restartChk_}) w->setEnabled(has && !locked_);
    // Canh bao an toan: Scene dang chon dang o Program cua mot kenh -> phat/tua/sua keyframe tac dong TRUC TIEP len man hinh
    bool onAir = false;
    if (auto sc = insp_.currentScenePtr())
        for (const auto& c : project_.channels) {
            const int pg = c->program;
            if (pg >= 0 && pg < int(c->scenes.size()) && c->scenes[pg] == sc) onAir = true;
        }
    warnLabel_->setText(onAir ? "● Scene đang ở PROGRAM: phát/tua/sửa tác động trực tiếp lên màn hình" : QString());
    { QSignalBlocker b(durSpin_); durSpin_->setValue(sp.duration); }
    { QSignalBlocker b(loopChk_); loopChk_->setChecked(sp.loop); }
    { QSignalBlocker b(restartChk_); restartChk_->setChecked(sp.restart); }
    view_->update();
}

void TimelineWidget::afterEdit() {
    insp_.refreshProps();
    refresh();
}

// ---- phat ----

void TimelineWidget::play() {
    auto sc = insp_.currentScenePtr();
    if (!sc) return;
    const double d = duration();
    post([sc, d](double now) { sc->clock.play(now, d); });
}
void TimelineWidget::pause() {
    auto sc = insp_.currentScenePtr();
    if (sc) post([sc](double) { sc->clock.pause(); });
}
void TimelineWidget::stopTransport() {
    auto sc = insp_.currentScenePtr();
    if (!sc) return;
    post([sc](double) { sc->clock.stop(); });
    scrubTime_ = 0;
    emit playheadChanged();
    QTimer::singleShot(80, this, [this] { afterEdit(); });
}
void TimelineWidget::seek(double t) {
    auto sc = insp_.currentScenePtr();
    if (!sc) return;
    t = std::clamp(t, 0.0, duration());
    scrubTime_ = t;
    post([sc, t](double) { sc->clock.seek(t); });
    emit playheadChanged();
    refresh();
}

// ---- thuoc tinh Scene (khong qua Hoan tac) ----

void TimelineWidget::setDuration(double d) {
    if (locked_) return;
    auto sc = insp_.currentScenePtr();
    if (!sc) return;
    d = std::max(0.1, d);
    props().duration = d;
    post([sc, d](double) { sc->duration = d; if (sc->clock.time > d) sc->clock.time = d; });
    refresh();
}
void TimelineWidget::setLoop(bool on) {
    if (locked_) return;
    auto sc = insp_.currentScenePtr();
    if (!sc) return;
    props().loop = on;
    post([sc, on](double) { sc->loop = on; });
}
void TimelineWidget::setRestartOnAir(bool on) {
    if (locked_) return;
    auto sc = insp_.currentScenePtr();
    if (!sc) return;
    props().restart = on;
    post([sc, on](double) { sc->restartOnAir = on; });
}

// ---- keyframe (moi thao tac la mot buoc Hoan tac) ----

void TimelineWidget::addKey(AnimProp p, double t) {
    if (locked_) return;
    insp_.editCurrentLayer([p, t](Layer& l) {
        const double v = valueOf(l, p, t);
        Track* tr = l.track(p);
        if (!tr) { l.tracks.push_back(Track{p, {}}); tr = &l.tracks.back(); }
        setKey(*tr, t, v, Interp::Linear);
    }, "Thêm keyframe");
    afterEdit();
}

void TimelineWidget::toggleKeyAtPlayhead(AnimProp p) {
    if (locked_) return;
    const Layer* l = insp_.currentLayerData();
    if (!l) return;
    const double t = playhead();
    const Track* tr = l->track(p);
    const int k = tr ? findKey(*tr, t) : -1;
    if (k >= 0) deleteKey(p, k); else addKey(p, t);
}

int TimelineWidget::moveKey(AnimProp p, int index, double newTime, bool merge) {
    if (locked_) return -1;
    const Layer* cur = insp_.currentLayerData();
    const Track* tr0 = cur ? cur->track(p) : nullptr;
    if (!tr0 || index < 0 || index >= int(tr0->keys.size())) return -1;
    newTime = std::clamp(newTime, 0.0, duration());
    for (size_t i = 0; i < tr0->keys.size(); ++i)                          // khong de hai key trung nhau
        if (int(i) != index && std::abs(tr0->keys[i].time - newTime) < 1e-3) return index;
    if (std::abs(tr0->keys[index].time - newTime) < 1e-9) return index;
    int newIdx = index;
    insp_.editCurrentLayer([&](Layer& l) {
        Track* tr = l.track(p);
        Keyframe k = tr->keys[index];
        tr->keys.erase(tr->keys.begin() + index);
        newIdx = setKey(*tr, newTime, k.value, k.interp);
    }, "Dời keyframe", merge ? 4000000 + (dragSession_ % 20000) * 100 + int(p) : -1);
    afterEdit();
    return newIdx;
}

void TimelineWidget::deleteKey(AnimProp p, int index) {
    if (locked_) return;
    insp_.editCurrentLayer([p, index](Layer& l) {
        Track* tr = l.track(p);
        if (!tr || index < 0 || index >= int(tr->keys.size())) return;
        const double last = tr->keys[index].value;
        removeKey(*tr, index);
        if (tr->keys.empty()) {                                              // het key: thuoc tinh ve gia tri tinh (giu gia tri key vua xoa)
            setBase(l, p, last);
            l.tracks.erase(std::remove_if(l.tracks.begin(), l.tracks.end(), [p](const Track& t) { return t.prop == p; }), l.tracks.end());
        }
    }, "Xoá keyframe");
    afterEdit();
}

void TimelineWidget::setKeyInterp(AnimProp p, int index, Interp interp) {
    if (locked_) return;
    insp_.editCurrentLayer([p, index, interp](Layer& l) {
        Track* tr = l.track(p);
        if (tr && index >= 0 && index < int(tr->keys.size())) tr->keys[index].interp = interp;
    }, "Đổi kiểu nội suy keyframe");
    afterEdit();
}

} // namespace lc
