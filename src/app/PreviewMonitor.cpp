#include "PreviewMonitor.h"
#include "Text.h"
#include "Theme.h"
#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace lc {

PreviewMonitor::PreviewMonitor(const QColor& color, QWidget* parent) : MonitorWidget("PREVIEW", color, false, parent) {
    setObjectName("previewMonitor");
    setMouseTracking(true);
}

void PreviewMonitor::setBoxes(const void* key, std::vector<Box> boxes, int selected, bool onAir, bool editable) {
    if (key != key_ || !editable) {
        key_ = key;
        active_ = false;
        mode_ = 0;
        hover_ = -1;
        if (!editable) unsetCursor();
    }
    boxes_ = std::move(boxes);
    selected_ = selected;
    onAir_ = onAir;
    editable_ = editable;
    update();
}

QRect PreviewMonitor::boxRect(const QRectF& r) const {
    const QRect view = imageRect();
    if (view.isEmpty()) return {};
    return QRect(view.left() + int(std::lround(r.x() * view.width())), view.top() + int(std::lround(r.y() * view.height())),
                 std::max(1, int(std::lround(r.width() * view.width()))), std::max(1, int(std::lround(r.height() * view.height()))));
}

const PreviewMonitor::Box* PreviewMonitor::selectedBox() const {
    for (const Box& b : boxes_) if (b.layer == selected_) return &b;
    return nullptr;
}

int PreviewMonitor::edgesAt(const QRect& box, const QPoint& p) const {
    constexpr int k = 8;
    if (box.isEmpty() || !box.adjusted(-k, -k, k, k).contains(p)) return 0;
    const bool small = box.width() < 3 * k || box.height() < 3 * k;
    const int in = small ? 0 : k;
    int edge = 0;
    if (p.x() <= box.left() + in) edge |= Left;
    else if (p.x() >= box.right() - in) edge |= Right;
    if (p.y() <= box.top() + in) edge |= Top;
    else if (p.y() >= box.bottom() - in) edge |= Bottom;
    return edge ? edge : Move;
}

int PreviewMonitor::layerAt(const QPoint& p) const {
    if (!imageRect().contains(p)) return -1;
    for (auto it = boxes_.rbegin(); it != boxes_.rend(); ++it)
        if (boxRect(it->rect).contains(p)) return it->layer;
    return -1;
}

void PreviewMonitor::shape(int h) {
    if (!h) unsetCursor();
    else if (h == Move) setCursor(Qt::SizeAllCursor);
    else if (h == (Left | Top) || h == (Right | Bottom)) setCursor(Qt::SizeFDiagCursor);
    else if (h == (Right | Top) || h == (Left | Bottom)) setCursor(Qt::SizeBDiagCursor);
    else setCursor(h & (Left | Right) ? Qt::SizeHorCursor : Qt::SizeVerCursor);
}

void PreviewMonitor::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !editable_) return;
    const QPoint p = e->position().toPoint();
    mode_ = 0;
    moved_ = false;
    // canh/goc cua hinh dang chon duoc uu tien; con lai chon hinh tren cung duoi con tro
    if (const Box* s = active_ ? selectedBox() : nullptr) {
        const int h = edgesAt(boxRect(s->rect), p);
        if (h && h != Move) { mode_ = h; dragLayer_ = s->layer; start_ = s->rect; }
    }
    if (!mode_) {
        const int layer = layerAt(p);
        if (layer < 0) { active_ = false; update(); return; }
        mode_ = Move;
        dragLayer_ = layer;
        for (const Box& b : boxes_) if (b.layer == layer) start_ = b.rect;
    }
    press_ = p;
    ++gesture_;
    active_ = true;
    selected_ = dragLayer_;
    if (onPick) onPick(dragLayer_);
    update();
}

