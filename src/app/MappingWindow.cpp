#include "MappingWindow.h"
#include "core/I18n.h"
#include "InspectorPanel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace lc {

namespace mapping {

QRect toPixels(const QRectF& c, QSize s) {
    return QRect(int(std::lround(c.x() * s.width())), int(std::lround(c.y() * s.height())),
                 int(std::lround(c.width() * s.width())), int(std::lround(c.height() * s.height())));
}

QRectF toCrop(const QRect& px, QSize s) {
    return QRectF(double(px.x()) / s.width(), double(px.y()) / s.height(),
                  double(px.width()) / s.width(), double(px.height()) / s.height());
}

// Do doi nho nhat dua mot trong cac `edges` trung voi mot moc trong `marks` (0 neu khong moc nao trong pham vi snap).
static int snapDelta(std::initializer_list<int> edges, const std::vector<int>& marks, int snap) {
    int best = 0, bestAbs = snap + 1;
    for (int e : edges)
        for (int m : marks)
            if (std::abs(m - e) < bestAbs) { bestAbs = std::abs(m - e); best = m - e; }
    return best;
}

QRect drag(const QRect& start, int edges, int dx, int dy, QSize canvas, const std::vector<QRect>& others, int snap) {
    const int W = canvas.width(), H = canvas.height();
    std::vector<int> xs{0, W}, ys{0, H};
    for (const QRect& o : others) {
        xs.push_back(o.x()); xs.push_back(o.x() + o.width());
        ys.push_back(o.y()); ys.push_back(o.y() + o.height());
    }
    int l = start.x(), t = start.y(), r = start.x() + start.width(), b = start.y() + start.height();

    if (edges & Move) {
        const int w = std::clamp(start.width(), 1, W), h = std::clamp(start.height(), 1, H);
        l += dx; t += dy;
        if (snap > 0) {
            l += snapDelta({l, l + w}, xs, snap);
            t += snapDelta({t, t + h}, ys, snap);
        }
        return QRect(std::clamp(l, 0, W - w), std::clamp(t, 0, H - h), w, h);
    }
    auto moved = [snap](int v, const std::vector<int>& marks) { return snap > 0 ? v + snapDelta({v}, marks, snap) : v; };
    if (edges & Left)   l = std::clamp(moved(l + dx, xs), 0, std::min(r, W) - 1);
    if (edges & Right)  r = std::clamp(moved(r + dx, xs), std::max(l, 0) + 1, W);
    if (edges & Top)    t = std::clamp(moved(t + dy, ys), 0, std::min(b, H) - 1);
    if (edges & Bottom) b = std::clamp(moved(b + dy, ys), std::max(t, 0) + 1, H);
    return QRect(l, t, r - l, b - t);
}

QSize outputPixels(const OutputConfig& c) {
    if (c.kind == OutputKind::Ndi) return c.size;
    if (!c.area.isEmpty()) return c.area.size();                   // chi dung mot vung LED trong tin hieu
    const QList<QScreen*> screens = QGuiApplication::screens();
    if (c.fullscreen) {
        QScreen* sc = nullptr;
        if (!c.screenName.isEmpty()) {
            for (QScreen* s : screens) if (s->name() == c.screenName) sc = s;
        } else if (c.screenIndex >= 0 && c.screenIndex < screens.size()) {
            sc = screens[c.screenIndex];
        }
        if (!sc) return c.size;                                    // man hinh khong con: chi biet kich thuoc cau hinh
        return sc->geometry().size() * sc->devicePixelRatio();
    }
    const QScreen* primary = QGuiApplication::primaryScreen();
    return c.size * (primary ? primary->devicePixelRatio() : 1.0);
}

}   // namespace mapping

// ---------------- Canvas ----------------

