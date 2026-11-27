#include "TimelineWidget.h"
#include "InspectorPanel.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QUndoCommand>
#include <QUndoStack>
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

namespace {
// Hoan tac thuoc tinh Scene (thoi luong / lap / tu phat lai). Buoc dau (redo khi push) bo qua vi UI da ap dung.
class ScenePropsCmd : public QUndoCommand {
public:
    ScenePropsCmd(TimelineWidget* t, int si, std::shared_ptr<Scene> sc, TimelineWidget::SceneProps b, TimelineWidget::SceneProps a, const QString& text, int mergeId)
        : QUndoCommand(text), t_(t), si_(si), sc_(std::move(sc)), before_(b), after_(a), id_(mergeId) {}
    int id() const override { return id_; }
    void undo() override { t_->applySceneProps(si_, sc_, before_); }
    void redo() override { if (first_) { first_ = false; return; } t_->applySceneProps(si_, sc_, after_); }
    bool mergeWith(const QUndoCommand* o) override {
        if (o->id() != id_ || id_ < 0) return false;
        after_ = static_cast<const ScenePropsCmd*>(o)->after_;
        return true;
    }
private:
    TimelineWidget* t_;
    int si_;
    std::shared_ptr<Scene> sc_;
    TimelineWidget::SceneProps before_, after_;
    int id_;
    bool first_ = true;
};
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
    bool rubberActive() const { return mode_ == Rubber; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(0x10, 0x13, 0x19));
        const Layer* layer = o_->insp_.currentLayerData();
        const double dur = o_->duration(), ph = o_->playhead();
        const int x0 = o_->timeToX(0), x1 = o_->timeToX(dur);
        const QRect content(TimelineWidget::kLabelW, 0, std::max(0, width() - TimelineWidget::kLabelW), height());

        // nen cac hang
        for (int r = 0; r < kAnimPropCount; ++r)
            p.fillRect(QRect(0, o_->rowTop(r), width(), TimelineWidget::kRowH), r % 2 ? QColor(0x14, 0x17, 0x1e) : QColor(0x11, 0x14, 0x1a));