void PreviewMonitor::mouseMoveEvent(QMouseEvent* e) {
    const QPoint p = e->position().toPoint();
    if (!editable_) return;
    if (!mode_) {
        const int over = layerAt(p);
        int h = 0;
        if (const Box* s = active_ ? selectedBox() : nullptr) h = edgesAt(boxRect(s->rect), p);
        if (!h && over >= 0) h = Move;
        shape(h);
        if (over != hover_) { hover_ = over; update(); }
        return;
    }
    if (!moved_ && (p - press_).manhattanLength() < QApplication::startDragDistance()) return;    // bam nham: khong xe dich hinh
    moved_ = true;
    const QRect view = imageRect();
    if (view.isEmpty() || !onDrag) return;
    const double dx = double(p.x() - press_.x()) / view.width(), dy = double(p.y() - press_.y()) / view.height();
    const double snapX = 6.0 / view.width(), snapY = 6.0 / view.height();
    const double minW = std::max(0.01, 8.0 / view.width()), minH = std::max(0.01, 8.0 / view.height());
    auto snapped = [](double v, std::initializer_list<double> targets, double tolerance) {
        for (double t : targets) if (std::abs(v - t) <= tolerance) return t;
        return v;
    };
    const double w = start_.width(), h = start_.height();
    QRectF r = start_;
    if (mode_ == Move) {
        double x = snapped(start_.x() + dx, {0.0, 1.0 - w, (1.0 - w) / 2}, snapX);
        double y = snapped(start_.y() + dy, {0.0, 1.0 - h, (1.0 - h) / 2}, snapY);
        x = std::clamp(x, std::min(start_.x(), minW - w), std::max(start_.x(), 1.0 - minW));     // luon con mot mieng tren man
        y = std::clamp(y, std::min(start_.y(), minH - h), std::max(start_.y(), 1.0 - minH));
        r.moveTo(x, y);
    } else {
        double l = start_.x(), t = start_.y(), rr = start_.x() + w, b = start_.y() + h;
        const bool corner = (mode_ & (Left | Right)) && (mode_ & (Top | Bottom));
        if (mode_ & Left) l = std::min(snapped(l + dx, {0.0}, snapX), rr - minW);
        if (mode_ & Right) rr = std::max(snapped(rr + dx, {1.0}, snapX), l + minW);
        if (mode_ & Top) t = std::min(snapped(t + dy, {0.0}, snapY), b - minH);
        if (mode_ & Bottom) b = std::max(snapped(b + dy, {1.0}, snapY), t + minH);
        if (corner && w > 0 && h > 0) {                          // keo goc: giu ti le khung
            const double kw = (rr - l) / w, kh = (b - t) / h;
            double k = std::abs(kw - 1) > std::abs(kh - 1) ? kw : kh;
            k = std::max({k, minW / w, minH / h});
            if (mode_ & Left) l = rr - w * k; else rr = l + w * k;
            if (mode_ & Top) t = b - h * k; else b = t + h * k;
        }
        r = QRectF(l, t, std::min(8.0, rr - l), std::min(8.0, b - t));
    }
    for (Box& bx : boxes_) if (bx.layer == dragLayer_) bx.rect = r;      // khung theo tay ngay, khong doi vong lam moi
    onDrag(dragLayer_, r, gesture_);
    update();
}

void PreviewMonitor::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) mode_ = 0;
}

void PreviewMonitor::leaveEvent(QEvent* e) {
    if (hover_ >= 0) { hover_ = -1; update(); }
    MonitorWidget::leaveEvent(e);
}

void PreviewMonitor::paintExtra(QPainter& p) {
    const QRect view = imageRect();
    if (!editable_ || view.isEmpty()) return;
    p.save();
    p.setBrush(Qt::NoBrush);
    const Box* sel = active_ ? selectedBox() : nullptr;
    for (const Box& b : boxes_)
        if (b.layer == hover_ && (!sel || sel->layer != b.layer)) {
            p.setPen(QPen(QColor(255, 255, 255, 160), 1, Qt::DashLine));
            p.drawRect(boxRect(b.rect).intersected(rect()).adjusted(0, 0, -1, -1));
        }
    if (sel) {
        const QRect r = boxRect(sel->rect);
        p.setPen(QPen(QColor(colors::kWarn), 2));
        p.drawRect(r.adjusted(0, 0, -1, -1));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(colors::kWarn));
        const QPoint c = r.center();
        for (const QPoint& h : {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight(), QPoint(c.x(), r.top()), QPoint(c.x(), r.bottom()),
                                QPoint(r.left(), c.y()), QPoint(r.right(), c.y())})
            p.drawRect(QRect(h.x() - 4, h.y() - 4, 8, 8));
        if (onAir_) {                                            // Preview va Program la cung mot Scene
            const QString t = p.fontMetrics().elidedText(text::pvOnAir(), Qt::ElideRight, view.width() - 12);
            const QRect badge(view.left() + 4, view.bottom() - 24, p.fontMetrics().horizontalAdvance(t) + 12, 20);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(colors::kProgram));
            p.drawRoundedRect(badge, 4, 4);
            p.setPen(Qt::white);
            p.drawText(badge, Qt::AlignCenter, t);
        }
    }
    p.restore();
}

} // namespace lc