namespace {
constexpr double kHandle = 7;                          // ban kinh bat canh/goc (diem anh widget)
constexpr double kSnapScreen = 8;                      // khoang bat dinh tren man hinh
constexpr int kDragMerge = InspectorPanel::kMappingMergeBase;
constexpr int kNudgeMerge = InspectorPanel::kMappingMergeBase + 50000000;
const QColor kColors[] = {QColor("#4fc3f7"), QColor("#ffb74d"), QColor("#81c784"), QColor("#ba68c8"),
                          QColor("#fff176"), QColor("#f06292"), QColor("#4db6ac"), QColor("#a1887f")};
static QString editText() { return LC_TR("Mapping: sửa vùng output"); }
}

MappingCanvas::MappingCanvas(InspectorPanel& inspector, QWidget* parent) : QWidget(parent), inspector_(inspector) {
    setObjectName("mappingCanvas");
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

void MappingCanvas::setChannel(int channel, QSize size) {
    endDrag();
    channel_ = channel;
    size_ = size.isEmpty() ? QSize(1920, 1080) : size;
    image_ = QImage();
    const auto outs = outputs();
    select(outs.empty() ? -1 : outs.front());
    update();
}

void MappingCanvas::select(int output) {
    const auto outs = outputs();
    const int next = std::find(outs.begin(), outs.end(), output) != outs.end() ? output : -1;
    if (next == selected_) { update(); return; }
    endDrag();
    selected_ = next;
    update();
    emit selectionChanged(selected_);
}

void MappingCanvas::setReadOnly(bool on) {
    if (on) endDrag();
    readOnly_ = on;
    unsetCursor();
    update();
}

std::vector<int> MappingCanvas::outputs() const {
    std::vector<int> v;
    for (int i = 0; i < inspector_.outputCount(); ++i)
        if (inspector_.outputConfig(i).channel == channel_) v.push_back(i);
    return v;
}

QRect MappingCanvas::cropPx(int output) const { return mapping::toPixels(inspector_.outputConfig(output).crop, size_); }

QRectF MappingCanvas::canvasRect() const {
    const QRectF area = QRectF(rect()).adjusted(12, 12, -12, -12);
    const double k = std::max(0.0001, std::min(area.width() / size_.width(), area.height() / size_.height()));
    const QSizeF s(size_.width() * k, size_.height() * k);
    return QRectF(area.center() - QPointF(s.width() / 2, s.height() / 2), s);
}

QPointF MappingCanvas::toWidget(const QPointF& p) const {
    const QRectF c = canvasRect();
    return c.topLeft() + QPointF(p.x() * c.width() / size_.width(), p.y() * c.height() / size_.height());
}

QRectF MappingCanvas::widgetRect(const QRect& px) const {
    return QRectF(toWidget(QPointF(px.x(), px.y())), toWidget(QPointF(px.x() + px.width(), px.y() + px.height())));
}

int MappingCanvas::hitEdges(const QRectF& r, const QPointF& p) const {
    if (!r.adjusted(-kHandle, -kHandle, kHandle, kHandle).contains(p)) return mapping::None;
    int e = mapping::None;
    // vung nho: khong de hai canh doi dien cung bat -> chon canh gan hon
    const double dl = std::abs(p.x() - r.left()), dr = std::abs(p.x() - r.right());
    const double dt = std::abs(p.y() - r.top()), db = std::abs(p.y() - r.bottom());
    if (std::min(dl, dr) <= kHandle) e |= dl < dr ? mapping::Left : mapping::Right;
    if (std::min(dt, db) <= kHandle) e |= dt < db ? mapping::Top : mapping::Bottom;
    if (e == mapping::None && r.contains(p)) e = mapping::Move;
    return e;
}

// Output dang chon duoc uu tien; neu khong, lay vung NHO NHAT chua diem (de bam duoc vung nam long trong vung lon).
int MappingCanvas::outputAt(const QPointF& p) const {
    if (selected_ >= 0 && hitEdges(widgetRect(cropPx(selected_)), p) != mapping::None) return selected_;
    int best = -1;
    double area = 0;
    for (int i : outputs()) {
        const QRectF r = widgetRect(cropPx(i));
        if (hitEdges(r, p) == mapping::None) continue;
        if (best < 0 || r.width() * r.height() <= area) { best = i; area = r.width() * r.height(); }
    }
    return best;
}

void MappingCanvas::paintEvent(QPaintEvent*) {
    QPainter g(this);
    g.fillRect(rect(), QColor("#14161c"));
    const QRectF c = canvasRect();
    g.fillRect(c, Qt::black);
    if (!image_.isNull()) {
        g.setRenderHint(QPainter::SmoothPixmapTransform);
        g.drawImage(c, image_);
    }
    g.setPen(QPen(QColor("#5a6072"), 1));
    g.drawRect(c);

    auto outs = outputs();
    std::stable_partition(outs.begin(), outs.end(), [this](int i) { return i != selected_; });   // ve vung dang chon sau cung
    for (int i : outs) {
        const QRect px = cropPx(i);
        const QRectF r = widgetRect(px);
        const bool sel = i == selected_;
        QColor col = kColors[i % int(std::size(kColors))];
        QColor fill = col;
        fill.setAlpha(sel ? 60 : 30);
        g.fillRect(r, fill);
        g.setPen(QPen(col, sel ? 2.5 : 1.2, sel ? Qt::SolidLine : Qt::DashLine));
        g.setBrush(Qt::NoBrush);
        g.drawRect(r);
        const QString text = QString("%1\n%2×%3 @ %4,%5").arg(inspector_.outputConfig(i).name)
                                 .arg(px.width()).arg(px.height()).arg(px.x()).arg(px.y());
        const QRectF tr = g.boundingRect(r.adjusted(6, 4, -4, -4), Qt::AlignTop | Qt::AlignLeft, text);
        g.fillRect(tr.adjusted(-3, -2, 3, 2), QColor(0, 0, 0, 170));
        g.setPen(col);
        g.drawText(tr, Qt::AlignTop | Qt::AlignLeft, text);
        if (sel && !readOnly_) {
            g.setPen(QPen(Qt::black, 1));
            g.setBrush(col);
            for (const QPointF& h : {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight(), QPointF(r.center().x(), r.top()),
                                     QPointF(r.center().x(), r.bottom()), QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y())})
                g.drawRect(QRectF(h - QPointF(4, 4), QSizeF(8, 8)));
        }
    }
    if (outs.empty()) {
        g.setPen(QColor("#8a90a2"));
        g.drawText(c, Qt::AlignCenter, LC_TR("Kênh này chưa có Output nào."));
    }
}