        // phan co the cuon (thuoc, doan noi, key, playhead): cat trong vung noi dung de khong de len cot nhan
        p.save();
        p.setClipRect(content);
        p.fillRect(QRect(0, 0, width(), TimelineWidget::kRulerH), QColor(0x1b, 0x1e, 0x27));
        const double steps[] = {0.05, 0.1, 0.25, 0.5, 1, 2, 5, 10, 30, 60, 120, 300, 600};
        const double pxPerSec = o_->pxPerSec();
        double step = steps[12];
        for (double s : steps) if (pxPerSec * s >= 56) { step = s; break; }
        p.setFont(QFont(font().family(), 8));
        for (double t = std::floor(o_->viewStart() / step) * step; t <= dur + 1e-9 && o_->timeToX(t) < width() + 60; t += step) {
            if (t < -1e-9) continue;
            const int x = o_->timeToX(t);
            p.setPen(QColor(0x5a, 0x61, 0x78));
            p.drawLine(x, TimelineWidget::kRulerH - 6, x, TimelineWidget::kRulerH);
            p.setPen(QColor(0x9a, 0xa0, 0xb4));
            p.drawText(x + 3, TimelineWidget::kRulerH - 8, QString::number(t, 'f', step < 0.1 ? 2 : step < 1 ? 2 : 0) + "s");
        }
        for (int r = 0; r < kAnimPropCount; ++r) {
            const int top = o_->rowTop(r);
            const Track* tr = layer ? layer->track(AnimProp(r)) : nullptr;
            if (!tr) continue;
            const double cy = top + TimelineWidget::kRowH / 2.0;
            for (size_t i = 0; i + 1 < tr->keys.size(); ++i) {                       // doan noi giua hai key
                const int xa = o_->timeToX(tr->keys[i].time), xb = o_->timeToX(tr->keys[i + 1].time);
                const bool hold = tr->keys[i].interp == Interp::Hold;
                p.fillRect(QRectF(xa, cy - (hold ? 1 : 2), xb - xa, hold ? 2 : 4), hold ? QColor(0x7a, 0x80, 0x94) : QColor(0x2f, 0x7d, 0xe1, 150));
            }
            for (const Keyframe& k : tr->keys)
                drawDiamond(p, QPointF(o_->timeToX(k.time), cy), 6, o_->isSelected(AnimProp(r), k.time) ? QColor(0xf5, 0xc5, 0x18) : QColor(0xe0, 0xa5, 0x26), QColor(0x10, 0x13, 0x19));
        }
        if (!layer) {
            p.setPen(QColor(0x7a, 0x80, 0x94));
            p.drawText(QRect(TimelineWidget::kLabelW, TimelineWidget::kRulerH, width() - TimelineWidget::kLabelW, kAnimPropCount * TimelineWidget::kRowH),
                       Qt::AlignCenter, "Chọn một layer (tab Scene / Layer) để xem và đặt keyframe");
        }
        // het thoi luong + playhead
        p.setPen(QPen(QColor(0x4d, 0x55, 0x70), 1, Qt::DashLine));
        p.drawLine(x1, TimelineWidget::kRulerH, x1, height());
        if (x0 >= TimelineWidget::kLabelW) { p.setPen(QPen(QColor(0x4d, 0x55, 0x70), 1)); p.drawLine(x0, TimelineWidget::kRulerH, x0, height()); }
        const int px = o_->timeToX(ph);
        p.setPen(QPen(QColor(0xe5, 0x32, 0x2d), 2));
        p.drawLine(px, 0, px, height());
        p.setBrush(QColor(0xe5, 0x32, 0x2d));
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygon({QPoint(px - 6, 0), QPoint(px + 6, 0), QPoint(px, 9)}));
        if (mode_ == Rubber) {                                                          // khung chon
            p.setBrush(QColor(0x2f, 0x7d, 0xe1, 50));
            p.setPen(QPen(QColor(0x2f, 0x7d, 0xe1), 1));
            p.drawRect(QRect(rubberA_, rubberB_).normalized());
        }
        p.restore();

        // cot nhan (khong cuon)
        p.fillRect(QRect(0, 0, TimelineWidget::kLabelW, height()), QColor(0x10, 0x13, 0x19));
        for (int r = 0; r < kAnimPropCount; ++r) {
            const int top = o_->rowTop(r);
            p.fillRect(QRect(0, top, TimelineWidget::kLabelW, TimelineWidget::kRowH), r % 2 ? QColor(0x14, 0x17, 0x1e) : QColor(0x11, 0x14, 0x1a));
            const Track* tr = layer ? layer->track(AnimProp(r)) : nullptr;
            p.setPen(tr ? QColor(0xe6, 0xe8, 0xee) : QColor(0x7a, 0x80, 0x94));
            p.setFont(QFont(font().family(), 9));
            p.drawText(QRect(8, top, TimelineWidget::kLabelW - 30, TimelineWidget::kRowH), Qt::AlignVCenter | Qt::AlignLeft, animPropLabel(AnimProp(r)));
            const QPointF bc(TimelineWidget::kLabelW - 14, top + TimelineWidget::kRowH / 2.0);       // nut ◆ them/xoa key tai playhead
            const bool atPh = tr && findKey(*tr, ph) >= 0;
            drawDiamond(p, bc, 5, atPh ? QColor(0xf5, 0xc5, 0x18) : QColor(0x1b, 0x1e, 0x27), QColor(0xf5, 0xc5, 0x18));
        }
        p.fillRect(QRect(0, 0, TimelineWidget::kLabelW, TimelineWidget::kRulerH), QColor(0x1b, 0x1e, 0x27));
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (o_->locked_) return;                       // khoa van hanh: khong sua, khong keo thuoc
        setFocus();
        const QPoint pos = e->position().toPoint();
        const int r = rowAt(pos.y());
        const bool additive = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
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
            if (hitKey(pos, hr, hi)) {
                const Track* tr = o_->insp_.currentLayerData()->track(AnimProp(hr));
                const double t = tr->keys[hi].time;
                if (additive) { o_->toggleKeySelected(AnimProp(hr), t); update(); return; }
                if (!o_->isSelected(AnimProp(hr), t)) o_->selectKey(AnimProp(hr), hi, false);       // bam vao key chua chon: chon rieng no
                mode_ = DragKeys;
                anchorOrigin_ = t;
                pressTime_ = o_->xToTime(pos.x());
                appliedDt_ = 0;
                o_->beginKeyDrag();
                update();
                return;
            }
            if (pos.x() >= TimelineWidget::kLabelW && pos.y() >= TimelineWidget::kRulerH) {         // khoang trong: khung chon
                mode_ = Rubber;
                rubberA_ = rubberB_ = pos;
                baseSel_ = additive ? o_->selection() : std::vector<TimelineWidget::KeyRef>{};
                if (!additive) o_->clearSelection();
                update();
            }
        } else if (e->button() == Qt::RightButton) {
            int hr, hi;
            const Layer* l = o_->insp_.currentLayerData();
            if (!hitKey(pos, hr, hi)) {
                if (pos.x() >= TimelineWidget::kLabelW && o_->hasClipboard() && l) {
                    QMenu menu(this);
                    connect(menu.addAction("Dán keyframe tại playhead"), &QAction::triggered, this, [this] { o_->paste(); });
                    menu.exec(e->globalPosition().toPoint());
                }
                return;
            }
            const Track* tr = l ? l->track(AnimProp(hr)) : nullptr;
            if (!tr) return;
            if (!o_->isSelected(AnimProp(hr), tr->keys[hi].time)) o_->selectKey(AnimProp(hr), hi, false);
            update();
            const int n = int(o_->selection().size());
            QMenu menu(this);
            for (int i = 0; i < kInterpCount; ++i) {
                QAction* a = menu.addAction(interpLabel(Interp(i)));
                a->setCheckable(true);
                a->setChecked(n == 1 && tr->keys[hi].interp == Interp(i));
                connect(a, &QAction::triggered, this, [this, i] { o_->setSelectionInterp(Interp(i)); });
            }
            menu.addSeparator();
            connect(menu.addAction(n > 1 ? QString("Sao chép %1 keyframe").arg(n) : "Sao chép keyframe"), &QAction::triggered, this, [this] { o_->copySelection(); });
            if (o_->hasClipboard()) connect(menu.addAction("Dán keyframe tại playhead"), &QAction::triggered, this, [this] { o_->paste(); });
            connect(menu.addAction(n > 1 ? QString("Xoá %1 keyframe").arg(n) : "Xoá keyframe"), &QAction::triggered, this, [this] { o_->deleteSelection(); });
            menu.exec(e->globalPosition().toPoint());
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        const QPoint pos = e->position().toPoint();
        if (o_->scrubbing_) { o_->seek(o_->xToTime(pos.x())); return; }
        if (mode_ == DragKeys && !o_->selection().empty()) {
            // key "neo" (key dang keo) roi dung o luoi 0,05 s; cac key khac giu nguyen khoang cach tuong doi
            const double target = snap(o_->xToTime(pos.x())) - anchorOrigin_;
            const double step = target - appliedDt_;
            if (std::abs(step) > 1e-9) appliedDt_ += o_->moveSelection(step, true);
        } else if (mode_ == Rubber) {
            rubberB_ = pos;
            const QRect rc = QRect(rubberA_, rubberB_).normalized();
            o_->selectRect(o_->xToTime(rc.left()), o_->xToTime(rc.right()), rowAt(std::max(rc.top(), TimelineWidget::kRulerH)), rowAt(std::min(rc.bottom(), TimelineWidget::kRulerH + kAnimPropCount * TimelineWidget::kRowH - 1)), baseSel_);
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent*) override {
        o_->scrubbing_ = false;
        if (mode_ == Rubber) update();
        mode_ = None;
    }

    void mouseDoubleClickEvent(QMouseEvent* e) override {
        if (o_->locked_) return;
        const QPoint pos = e->position().toPoint();
        const int r = rowAt(pos.y());
        int hr, hi;
        if (r >= 0 && pos.x() >= TimelineWidget::kLabelW && !hitKey(pos, hr, hi)) o_->addKey(AnimProp(r), std::clamp(snap(o_->xToTime(pos.x())), 0.0, o_->duration()));
    }

    void wheelEvent(QWheelEvent* e) override {
        if (e->modifiers() & Qt::ControlModifier) {                                   // Ctrl + lan chuot: zoom quanh con tro
            const double at = o_->xToTime(int(e->position().x()));
            o_->setZoom(o_->zoom() * (e->angleDelta().y() > 0 ? 1.25 : 0.8), at);
            e->accept();
        } else {                                                                       // lan chuot: cuon ngang
            const int d = e->angleDelta().x() != 0 ? e->angleDelta().x() : e->angleDelta().y();
            o_->setViewStart(o_->viewStart() - o_->visibleDuration() * 0.1 * (d > 0 ? 1 : -1));
            e->accept();
        }
    }

    void keyPressEvent(QKeyEvent* e) override {
        if (o_->locked_) return;
        const bool ctrl = e->modifiers() & Qt::ControlModifier;
        if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) o_->deleteSelection();
        else if (ctrl && e->key() == Qt::Key_C) o_->copySelection();
        else if (ctrl && e->key() == Qt::Key_V) o_->paste();
        else if (ctrl && e->key() == Qt::Key_A) o_->selectAllKeys();
        else if (e->key() == Qt::Key_Escape) o_->clearSelection();
        else QWidget::keyPressEvent(e);
    }