void MappingCanvas::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    setFocus(Qt::MouseFocusReason);
    const int hit = outputAt(e->position());
    if (hit < 0) return;                                   // bam ra ngoai: giu lua chon
    select(hit);
    if (readOnly_) return;
    dragEdges_ = hitEdges(widgetRect(cropPx(hit)), e->position());
    dragFrom_ = e->position();
    dragStart_ = cropPx(hit);
    dragStartCrop_ = inspector_.outputConfig(hit).crop;
    static int seq = 0;
    dragMergeId_ = kDragMerge + (++seq % 40000000);        // moi lan keo = mot buoc Hoan tac rieng
}

void MappingCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (dragEdges_ == mapping::None) {
        if (readOnly_) return;
        const int hit = outputAt(e->position());
        const int k = hit < 0 ? mapping::None : hitEdges(widgetRect(cropPx(hit)), e->position());
        if (k == mapping::None) unsetCursor();
        else if (k == mapping::Move) setCursor(Qt::SizeAllCursor);
        else if (k == (mapping::Left | mapping::Top) || k == (mapping::Right | mapping::Bottom)) setCursor(Qt::SizeFDiagCursor);
        else if (k == (mapping::Right | mapping::Top) || k == (mapping::Left | mapping::Bottom)) setCursor(Qt::SizeBDiagCursor);
        else if (k & (mapping::Left | mapping::Right)) setCursor(Qt::SizeHorCursor);
        else setCursor(Qt::SizeVerCursor);
        return;
    }
    if (selected_ < 0) return;
    const double k = canvasRect().width() / size_.width();            // diem anh widget / diem anh Channel
    const QPointF d = (e->position() - dragFrom_) / k;
    std::vector<QRect> others;
    for (int i : outputs()) if (i != selected_) others.push_back(cropPx(i));
    const int snap = snap_ && !(e->modifiers() & Qt::AltModifier) ? std::max(1, int(std::lround(kSnapScreen / k))) : 0;
    const QRect r = mapping::drag(dragStart_, dragEdges_, int(std::lround(d.x())), int(std::lround(d.y())), size_, others, snap);
    if (r == cropPx(selected_)) return;
    // ve lai dung vi tri ban dau: tra lai CHINH XAC gia tri cu (khong lam tron qua diem anh)
    inspector_.setOutputCrop(selected_, r == dragStart_ ? dragStartCrop_ : mapping::toCrop(r, size_), editText(), dragMergeId_);
}

void MappingCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) endDrag();
}

void MappingCanvas::endDrag() { dragEdges_ = mapping::None; }

void MappingCanvas::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape && dragging() && selected_ >= 0) {
        inspector_.setOutputCrop(selected_, dragStartCrop_, editText(), dragMergeId_);
        endDrag();
        return;
    }
    int dx = 0, dy = 0;
    switch (e->key()) {
    case Qt::Key_Left:  dx = -1; break;
    case Qt::Key_Right: dx = 1; break;
    case Qt::Key_Up:    dy = -1; break;
    case Qt::Key_Down:  dy = 1; break;
    default: QWidget::keyPressEvent(e); return;
    }
    if (readOnly_ || selected_ < 0 || dragging()) return;
    const int step = e->modifiers() & Qt::ShiftModifier ? 10 : 1;
    const QRect now = cropPx(selected_);
    const QRect r = mapping::drag(now, mapping::Move, dx * step, dy * step, size_, {}, 0);
    if (r != now) inspector_.setOutputCrop(selected_, mapping::toCrop(r, size_), editText(), kNudgeMerge + selected_);
}

// ---------------- Cua so ----------------

namespace {
// Enter trong o so phai XAC NHAN so vua go, khong kich hoat phim tat CUT cua cua so chinh.
class PixelSpin : public QSpinBox {
protected:
    bool event(QEvent* e) override {
        if (e->type() == QEvent::ShortcutOverride) {
            const int k = static_cast<QKeyEvent*>(e)->key();
            if (k == Qt::Key_Return || k == Qt::Key_Enter) { e->accept(); return true; }
        }
        return QSpinBox::event(e);
    }
};
}