private:
    enum Mode { None, DragKeys, Rubber };
    int rowAt(int y) const {
        const int r = (y - TimelineWidget::kRulerH) / TimelineWidget::kRowH;
        return y >= TimelineWidget::kRulerH && r >= 0 && r < kAnimPropCount ? r : -1;
    }
    bool hitKey(const QPoint& pos, int& row, int& idx) const {
        const Layer* l = o_->insp_.currentLayerData();
        row = rowAt(pos.y());
        if (!l || row < 0 || pos.x() < TimelineWidget::kLabelW) return false;
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
    Mode mode_ = None;
    double anchorOrigin_ = 0, pressTime_ = 0, appliedDt_ = 0;
    QPoint rubberA_, rubberB_;
    std::vector<TimelineWidget::KeyRef> baseSel_;
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
    restartChk_->setToolTip("Mỗi lần Scene lên Program (hoặc bắt đầu chuyển vào), hoạt hoạ và video phát lại từ đầu "
                            "(ở Preview video vẫn chạy).\nBỏ chọn: video của Scene này chạy tiếp, không tua về đầu.");
    for (QWidget* w : std::initializer_list<QWidget*>{stopBtn_, playBtn_, timeLabel_, durSpin_, loopChk_, restartChk_}) bar->addWidget(w);
    warnLabel_ = new QLabel;
    warnLabel_->setObjectName("tlWarn");
    warnLabel_->setStyleSheet("color:#e0a526;font-weight:bold");
    warnLabel_->setWordWrap(true);                      // canh bao dai o DONG RIENG: khong ep cua so phai rong them
    bar->addStretch();
    zoomOut_ = new QPushButton("−");
    zoomOut_->setObjectName("tlZoomOut");
    zoomOut_->setToolTip("Thu nhỏ timeline (Ctrl + lăn chuột)");
    zoomIn_ = new QPushButton("+");
    zoomIn_->setObjectName("tlZoomIn");
    zoomIn_->setToolTip("Phóng to timeline (Ctrl + lăn chuột)");
    zoomFitBtn_ = new QPushButton("Vừa khít");
    zoomFitBtn_->setObjectName("tlZoomFit");
    zoomLabel_ = new QLabel("×1");
    zoomLabel_->setObjectName("tlZoomLabel");
    zoomLabel_->setMinimumWidth(36);
    for (QPushButton* b : {zoomOut_, zoomIn_}) b->setFixedWidth(30);
    for (QWidget* w : std::initializer_list<QWidget*>{zoomOut_, zoomLabel_, zoomIn_, zoomFitBtn_}) bar->addWidget(w);
    v->addLayout(bar);
    v->addWidget(warnLabel_);
    view_ = new TrackView(this);
    view_->setObjectName("trackView");
    v->addWidget(view_);
    hbar_ = new QScrollBar(Qt::Horizontal);
    hbar_->setObjectName("tlScroll");
    v->addWidget(hbar_);
    v->addStretch();

    connect(playBtn_, &QPushButton::clicked, this, [this] { playing() ? pause() : play(); });
    connect(stopBtn_, &QPushButton::clicked, this, [this] { stopTransport(); });
    connect(durSpin_, &QDoubleSpinBox::valueChanged, this, [this](double d) { setDuration(d); });
    connect(loopChk_, &QCheckBox::toggled, this, [this](bool b) { setLoop(b); });
    connect(restartChk_, &QCheckBox::toggled, this, [this](bool b) { setRestartOnAir(b); });
    connect(zoomIn_, &QPushButton::clicked, this, [this] { setZoom(zoom_ * 1.5); });
    connect(zoomOut_, &QPushButton::clicked, this, [this] { setZoom(zoom_ / 1.5); });
    connect(zoomFitBtn_, &QPushButton::clicked, this, [this] { zoomFit(); });
    connect(hbar_, &QScrollBar::valueChanged, this, [this](int v) { setViewStart(v / 1000.0); });
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

double TimelineWidget::visibleDuration() const { return std::max(0.001, duration()) / zoom_; }
double TimelineWidget::pxPerSec() const {
    const int w = std::max(1, view_->width() - kLabelW - kRightPad);
    return w / std::max(0.001, visibleDuration());
}
int TimelineWidget::timeToX(double t) const { return kLabelW + int(std::round((t - viewStart_) * pxPerSec())); }
double TimelineWidget::xToTime(int x) const { return std::clamp(viewStart_ + (x - kLabelW) / pxPerSec(), 0.0, duration()); }

void TimelineWidget::clampView() {
    viewStart_ = std::clamp(viewStart_, 0.0, std::max(0.0, duration() - visibleDuration()));
}
void TimelineWidget::syncScroll() {
    if (!hbar_) return;
    QSignalBlocker b(hbar_);
    const int total = int(std::round(duration() * 1000)), page = int(std::round(visibleDuration() * 1000));
    hbar_->setRange(0, std::max(0, total - page));
    hbar_->setPageStep(std::max(1, page));
    hbar_->setSingleStep(std::max(1, page / 10));
    hbar_->setValue(int(std::round(viewStart_ * 1000)));
    hbar_->setEnabled(zoom_ > 1.0001);
    zoomLabel_->setText(QString("×%1").arg(zoom_, 0, 'f', zoom_ < 10 ? 1 : 0));
}
void TimelineWidget::setZoom(double z, double anchor) {
    z = std::clamp(z, 1.0, 64.0);
    const double oldVis = visibleDuration();
    if (anchor < 0) anchor = viewStart_ + oldVis / 2;
    const double frac = oldVis > 0 ? (anchor - viewStart_) / oldVis : 0.5;
    zoom_ = z;
    viewStart_ = anchor - frac * visibleDuration();
    clampView();
    syncScroll();
    view_->update();
}
void TimelineWidget::setViewStart(double t) {
    viewStart_ = t;
    clampView();
    syncScroll();
    view_->update();
}
int TimelineWidget::rowTop(int row) const { return kRulerH + row * kRowH; }

void TimelineWidget::refresh() {
    const int si = insp_.currentSceneIndex(), li = insp_.currentLayerIndex();
    if (si != lastScene_ || li != lastLayer_) {                 // doi layer/scene: bo chon key
        lastScene_ = si; lastLayer_ = li;
        sel_.clear();
    }
    pruneSelection();
    clampView();
    syncScroll();
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

void TimelineWidget::sceneRemoved(int si) {
    if (si >= 0 && si < int(cache_.size())) cache_.erase(cache_.begin() + si);
    while (cache_.size() > project_.scenes.size() && !project_.scenes.empty()) cache_.pop_back();
    lastScene_ = lastLayer_ = -2;
}

void TimelineWidget::sceneInserted(int si) {
    if (si >= 0 && si < int(cache_.size()) && si < int(project_.scenes.size())) {
        const auto& s = project_.scenes[size_t(si)];
        cache_.insert(cache_.begin() + si, {s->duration, s->loop, s->restartOnAir});
    }
    lastScene_ = lastLayer_ = -2;
}

void TimelineWidget::setSceneLoop(int si, bool on) {
    if (locked_ || si < 0 || si >= int(project_.scenes.size())) return;
    while (cache_.size() <= size_t(si)) {
        const auto& s = project_.scenes[cache_.size()];
        cache_.push_back({s->duration, s->loop, s->restartOnAir});
    }
    if (cache_[size_t(si)].loop == on) return;
    auto sc = project_.scenes[size_t(si)];
    const SceneProps before = cache_[size_t(si)];
    SceneProps after = before;
    after.loop = on;
    applySceneProps(si, sc, after);
    insp_.undoStack()->push(new ScenePropsCmd(this, si, sc, before, after, on ? "Bật lặp Scene" : "Tắt lặp Scene", 5000000 + (si % 10000) * 10 + 1));
}

// Ap dung thuoc tinh Scene (tu Hoan tac/Lam lai): cap nhat cache UI + gui render thread.
void TimelineWidget::applySceneProps(int si, std::shared_ptr<Scene> sc, const SceneProps& p) {
    if (si >= 0 && si < int(cache_.size()) && si < int(project_.scenes.size()) && project_.scenes[si] == sc) cache_[si] = p;
    post([sc, p](double) { sc->duration = p.duration; sc->loop = p.loop; sc->restartOnAir = p.restart; if (sc->clock.time > p.duration) sc->clock.time = p.duration; });
    refresh();
}

void TimelineWidget::editSceneProps(const std::function<void(SceneProps&)>& mutate, const QString& text, int propId) {
    auto sc = insp_.currentScenePtr();
    if (!sc) return;
    const int si = std::max(0, insp_.currentSceneIndex());
    SceneProps& cur = props();
    const SceneProps before = cur;
    SceneProps after = before;
    mutate(after);
    cur = after;
    post([sc, after](double) { sc->duration = after.duration; sc->loop = after.loop; sc->restartOnAir = after.restart; if (sc->clock.time > after.duration) sc->clock.time = after.duration; });
    insp_.undoStack()->push(new ScenePropsCmd(this, si, sc, before, after, text, 5000000 + (si % 10000) * 10 + propId));
    refresh();
}

// ---- thuoc tinh Scene (co Hoan tac; cung loai sua lien tiep gop thanh mot buoc) ----

void TimelineWidget::setDuration(double d) {
    if (locked_) return;
    d = std::max(0.1, d);
    if (std::abs(props().duration - d) < 1e-9) return;
    editSceneProps([d](SceneProps& p) { p.duration = d; }, "Đổi thời lượng Scene", 0);
}
void TimelineWidget::setLoop(bool on) {
    if (locked_ || props().loop == on) return;
    editSceneProps([on](SceneProps& p) { p.loop = on; }, on ? "Bật lặp Scene" : "Tắt lặp Scene", 1);
}
void TimelineWidget::setRestartOnAir(bool on) {
    if (locked_ || props().restart == on) return;
    editSceneProps([on](SceneProps& p) { p.restart = on; }, on ? "Bật phát lại khi lên Program" : "Tắt phát lại khi lên Program", 2);
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

// ---------------------------------------------------------------- chon nhieu / nhom / sao chep

namespace {
struct ClipKey { AnimProp prop; double dt; double value; Interp interp; };
std::vector<ClipKey> g_clip;                           // dung chung moi Timeline (dan duoc sang layer/Scene khac)
bool near(double a, double b) { return std::abs(a - b) < 1e-3; }
}

void TimelineWidget::clearSelection() { sel_.clear(); if (view_) view_->update(); }

bool TimelineWidget::isSelected(AnimProp p, double t) const {
    for (const KeyRef& k : sel_) if (k.prop == p && near(k.time, t)) return true;
    return false;
}

void TimelineWidget::pruneSelection() {
    if (sel_.empty()) return;
    const Layer* l = insp_.currentLayerData();
    std::vector<KeyRef> keep;
    for (const KeyRef& k : sel_) {
        const Track* tr = l ? l->track(k.prop) : nullptr;
        if (tr && findKey(*tr, k.time) >= 0) keep.push_back(k);
    }
    sel_ = keep;
}

void TimelineWidget::toggleKeySelected(AnimProp p, double t) {
    for (size_t i = 0; i < sel_.size(); ++i)
        if (sel_[i].prop == p && near(sel_[i].time, t)) { sel_.erase(sel_.begin() + i); view_->update(); return; }
    sel_.push_back({p, t});
    view_->update();
}

void TimelineWidget::selectKey(AnimProp p, int index, bool additive) {
    const Layer* l = insp_.currentLayerData();
    const Track* tr = l ? l->track(p) : nullptr;
    if (!tr || index < 0 || index >= int(tr->keys.size())) return;
    const double t = tr->keys[index].time;
    if (additive) { if (!isSelected(p, t)) sel_.push_back({p, t}); }
    else sel_ = {{p, t}};
    view_->update();
}

void TimelineWidget::selectRect(double t0, double t1, int row0, int row1, const std::vector<KeyRef>& base) {
    sel_ = base;
    const Layer* l = insp_.currentLayerData();
    if (l && row0 >= 0 && row1 >= row0)
        for (int r = row0; r <= row1 && r < kAnimPropCount; ++r)
            if (const Track* tr = l->track(AnimProp(r)))
                for (const Keyframe& k : tr->keys)
                    if (k.time >= t0 - 1e-6 && k.time <= t1 + 1e-6 && !isSelected(AnimProp(r), k.time)) sel_.push_back({AnimProp(r), k.time});
    view_->update();
}

void TimelineWidget::selectAllKeys() {
    sel_.clear();
    if (const Layer* l = insp_.currentLayerData())
        for (const Track& tr : l->tracks) for (const Keyframe& k : tr.keys) sel_.push_back({tr.prop, k.time});
    view_->update();
}

double TimelineWidget::moveSelection(double dt, bool merge) {
    if (locked_ || sel_.empty()) return 0;
    const Layer* cur = insp_.currentLayerData();
    if (!cur) return 0;
    double lo = 1e18, hi = -1e18;
    for (const KeyRef& k : sel_) { lo = std::min(lo, k.time); hi = std::max(hi, k.time); }
    dt = std::clamp(dt, -lo, duration() - hi);                         // khong ra ngoai [0, thoi luong]
    if (std::abs(dt) < 1e-9) return 0;
    for (const KeyRef& k : sel_) {                                     // khong de de len key dang khong duoc chon (se bi ghi de)
        const Track* tr = cur->track(k.prop);
        if (!tr) return 0;
        for (const Keyframe& u : tr->keys)
            if (!isSelected(k.prop, u.time) && near(k.time + dt, u.time)) return 0;
    }
    const std::vector<KeyRef> sel = sel_;
    for (KeyRef& k : sel_) k.time += dt;                              // cap nhat truoc: editCurrentLayer lam refresh() (se bo qua chon neu lech)
    insp_.editCurrentLayer([&](Layer& l) {
        struct Moved { AnimProp prop; double t, value; Interp interp; };
        std::vector<Moved> moved;
        for (const KeyRef& k : sel) {
            Track* tr = l.track(k.prop);
            const int i = tr ? findKey(*tr, k.time) : -1;
            if (i < 0) continue;
            moved.push_back({k.prop, k.time + dt, tr->keys[i].value, tr->keys[i].interp});
            removeKey(*tr, i);
        }
        for (const Moved& m : moved) setKey(*l.track(m.prop), m.t, m.value, m.interp);
    }, sel.size() > 1 ? "Dời nhóm keyframe" : "Dời keyframe", merge ? 4100000 + (dragSession_ % 20000) : -1);
    afterEdit();
    return dt;
}

void TimelineWidget::deleteSelection() {
    if (locked_ || sel_.empty()) return;
    const std::vector<KeyRef> sel = sel_;
    insp_.editCurrentLayer([&](Layer& l) {
        for (int pi = 0; pi < kAnimPropCount; ++pi) {
            Track* tr = l.track(AnimProp(pi));
            if (!tr) continue;
            double last = 0;
            bool any = false;
            for (const KeyRef& k : sel) {
                if (k.prop != AnimProp(pi)) continue;
                const int i = findKey(*tr, k.time);
                if (i < 0) continue;
                last = tr->keys[i].value;
                removeKey(*tr, i);
                any = true;
            }
            if (any && tr->keys.empty()) {                                // het key: thuoc tinh ve gia tri tinh (giu gia tri key vua xoa)
                setBase(l, AnimProp(pi), last);
                l.tracks.erase(std::remove_if(l.tracks.begin(), l.tracks.end(), [pi](const Track& t) { return t.prop == AnimProp(pi); }), l.tracks.end());
            }
        }
    }, sel.size() > 1 ? "Xoá nhóm keyframe" : "Xoá keyframe");
    sel_.clear();
    afterEdit();
}

int TimelineWidget::copySelection() {
    const Layer* l = insp_.currentLayerData();
    if (!l || sel_.empty()) return 0;
    double base = 1e18;
    for (const KeyRef& k : sel_) base = std::min(base, k.time);
    std::vector<ClipKey> clip;
    for (const KeyRef& k : sel_) {
        const Track* tr = l->track(k.prop);
        const int i = tr ? findKey(*tr, k.time) : -1;
        if (i >= 0) clip.push_back({k.prop, k.time - base, tr->keys[i].value, tr->keys[i].interp});
    }
    if (clip.empty()) return 0;
    g_clip = clip;
    return int(clip.size());
}

bool TimelineWidget::hasClipboard() const { return !g_clip.empty(); }

int TimelineWidget::paste(double at) {
    if (locked_ || g_clip.empty() || !insp_.currentLayerData()) return 0;
    if (at < 0) at = playhead();
    std::vector<ClipKey> use;
    for (const ClipKey& c : g_clip)
        if (at + c.dt >= -1e-9 && at + c.dt <= duration() + 1e-6) use.push_back(c);       // key vuot thoi luong Scene bi bo
    if (use.empty()) return 0;
    insp_.editCurrentLayer([&](Layer& l) {
        for (const ClipKey& c : use) {
            Track* tr = l.track(c.prop);
            if (!tr) { l.tracks.push_back(Track{c.prop, {}}); tr = &l.tracks.back(); }
            setKey(*tr, std::max(0.0, at + c.dt), c.value, c.interp);
        }
    }, use.size() > 1 ? "Dán nhóm keyframe" : "Dán keyframe");
    sel_.clear();
    for (const ClipKey& c : use) sel_.push_back({c.prop, std::max(0.0, at + c.dt)});
    afterEdit();
    return int(use.size());
}

void TimelineWidget::setSelectionInterp(Interp interp) {
    if (locked_ || sel_.empty()) return;
    const std::vector<KeyRef> sel = sel_;
    insp_.editCurrentLayer([&](Layer& l) {
        for (const KeyRef& k : sel) {
            Track* tr = l.track(k.prop);
            const int i = tr ? findKey(*tr, k.time) : -1;
            if (i >= 0) tr->keys[i].interp = interp;
        }
    }, "Đổi kiểu nội suy keyframe");
    afterEdit();
}

} // namespace lc