MappingWindow::MappingWindow(Project& project, RenderEngine& engine, InspectorPanel& inspector, QWidget* parent)
    : QWidget(parent, Qt::Tool), project_(project), engine_(engine), inspector_(inspector) {
    setObjectName("mappingWindow");
    setWindowTitle(LC_TR("Mapping trực quan"));
    auto* v = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    chanCombo_ = new QComboBox;
    chanCombo_->setObjectName("mapChannel");
    for (size_t i = 0; i < project_.channels.size(); ++i)
        chanCombo_->addItem(QString(LC_TR("Kênh %1: %2  (%3×%4)")).arg(i + 1).arg(project_.channels[i]->name)
                                .arg(project_.channels[i]->size.width()).arg(project_.channels[i]->size.height()));
    top->addWidget(chanCombo_);
    outCombo_ = new QComboBox;
    outCombo_->setObjectName("mapOutput");
    top->addWidget(outCombo_, 1);
    snapBox_ = new QCheckBox(LC_TR("Bắt dính"));
    snapBox_->setChecked(true);
    snapBox_->setToolTip(LC_TR("Bắt dính vào mép canvas và mép Output khác. Giữ Alt khi kéo để tạm tắt."));
    top->addWidget(snapBox_);
    v->addLayout(top);

    canvas_ = new MappingCanvas(inspector_);
    v->addWidget(canvas_, 1);

    auto* row = new QHBoxLayout;
    const QString names[4] = {"X ", "Y ", LC_TR("Rộng "), LC_TR("Cao ")};
    const char* ids[4] = {"mapX", "mapY", "mapW", "mapH"};
    for (int k = 0; k < 4; ++k) {
        spin_[k] = new PixelSpin;
        spin_[k]->setObjectName(ids[k]);
        spin_[k]->setPrefix(names[k]);
        spin_[k]->setSuffix(" px");
        spin_[k]->setKeyboardTracking(false);              // khong day tung chu so dang go ra man hinh LED
        connect(spin_[k], &QSpinBox::valueChanged, this, [this, k](int val) { applySpin(k, val); });
        row->addWidget(spin_[k]);
    }
    oneToOne_ = new QPushButton(LC_TR("Khớp 1:1"));
    oneToOne_->setObjectName("mapOneToOne");
    oneToOne_->setToolTip(LC_TR("Đặt cỡ vùng bằng số điểm ảnh thật của Output (giữ nguyên vị trí nếu còn chỗ)"));
    full_ = new QPushButton(LC_TR("Cả khung"));
    full_->setObjectName("mapFull");
    row->addWidget(oneToOne_);
    row->addWidget(full_);
    v->addLayout(row);

    info_ = new QLabel;
    info_->setObjectName("mapInfo");
    info_->setWordWrap(true);
    v->addWidget(info_);
    note_ = new QLabel;
    note_->setObjectName("mapNote");
    note_->setWordWrap(true);
    v->addWidget(note_);

    connect(chanCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        canvas_->setChannel(channel(), channelSize());
        rebuildOutputs();
    });
    connect(outCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (outCombo_->currentIndex() >= 0) canvas_->select(outCombo_->currentData().toInt());
    });
    connect(snapBox_, &QCheckBox::toggled, this, [this](bool on) { canvas_->setSnap(on); });
    connect(canvas_, &MappingCanvas::selectionChanged, this, [this](int) { sync(); });
    connect(&inspector_, &InspectorPanel::outputConfigChanged, this, [this](int) { sync(); canvas_->update(); });
    connect(oneToOne_, &QPushButton::clicked, this, [this] {
        const int i = canvas_->selected();
        if (i < 0) return;
        const OutputConfig& c = inspector_.outputConfig(i);
        QSize s = mapping::outputPixels(c);
        if ((c.rotation / 90) % 2) s.transpose();
        const QSize ch = channelSize();
        s = s.boundedTo(ch);
        const QRect now = mapping::toPixels(c.crop, ch);
        const QRect r(std::clamp(now.x(), 0, ch.width() - s.width()), std::clamp(now.y(), 0, ch.height() - s.height()), s.width(), s.height());
        inspector_.setOutputCrop(i, mapping::toCrop(r, ch), LC_TR("Mapping: khớp 1:1"), -1);
    });
    connect(full_, &QPushButton::clicked, this, [this] {
        const int i = canvas_->selected();
        if (i >= 0 && inspector_.outputConfig(i).crop != QRectF(0, 0, 1, 1))
            inspector_.setOutputCrop(i, QRectF(0, 0, 1, 1), LC_TR("Mapping: cả khung"), -1);
    });

    imageTimer_.setInterval(100);
    connect(&imageTimer_, &QTimer::timeout, this, [this] { canvas_->setProgramImage(engine_.monitor(channel()).program); });

    canvas_->setChannel(channel(), channelSize());
    rebuildOutputs();
    resize(820, 600);
}

int MappingWindow::channel() const { return std::max(0, chanCombo_->currentIndex()); }

QSize MappingWindow::channelSize() const {
    const int c = channel();
    return c < int(project_.channels.size()) ? project_.channels[c]->size : QSize(1920, 1080);
}

void MappingWindow::showChannel(int c) {
    if (c >= 0 && c < chanCombo_->count()) chanCombo_->setCurrentIndex(c);
}

void MappingWindow::showEvent(QShowEvent* e) {
    rebuildOutputs();
    imageTimer_.start();
    QWidget::showEvent(e);
}

void MappingWindow::hideEvent(QHideEvent* e) {
    imageTimer_.stop();
    QWidget::hideEvent(e);
}

void MappingWindow::rebuildOutputs() {
    {
        QSignalBlocker b(outCombo_);
        outCombo_->clear();
        for (int i = 0; i < inspector_.outputCount(); ++i)
            if (inspector_.outputConfig(i).channel == channel()) outCombo_->addItem(inspector_.outputConfig(i).name, i);
    }
    if (canvas_->selected() < 0 && outCombo_->count() > 0) canvas_->select(outCombo_->itemData(0).toInt());
    sync();
}

void MappingWindow::sync() {
    const int i = canvas_->selected();
    const bool has = i >= 0;
    const QSize ch = channelSize();
    for (int k = 0; k < 4; ++k) spin_[k]->setEnabled(has && !locked_);
    oneToOne_->setEnabled(has && !locked_);
    full_->setEnabled(has && !locked_);
    note_->setText(locked_ ? LC_TR("🔒 Đang KHOÁ VẬN HÀNH: chỉ xem, không sửa được.")
                           : LC_TR("Thay đổi tác động NGAY lên Output đang phát. Kéo để di chuyển, kéo mép/góc để đổi cỡ; "
                             "mũi tên nhích 1 px (Shift: 10 px); Esc huỷ lần kéo; Ctrl+Z hoàn tác."));
    note_->setStyleSheet(locked_ ? "color:#ff6b6b;font-weight:bold" : "color:#8a90a2");
    {
        QSignalBlocker b(outCombo_);
        outCombo_->setCurrentIndex(outCombo_->findData(i));
    }
    if (!has) {
        info_->clear();
        return;
    }
    const OutputConfig& c = inspector_.outputConfig(i);
    const QRect r = mapping::toPixels(c.crop, ch);
    const int values[4] = {r.x(), r.y(), r.width(), r.height()};
    const int lo[4] = {0, 0, 1, 1};
    const int hi[4] = {ch.width() - 1, ch.height() - 1, ch.width(), ch.height()};
    for (int k = 0; k < 4; ++k) {
        QSignalBlocker b(spin_[k]);
        spin_[k]->setRange(std::min(lo[k], values[k]), std::max(hi[k], values[k]));   // project viet tay co the vuot canvas
        spin_[k]->setValue(values[k]);
    }

    const QSize px = mapping::outputPixels(c);
    QSize shown = r.size();
    if ((c.rotation / 90) % 2) shown.transpose();
    QString verdict;
    if (shown == px) verdict = LC_TR("<span style='color:#81c784'>1:1 đúng điểm ảnh</span>");
    else if (c.scale == ScaleMode::Pixel) verdict = LC_TR("<span style='color:#ffb74d'>1:1 nhưng khác cỡ: bị cắt bớt hoặc có viền đen</span>");
    else verdict = QString(LC_TR("<span style='color:#ffb74d'>bị co giãn (kiểu %1)</span>"))
                       .arg(c.scale == ScaleMode::Fit ? "Fit" : c.scale == ScaleMode::Fill ? "Fill" : "Stretch");
    info_->setText(QString(LC_TR("Vùng %1×%2 px → Output %3×%4 px: %5")).arg(shown.width()).arg(shown.height())
                       .arg(px.width()).arg(px.height()).arg(verdict));
}

void MappingWindow::applySpin(int field, int value) {
    const int i = canvas_->selected();
    if (i < 0) return;
    const QSize ch = channelSize();
    QRect r = mapping::toPixels(inspector_.outputConfig(i).crop, ch);
    if (field == 0) r.moveLeft(std::clamp(value, 0, std::max(0, ch.width() - r.width())));
    else if (field == 1) r.moveTop(std::clamp(value, 0, std::max(0, ch.height() - r.height())));
    else if (field == 2) r.setWidth(std::clamp(value, 1, std::max(1, ch.width() - r.x())));
    else r.setHeight(std::clamp(value, 1, std::max(1, ch.height() - r.y())));
    if (r != mapping::toPixels(inspector_.outputConfig(i).crop, ch))
        inspector_.setOutputCrop(i, mapping::toCrop(r, ch), LC_TR("Mapping: sửa vùng output"), 960000000 + i * 4 + field);
    else
        sync();                                            // gia tri bi gioi han: hien lai so that
}

void MappingWindow::setLocked(bool on) {
    locked_ = on;
    canvas_->setReadOnly(on);
    sync();
}

}   // namespace lc
