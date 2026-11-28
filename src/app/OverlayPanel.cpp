#include "OverlayPanel.h"
#include "core/I18n.h"
#include "DragList.h"
#include "Text.h"
#include "Theme.h"
#include "Ui.h"
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTextCursor>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace lc {

namespace {

// Lenh dau tien (redo() khi push) bo qua vi UI da ap dung roi (giong InspectorPanel).
class OverlayCmd : public QUndoCommand {
public:
    OverlayCmd(OverlayPanel* p, int ch, std::vector<OverlayDef> before, std::vector<OverlayDef> after, const QString& text, int mergeId)
        : QUndoCommand(text), panel_(p), ch_(ch), before_(std::move(before)), after_(std::move(after)), mergeId_(mergeId) {}
    int id() const override { return mergeId_; }
    void undo() override { panel_->applyDefs(ch_, before_); }
    void redo() override { if (first_) { first_ = false; return; } panel_->applyDefs(ch_, after_); }
    bool mergeWith(const QUndoCommand* other) override {
        if (other->id() != id() || id() < 0) return false;
        after_ = static_cast<const OverlayCmd*>(other)->after_;
        return true;
    }
private:
    OverlayPanel* panel_;
    int ch_;
    std::vector<OverlayDef> before_, after_;
    int mergeId_;
    bool first_ = true;
};

// So thu tu truong cho viec gop buoc Hoan tac (sua lien tuc cung mot o = mot buoc).
enum Field { FName, FText, FSize, FOutlineWidth, FX, FY, FW, FH, FOpacity, FFade, FSpeed, FDuration, FAlertStart, FAlertFast, FAlertVolume };
constexpr int kDragMergeBase = 600000000;        // + so lan keo: moi lan keo mot buoc Hoan tac
constexpr int kTestToneId = -1000;               // tieng "Nghe thu": khong bi dong ho tu dong tat

QString typeLabel(OverlayType t) {
    switch (t) {
    case OverlayType::Title: return LC_TR("Tiêu đề");
    case OverlayType::Ticker: return LC_TR("Chữ chạy");
    case OverlayType::Countdown: return LC_TR("Đếm ngược");
    case OverlayType::Logo: return "Logo";
    }
    return "";
}

Glyph typeGlyph(OverlayType t) {
    return t == OverlayType::Title ? Glyph::TitleText : t == OverlayType::Ticker ? Glyph::Ticker
           : t == OverlayType::Countdown ? Glyph::Countdown : Glyph::Logo;
}

void setSwatch(QPushButton* b, const QColor& c) {
    QPixmap pm(16, 16);
    QPainter p(&pm);
    p.fillRect(pm.rect(), Qt::white);
    p.fillRect(0, 0, 8, 8, Qt::lightGray);
    p.fillRect(8, 8, 8, 8, Qt::lightGray);
    p.fillRect(pm.rect(), c);
    p.end();
    b->setIcon(QIcon(pm));
    b->setToolTip(QString(LC_TR("%1, đậm đặc %2%")).arg(c.name()).arg(int(std::lround(c.alphaF() * 100))));
}

// Chu ten: tu cat bot bang dau ... theo be rong.
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget* parent) : QLabel(parent) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(24);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setPen(palette().color(isEnabled() ? QPalette::WindowText : QPalette::PlaceholderText));
        p.setFont(font());
        p.drawText(rect(), Qt::AlignVCenter | Qt::AlignLeft, fontMetrics().elidedText(text(), Qt::ElideRight, width()));
    }
};

QPushButton* rowButton(Glyph g, const char* name, const QString& tip, QWidget* parent) {
    QPushButton* b = ui::iconButton(g, name, tip, parent);
    b->setFixedSize(28, 26);
    return b;
}
// Be rong dong phai vua cot hep ~220: dong dong ho nam duoi, trai dai het sang cot thung rac.

} // namespace

// ---------------- mot dong cua danh sach ----------------

class OverlayRow : public QWidget {
public:
    OverlayRow(const OverlayDef& d, QWidget* parent) : QWidget(parent), type_(d.type) {
        setObjectName("ovRow");
        auto* g = new QGridLayout(this);
        g->setContentsMargins(4, 4, 4, 4);
        g->setHorizontalSpacing(4);
        g->setVerticalSpacing(2);
        g->setColumnStretch(2, 1);
        auto* grip = new QLabel;
        grip->setPixmap(glyphIcon(Glyph::Grip).pixmap(14, 14));
        grip->setToolTip(text::ovGripTip());
        grip->setCursor(Qt::OpenHandCursor);
        const int rows = type_ == OverlayType::Countdown ? 2 : 1;
        g->addWidget(grip, 0, 0, rows, 1);
        eye = rowButton(Glyph::EyeOff, "ovEye", text::ovShow(), this);
        g->addWidget(eye, 0, 1, rows, 1);
        name = new ElidedLabel(this);
        name->setObjectName("ovRowName");
        name->setTextFormat(Qt::PlainText);
        g->addWidget(name, 0, 2);
        remove = rowButton(Glyph::Delete, "ovDel", text::ovDelete(), this);
        g->addWidget(remove, 0, 3);
        if (type_ == OverlayType::Countdown) {
            auto* line = new QHBoxLayout;
            line->setSpacing(4);
            clock = new QLabel(this);
            clock->setObjectName("ovClock");
            clock->setStyleSheet("font-size:16px;font-weight:bold");
            play = rowButton(Glyph::Play, "ovPlay", text::ovPlay(), this);
            reset = rowButton(Glyph::Reset, "ovReset", text::ovReset(), this);
            line->addWidget(clock);
            line->addWidget(play);
            line->addWidget(reset);
            line->addStretch();
            g->addLayout(line, 1, 2, 1, 2);
        }
    }

    void update(const OverlayFrame& f, bool selected, bool locked) {
        const QString type = typeLabel(f.def.type);
        const QString label = f.def.name.startsWith(type) ? f.def.name : f.def.name + "  ·  " + type;
        if (name->text() != label) { name->setText(label); name->setToolTip(label); }
        if (eyeOn_ != int(f.on)) {
            eyeOn_ = f.on;
            eye->setIcon(glyphIcon(f.on ? Glyph::Eye : Glyph::EyeOff));
            eye->setToolTip(f.on ? text::ovHide() : text::ovShow());
            eye->setProperty("on", f.on);
        }
        if (remove->isEnabled() == locked) remove->setEnabled(!locked);
        if (clock) {
            const QString t = formatCountdown(f.secondsLeft, f.def.seconds >= 3600);
            if (clock->text() != t) clock->setText(t);
            if (alerting_ != int(f.alerting)) {
                alerting_ = f.alerting;
                clock->setStyleSheet(QString("font-size:16px;font-weight:bold;color:%1").arg(f.alerting ? "#ff5a4f" : "#e6e8ee"));
                clock->setProperty("alerting", f.alerting);
            }
            if (running_ != int(f.running)) {
                running_ = f.running;
                play->setIcon(glyphIcon(f.running ? Glyph::Pause : Glyph::Play));
                play->setToolTip(f.running ? text::ovPause() : text::ovPlay());
                play->setProperty("running", f.running);
            }
            const bool can = f.running || f.secondsLeft > 0;
            ui::setWhy(play, can ? QString() : text::ovFinishedWhy());
            if (play->isEnabled() != can) play->setEnabled(can);
        }
        if (on_ != f.on || selected_ != selected) {
            on_ = f.on;
            selected_ = selected;
            setProperty("on", f.on);
            QWidget::update();
        }
    }

    QPushButton *eye = nullptr, *play = nullptr, *reset = nullptr, *remove = nullptr;
    ElidedLabel* name = nullptr;
    QLabel* clock = nullptr;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(selected_ ? QPen(QColor(colors::kAccent), 2) : QPen(QColor("#2a2f3d"), 1));
        p.setBrush(on_ ? QColor(0x8a, 0x1c, 0x1c) : QColor(0x22, 0x26, 0x31));      // dong dang hien: nen do
        p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 4, 4);
    }

private:
    OverlayType type_;
    bool on_ = false, selected_ = false;
    int eyeOn_ = -1, running_ = -1, alerting_ = -1;
};

QRectF placedRect(const QRectF& r, OverlayPlace place, QSize canvas) {
    if (canvas.isEmpty()) return r;
    const double m = std::round(canvas.height() * 0.03);
    auto along = [](double size, double margin, int where) {       // where: -1 dau, 0 giua, 1 cuoi
        if (where == 0 || size > 1 - 2 * margin) return (1 - size) / 2;
        return where < 0 ? margin : 1 - size - margin;
    };
    const int h = place == OverlayPlace::Center ? 0 : place == OverlayPlace::TopLeft || place == OverlayPlace::BottomLeft ? -1 : 1;
    const int v = place == OverlayPlace::Center ? 0 : place == OverlayPlace::TopLeft || place == OverlayPlace::TopRight ? -1 : 1;
    return QRectF(along(r.width(), m / canvas.width(), h), along(r.height(), m / canvas.height(), v), r.width(), r.height());
}

// ---------------- xem truoc ----------------

OverlayPreview::OverlayPreview(QWidget* parent) : QWidget(parent) {
    setObjectName("ovPreview");
    setMinimumHeight(110);
    setMaximumHeight(220);
    setMouseTracking(true);
    setToolTip(text::ovDragHint());
}

void OverlayPreview::show(const OverlayDef* def, int secondsLeft, QSize canvas, const QImage& background) {
    if (!def || (has_ && def_.id != def->id)) mode_ = 0;             // doi lop phu giua chung: bo lan keo
    has_ = def != nullptr;
    if (def) def_ = *def;
    secondsLeft_ = secondsLeft;
    canvas_ = canvas;
    background_ = background;
    update();
}

void OverlayPreview::setEditable(bool on) {
    editable_ = on;
    if (!on) { mode_ = 0; unsetCursor(); }
}

QRect OverlayPreview::viewRect() const {
    const QSize area = canvas_.scaled(size(), Qt::KeepAspectRatio);
    if (area.isEmpty()) return {};
    return QRect(QPoint((width() - area.width()) / 2, (height() - area.height()) / 2), area);
}

QRect OverlayPreview::boxRect() const {
    const QRect view = viewRect();
    if (!has_ || view.isEmpty()) return {};
    return QRect(view.left() + int(std::lround(def_.rect.x() * view.width())), view.top() + int(std::lround(def_.rect.y() * view.height())),
                 std::max(1, int(std::lround(def_.rect.width() * view.width()))), std::max(1, int(std::lround(def_.rect.height() * view.height()))));
}

bool OverlayPreview::clipped(const OverlayDef& def, int secondsLeft, QSize canvas) {
    if (def.type == OverlayType::Ticker || def.type == OverlayType::Logo) return false;
    const OverlayRasterKey key = overlayRasterKey(def, secondsLeft, canvas, canvas);
    if (!(key == clipKey_)) { clipKey_ = key; clipped_ = rasterizeOverlay(key).clipped; }     // chi dung lai khi noi dung doi
    return clipped_;
}

int OverlayPreview::hit(const QPoint& p) const {
    const QRect box = boxRect();
    constexpr int k = 6;
    if (!editable_ || box.isEmpty() || !box.adjusted(-k, -k, k, k).contains(p)) return 0;
    const bool small = box.width() < 3 * k || box.height() < 3 * k;     // hop nho: ben trong la di chuyen, mep ngoai la doi co
    const int in = small ? 0 : k;
    int edge = 0;
    if (p.x() <= box.left() + in) edge |= Left;
    else if (p.x() >= box.right() - in) edge |= Right;
    if (p.y() <= box.top() + in) edge |= Top;
    else if (p.y() >= box.bottom() - in) edge |= Bottom;
    return edge ? edge : Move;
}

void OverlayPreview::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    mode_ = hit(e->position().toPoint());
    if (!mode_) return;
    press_ = e->position().toPoint();
    startRect_ = def_.rect;
    startFont_ = def_.fontSize;
    moved_ = false;
    ++gesture_;
}

void OverlayPreview::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    if (!mode_) {
        const int h = hit(pos);
        if (!h) unsetCursor();
        else if (h == Move) setCursor(Qt::SizeAllCursor);
        else if (h == (Left | Top) || h == (Right | Bottom)) setCursor(Qt::SizeFDiagCursor);
        else if (h == (Right | Top) || h == (Left | Bottom)) setCursor(Qt::SizeBDiagCursor);
        else setCursor(h & (Left | Right) ? Qt::SizeHorCursor : Qt::SizeVerCursor);
        return;
    }
    if (!moved_ && (pos - press_).manhattanLength() < 4) return;      // nhich tay khi bam: khong tinh la keo
    moved_ = true;
    const QRect view = viewRect();
    if (view.isEmpty() || !onDrag) return;
    const double dx = double(pos.x() - press_.x()) / view.width(), dy = double(pos.y() - press_.y()) / view.height();
    const double snapX = 5.0 / view.width(), snapY = 5.0 / view.height();
    auto snapped = [](double v, std::initializer_list<double> targets, double tolerance) {
        for (double t : targets) if (std::abs(v - t) <= tolerance) return t;
        return v;
    };
    double l = startRect_.left(), t = startRect_.top(), r = startRect_.left() + startRect_.width(), b = startRect_.top() + startRect_.height();
    const double w = startRect_.width(), h = startRect_.height();
    const double minW = std::max(0.005, 6.0 / view.width()), minH = std::max(0.005, 6.0 / view.height());
    if (mode_ == Move) {
        double x = l + dx, y = t + dy;
        x = snapped(x, {0.0, 1.0 - w, (1.0 - w) / 2}, snapX);
        y = snapped(y, {0.0, 1.0 - h, (1.0 - h) / 2}, snapY);
        x = std::clamp(x, std::min(0.0, 1.0 - w), std::max(0.0, 1.0 - w));
        y = std::clamp(y, std::min(0.0, 1.0 - h), std::max(0.0, 1.0 - h));
        onDrag(QRectF(x, y, w, h), startFont_, gesture_);
        return;
    }
    if (mode_ & Left) l = std::clamp(snapped(l + dx, {0.0}, snapX), std::min(0.0, startRect_.left()), r - minW);
    if (mode_ & Right) r = std::clamp(snapped(r + dx, {1.0}, snapX), l + minW, std::max(1.0, startRect_.left() + w));
    if (mode_ & Top) t = std::clamp(snapped(t + dy, {0.0}, snapY), std::min(0.0, startRect_.top()), b - minH);
    if (mode_ & Bottom) b = std::clamp(snapped(b + dy, {1.0}, snapY), t + minH, std::max(1.0, startRect_.top() + h));
    int font = startFont_;
    const bool corner = (mode_ & (Left | Right)) && (mode_ & (Top | Bottom));
    if (corner && def_.type != OverlayType::Logo) {                  // keo goc: chu to / nho theo hop
        const double kw = (r - l) / w, kh = (b - t) / h;
        const double k = def_.type == OverlayType::Ticker ? kh : std::min(kw, kh);
        font = std::clamp(int(std::lround(startFont_ * k)), 4, 2000);
    }
    onDrag(QRectF(l, t, r - l, b - t), font, gesture_);
}

void OverlayPreview::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) mode_ = 0;
}

void OverlayPreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#14161c"));
    const QRect view = viewRect();
    if (view.isEmpty()) return;
    p.fillRect(view, Qt::black);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (!background_.isNull()) p.drawImage(view, background_);
    if (!has_) return;

    const QRect box = boxRect();
    const OverlayRasterKey key = overlayRasterKey(def_, secondsLeft_, canvas_, view.size());
    if (!(key == key_)) { key_ = key; bitmap_ = rasterizeOverlay(key); }

    p.save();
    p.setClipRect(view);
    p.setClipRect(box, Qt::IntersectClip);
    p.setOpacity(def_.opacity);
    if (def_.background.alpha() > 0) p.fillRect(box, def_.background);
    if (!bitmap_.size.isEmpty()) {
        double x = box.left();
        if (def_.type == OverlayType::Logo) x += overlayContentX(def_.type, def_.align, box.width(), bitmap_.size.width(), 0, 0);
        const int y = box.top() + (box.height() - bitmap_.size.height()) / 2;
        for (const QImage& tile : bitmap_.tiles) {
            if (x > box.right()) break;
            p.drawImage(QPointF(x, y), tile);
            x += tile.width();
        }
    }
    p.restore();
    p.setPen(QPen(QColor(colors::kWarn), 1, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(box.adjusted(0, 0, -1, -1));
    if (!editable_) return;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(colors::kWarn));
    for (const QPoint& c : {box.topLeft(), box.topRight(), box.bottomLeft(), box.bottomRight()}) p.drawRect(QRect(c.x() - 3, c.y() - 3, 6, 6));
}

// ---------------- panel ----------------

OverlayPanel::OverlayPanel(Project& project, RenderEngine& engine, QUndoStack* undo, QWidget* parent)
    : QWidget(parent), project_(project), engine_(engine), undo_(undo) {
    setObjectName("overlayPanel");
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(6, 4, 6, 4);
    title_ = new QLabel;
    title_->setWordWrap(true);
    v->addWidget(title_);

    auto* add = new QHBoxLayout;
    add->addWidget(new QLabel(text::ovAddLabel()));
    const struct { OverlayType type; QString tip; const char* name; } adds[4] = {
        {OverlayType::Title, text::ovAddTitle(), "ovAddTitle"}, {OverlayType::Ticker, text::ovAddTicker(), "ovAddTicker"},
        {OverlayType::Countdown, text::ovAddCountdown(), "ovAddCountdown"}, {OverlayType::Logo, text::ovAddLogo(), "ovAddLogo"}};
    for (int i = 0; i < 4; ++i) {
        const OverlayType t = adds[i].type;
        addBtn_[i] = ui::iconButton(typeGlyph(t), adds[i].name, adds[i].tip);
        add->addWidget(addBtn_[i]);
        connect(addBtn_[i], &QPushButton::clicked, this, [this, t] {
            if (t != OverlayType::Logo) { addOverlay(t); return; }
            const QString f = QFileDialog::getOpenFileName(this, LC_TR("Chọn logo"), QString(), LC_TR("Ảnh (*.png *.webp *.gif *.jpg *.jpeg *.bmp)"));
            if (!f.isEmpty()) addLogo(f);
        });
    }
    add->addStretch();
    v->addLayout(add);

    auto* split = new QSplitter(Qt::Vertical);
    split_ = split;
    split->setObjectName("ovSplit");
    split->setChildrenCollapsible(false);
    list_ = new DragList;
    list_->setObjectName("ovList");
    list_->setToolTip(text::ovListTip());
    list_->setMinimumHeight(70);
    list_->setSpacing(1);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->onReorder = [this](const std::vector<int>& rows, int before) { reorder(rows, before); };
    list_->installEventFilter(this);                       // phim Delete: CHI khi danh sach dang duoc chon
    split->addWidget(list_);

    auto* editor = new QWidget;
    auto* ev = new QVBoxLayout(editor);
    ev->setContentsMargins(0, 0, 0, 0);
    liveWarn_ = new QLabel(text::ovLiveWarning());
    liveWarn_->setObjectName("ovLiveWarn");
    liveWarn_->setWordWrap(true);
    liveWarn_->setStyleSheet("color:white;background:#8a1c1c;font-weight:bold;padding:3px 6px;border-radius:3px");
    ev->addWidget(liveWarn_);
    preview_ = new OverlayPreview;
    ev->addWidget(preview_);
    auto* places = new QHBoxLayout;
    places->setSpacing(3);
    const struct { Glyph g; QString tip; } spots[5] = {
        {Glyph::PosCenter, text::ovPlaceCenter()}, {Glyph::PosTopLeft, text::ovPlaceTopLeft()}, {Glyph::PosTopRight, text::ovPlaceTopRight()},
        {Glyph::PosBottomLeft, text::ovPlaceBottomLeft()}, {Glyph::PosBottomRight, text::ovPlaceBottomRight()}};
    places->addStretch();
    for (int i = 0; i < 5; ++i) {
        placeBtn_[i] = ui::iconButton(spots[i].g, QString("ovPlace%1").arg(i).toUtf8().constData(), spots[i].tip);
        connect(placeBtn_[i], &QPushButton::clicked, this, [this, i] { place(OverlayPlace(i)); });
        places->addWidget(placeBtn_[i]);
    }
    places->addStretch();
    ev->addLayout(places);
    warn_ = new QLabel;
    warn_->setObjectName("ovWarn");
    warn_->setWordWrap(true);
    warn_->setStyleSheet("color:#ff8a65");
    ev->addWidget(warn_);
    ev->addWidget(buildProps());
    ev->addStretch();

    auto* sa = new QScrollArea;
    sa->setObjectName("ovEditor");
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setWidget(editor);
    sa->setMinimumHeight(60);
    split->addWidget(sa);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    split->setSizes({220, 360});
    v->addWidget(split, 1);

    preview_->onDrag = [this](const QRectF& rect, int fontSize, int gesture) {
        const int id = currentId();
        if (locked_ || id < 0) return;
        std::vector<OverlayDef> after = defs_;
        for (auto& d : after)
            if (d.id == id) { d.rect = rect; d.fontSize = fontSize; }
        editDefs(std::move(after), text::ovUndoDrag(), kDragMergeBase + gesture);
        syncProps();
        refreshLive();
    };
    connect(list_, &QListWidget::currentRowChanged, this, [this] { syncProps(); refreshLive(); });
    setChannel(0);
}

QWidget* OverlayPanel::buildProps() {
    props_ = new QWidget;
    props_->setObjectName("ovProps");
    form_ = new QFormLayout(props_);
    form_->setContentsMargins(0, 0, 0, 0);
    form_->setRowWrapPolicy(QFormLayout::WrapLongRows);       // cot hep: nhan len tren o nhap
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto row = [](std::initializer_list<QWidget*> widgets) {
        auto* w = new QWidget;
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(4);
        for (QWidget* c : widgets) h->addWidget(c, 1);
        return w;
    };
    auto dspin = [](const char* name, double lo, double hi, double step, int dec) {
        auto* s = new QDoubleSpinBox;
        s->setObjectName(name);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(dec);
        s->setKeyboardTracking(false);
        return s;
    };
    auto ispin = [](const char* name, int lo, int hi, const QString& suffix) {
        auto* s = new QSpinBox;
        s->setObjectName(name);
        s->setRange(lo, hi);
        s->setSuffix(suffix);
        s->setKeyboardTracking(false);
        return s;
    };

    name_ = new QLineEdit;
    name_->setObjectName("ovName");
    name_->setMaxLength(120);
    form_->addRow(LC_TR("Tên"), name_);
    text_ = new QPlainTextEdit;
    text_->setObjectName("ovText");
    text_->setMaximumHeight(70);
    text_->setTabChangesFocus(true);
    form_->addRow(LC_TR("Nội dung"), text_);

    image_ = new QLineEdit;
    image_->setObjectName("ovImage");
    image_->setReadOnly(true);
    auto* pick = ui::iconButton(Glyph::Folder, "ovImageBtn", LC_TR("Chọn ảnh logo…"));
    imageRow_ = row({image_});
    imageRow_->layout()->addWidget(pick);
    form_->addRow(LC_TR("Ảnh"), imageRow_);

    font_ = new QFontComboBox;
    font_->setObjectName("ovFont");
    font_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    font_->setMinimumContentsLength(6);
    form_->addRow("Font", font_);
    fontSize_ = ispin("ovFontSize", 4, 2000, " px");
    bold_ = new QCheckBox(LC_TR("Đậm"));
    bold_->setObjectName("ovBold");
    italic_ = new QCheckBox(LC_TR("Nghiêng"));
    italic_->setObjectName("ovItalic");
    form_->addRow(LC_TR("Cỡ"), fontSize_);
    styleRow_ = row({bold_, italic_});
    form_->addRow(QString(), styleRow_);

    color_ = new QPushButton(LC_TR("Chữ"));
    color_->setObjectName("ovColor");
    outlineColor_ = new QPushButton(LC_TR("Viền"));
    outlineColor_->setObjectName("ovOutlineColor");
    bgColor_ = new QPushButton(LC_TR("Nền"));
    bgColor_->setObjectName("ovBgColor");
    for (QPushButton* b : {color_, outlineColor_, bgColor_}) {
        b->setProperty("swatch", true);
        b->setMinimumWidth(48);
    }
    form_->addRow(LC_TR("Màu"), row({color_, outlineColor_, bgColor_}));
    outlineWidth_ = dspin("ovOutlineWidth", 0, 50, 1, 1);
    outlineWidth_->setSuffix(" px");
    outlineWidth_->setSpecialValueText(LC_TR("không viền"));
    form_->addRow(LC_TR("Viền"), outlineWidth_);
    align_ = new QComboBox;
    align_->setObjectName("ovAlign");
    align_->addItems({LC_TR("Trái"), LC_TR("Giữa"), LC_TR("Phải")});
    form_->addRow(LC_TR("Căn"), align_);

    const char* rectName[4] = {"ovX", "ovY", "ovW", "ovH"};
    const QString prefix[4] = {"x ", "y ", LC_TR("rộng "), LC_TR("cao ")};
    for (int i = 0; i < 4; ++i) {
        rect_[i] = dspin(rectName[i], i < 2 ? -4 : 0.005, 8, 0.01, 3);
        rect_[i]->setPrefix(prefix[i]);
    }
    posRow_ = row({rect_[0], rect_[1]});
    sizeRow_ = row({rect_[2], rect_[3]});
    form_->addRow(LC_TR("Vị trí"), posRow_);
    form_->addRow(LC_TR("Kích thước"), sizeRow_);
    opacity_ = dspin("ovOpacity", 0, 1, 0.05, 2);
    form_->addRow(LC_TR("Độ đậm"), opacity_);
    fade_ = dspin("ovFade", 0, 10, 0.1, 1);
    fade_->setSuffix(" s");
    fade_->setSpecialValueText(LC_TR("cắt ngay"));
    form_->addRow(LC_TR("Mờ dần"), fade_);
    speed_ = dspin("ovSpeed", 1, 5000, 10, 0);
    speed_->setSuffix(" px/s");
    form_->addRow(LC_TR("Tốc độ"), speed_);
    minutes_ = ispin("ovMinutes", 0, kMaxCountdownSeconds / 60, LC_TR(" phút"));
    seconds_ = ispin("ovSeconds", 0, 59, LC_TR(" giây"));
    durationRow_ = row({minutes_, seconds_});
    form_->addRow(LC_TR("Thời lượng"), durationRow_);

    // ---- tieng nhac cua dem nguoc ----
    alert_ = new QCheckBox(text::ovAlertOn());
    alert_->setObjectName("ovAlertOn");
    alert_->setToolTip(text::ovAlertTip());
    alertTest_ = ui::iconButton(Glyph::Speaker, "ovAlertTest", text::ovAlertTest());
    alertRow_ = row({alert_});
    alertRow_->layout()->addWidget(alertTest_);
    form_->addRow(text::ovAlertRow(), alertRow_);
    alertStart_ = ispin("ovAlertStart", 1, kMaxAlertSeconds, text::ovAlertSecondsSuffix());
    alertStart_->setToolTip(text::ovAlertStartTip());
    form_->addRow(text::ovAlertStart(), alertStart_);
    alertFast_ = ispin("ovAlertFast", 0, kMaxAlertSeconds, text::ovAlertSecondsSuffix());
    alertFast_->setToolTip(text::ovAlertFastTip());
    alertFast_->setSpecialValueText(text::ovAlertNoFast());
    form_->addRow(text::ovAlertFast(), alertFast_);
    alertVolume_ = ispin("ovAlertVolume", 0, 100, " %");
    form_->addRow(text::ovAlertVolume(), alertVolume_);
    alertNote_ = new QLabel(text::ovAlertMuted());
    alertNote_->setObjectName("ovAlertNote");
    alertNote_->setWordWrap(true);
    alertNote_->setStyleSheet("color:#ff8a65");
    form_->addRow(alertNote_);

    // ---- noi tin hieu ----
    connect(name_, &QLineEdit::textEdited, this, [this](const QString& s) {
        editCurrent([s](OverlayDef& d) { d.name = s; }, LC_TR("Đổi tên lớp phủ"), FName);
    });
    connect(text_, &QPlainTextEdit::textChanged, this, [this] {
        const QString s = text_->toPlainText().left(kMaxOverlayText);
        editCurrent([s](OverlayDef& d) { d.text = s; }, LC_TR("Sửa chữ lớp phủ"), FText);
    });
    connect(pick, &QPushButton::clicked, this, [this] { chooseImage(); });
    connect(font_, &QComboBox::textActivated, this, [this] {      // chi khi NGUOI DUNG chon (khong phai khi hop tu nap font)
        const QString fam = font_->currentFont().family();
        editCurrent([fam](OverlayDef& d) { d.font = fam; }, LC_TR("Đổi font lớp phủ"));
    });
    connect(fontSize_, &QSpinBox::valueChanged, this, [this](int n) {
        editCurrent([n](OverlayDef& d) { d.fontSize = n; }, LC_TR("Đổi cỡ chữ"), FSize);
    });
    connect(bold_, &QCheckBox::toggled, this, [this](bool on) { editCurrent([on](OverlayDef& d) { d.bold = on; }, LC_TR("Đổi chữ đậm")); });
    connect(italic_, &QCheckBox::toggled, this, [this](bool on) { editCurrent([on](OverlayDef& d) { d.italic = on; }, LC_TR("Đổi chữ nghiêng")); });
    connect(color_, &QPushButton::clicked, this, [this] { pickColor(&OverlayDef::color, LC_TR("Màu chữ")); });
    connect(outlineColor_, &QPushButton::clicked, this, [this] { pickColor(&OverlayDef::outlineColor, LC_TR("Màu viền")); });
    connect(bgColor_, &QPushButton::clicked, this, [this] { pickColor(&OverlayDef::background, LC_TR("Màu nền (alpha 0 = không nền)")); });
    connect(outlineWidth_, &QDoubleSpinBox::valueChanged, this, [this](double x) {
        editCurrent([x](OverlayDef& d) { d.outlineWidth = float(x); }, LC_TR("Đổi độ dày viền"), FOutlineWidth);
    });
    connect(align_, &QComboBox::currentIndexChanged, this, [this](int i) {
        editCurrent([i](OverlayDef& d) { d.align = OverlayAlign(i); }, LC_TR("Đổi căn lề"));
    });
    for (int i = 0; i < 4; ++i)
        connect(rect_[i], &QDoubleSpinBox::valueChanged, this, [this, i](double x) {
            editCurrent([i, x](OverlayDef& d) {
                QRectF& r = d.rect;
                if (i == 0) r.moveLeft(x); else if (i == 1) r.moveTop(x); else if (i == 2) r.setWidth(x); else r.setHeight(x);
            }, LC_TR("Đổi vị trí/kích thước lớp phủ"), FX + i);
        });
    connect(opacity_, &QDoubleSpinBox::valueChanged, this, [this](double x) {
        editCurrent([x](OverlayDef& d) { d.opacity = float(x); }, LC_TR("Đổi độ đậm lớp phủ"), FOpacity);
    });
    connect(fade_, &QDoubleSpinBox::valueChanged, this, [this](double x) {
        editCurrent([x](OverlayDef& d) { d.fade = float(x); }, LC_TR("Đổi thời gian mờ dần"), FFade);
    });
    connect(speed_, &QDoubleSpinBox::valueChanged, this, [this](double x) {
        editCurrent([x](OverlayDef& d) { d.speed = x; }, LC_TR("Đổi tốc độ chữ chạy"), FSpeed);
    });
    auto duration = [this] {
        const int s = std::clamp(minutes_->value() * 60 + seconds_->value(), 1, kMaxCountdownSeconds);
        editCurrent([s](OverlayDef& d) { d.seconds = s; }, LC_TR("Đổi thời lượng đếm ngược"), FDuration);
    };
    connect(minutes_, &QSpinBox::valueChanged, this, duration);
    connect(seconds_, &QSpinBox::valueChanged, this, duration);
    connect(alert_, &QCheckBox::toggled, this, [this](bool on) { editCurrent([on](OverlayDef& d) { d.alert = on; }, text::ovUndoAlert()); });
    connect(alertStart_, &QSpinBox::valueChanged, this, [this](int n) {
        editCurrent([n](OverlayDef& d) { d.alertSeconds = n; d.fastSeconds = std::min(d.fastSeconds, n); }, text::ovUndoAlert(), FAlertStart);
    });
    connect(alertFast_, &QSpinBox::valueChanged, this, [this](int n) {
        editCurrent([n](OverlayDef& d) { d.fastSeconds = std::min(n, d.alertSeconds); }, text::ovUndoAlert(), FAlertFast);
    });
    connect(alertVolume_, &QSpinBox::valueChanged, this, [this](int n) {
        editCurrent([n](OverlayDef& d) { d.alertVolume = n; }, text::ovUndoAlert(), FAlertVolume);
    });
    connect(alertTest_, &QPushButton::clicked, this, [this] { testAlert(); });
    // cot hep (~220): o so duoc phep co lai thay vi sinh thanh cuon ngang
    for (auto* s : props_->findChildren<QAbstractSpinBox*>()) s->setMinimumWidth(70);
    for (QWidget* w : std::initializer_list<QWidget*>{name_, font_, fontSize_, outlineWidth_, align_, rect_[0], rect_[1], rect_[2], rect_[3],
                                                      opacity_, fade_, speed_, minutes_, seconds_, alertStart_, alertFast_, alertVolume_})
        w->installEventFilter(this);
    return props_;
}

bool OverlayPanel::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == list_ && ev->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Delete) {
        removeCurrent();
        return true;
    }
    if (ev->type() == QEvent::ShortcutOverride && obj != list_) {
        const int key = static_cast<QKeyEvent*>(ev)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) { ev->accept(); return true; }
    }
    return QWidget::eventFilter(obj, ev);
}

OverlayStack* OverlayPanel::stack() const { return &project_.channels[size_t(ch_)]->overlays; }

void OverlayPanel::preferList(bool on) {
    split_->setStretchFactor(0, on ? 4 : 2);
    split_->setStretchFactor(1, on ? 1 : 3);
    split_->setSizes(on ? QList<int>{1000, 130} : QList<int>{220, 360});
}

void OverlayPanel::setChannel(int ch) {
    if (ch < 0 || ch >= int(project_.channels.size())) return;
    ch_ = ch;
    title_->setText(text::ovTitleShort().arg(ch + 1));
    title_->setToolTip(text::ovTitleNote().arg(ch + 1));
    defs_ = stack()->defs();
    reload(defs_.empty() ? -1 : defs_.back().id);
}

int OverlayPanel::currentId() const {
    const QListWidgetItem* it = list_->currentItem();
    return it ? it->data(Qt::UserRole).toInt() : -1;
}

void OverlayPanel::selectId(int id) {
    for (int r = 0; r < list_->count(); ++r)
        if (list_->item(r)->data(Qt::UserRole).toInt() == id) { list_->setCurrentRow(r); return; }
}

QWidget* OverlayPanel::row(int id) const {
    for (int r = 0; r < list_->count(); ++r)
        if (list_->item(r)->data(Qt::UserRole).toInt() == id) return list_->itemWidget(list_->item(r));
    return nullptr;
}

const OverlayDef* OverlayPanel::current() const {
    const int id = currentId();
    for (const auto& d : defs_) if (d.id == id) return &d;
    return nullptr;
}

void OverlayPanel::reload(int select) {
    {
        QSignalBlocker b(list_);
        list_->clear();
        for (int i = int(defs_.size()) - 1; i >= 0; --i) {           // dau danh sach = tren cung
            const OverlayDef& d = defs_[size_t(i)];
            auto* it = new QListWidgetItem;
            it->setData(Qt::UserRole, d.id);
            auto* w = new OverlayRow(d, list_);
            const int id = d.id;
            connect(w->eye, &QPushButton::clicked, this, [this, id] { selectId(id); setOn(id, !stack()->isOn(id)); });
            connect(w->remove, &QPushButton::clicked, this, [this, id] { removeId(id); });
            if (w->play) {
                connect(w->play, &QPushButton::clicked, this, [this, id] { selectId(id); toggleClock(id); });
                connect(w->reset, &QPushButton::clicked, this, [this, id] { selectId(id); resetClock(id); });
            }
            it->setSizeHint(QSize(0, d.type == OverlayType::Countdown ? 64 : 40));
            list_->addItem(it);
            list_->setItemWidget(it, w);
        }
        if (list_->count() > 0) {
            list_->setCurrentRow(0);
            selectId(select);
        }
    }
    syncProps();
    refreshLive();
}

void OverlayPanel::syncProps() {
    const OverlayDef* d = current();
    props_->setEnabled(d && !locked_);
    preview_->setEditable(d && !locked_);
    for (QPushButton* b : placeBtn_) {
        ui::setWhy(b, locked_ ? text::lockedWhy() : d ? QString() : text::ovPickFirst());
        b->setEnabled(d && !locked_);
    }
    const bool countdown = d && d->type == OverlayType::Countdown;
    for (QWidget* w : std::initializer_list<QWidget*>{alertRow_, alertStart_, alertFast_, alertVolume_}) form_->setRowVisible(w, countdown);
    if (!d) { form_->setRowVisible(alertNote_, false); return; }
    syncing_ = true;                                  // dat gia tri len o nhap khong duoc tao buoc sua
    const bool text = d->type != OverlayType::Logo;
    form_->setRowVisible(text_, text);
    form_->setRowVisible(imageRow_, !text);
    form_->setRowVisible(font_, text);
    form_->setRowVisible(fontSize_, text);
    form_->setRowVisible(styleRow_, text);
    color_->setVisible(text);
    outlineColor_->setVisible(text);
    form_->setRowVisible(outlineWidth_, text);
    form_->setRowVisible(speed_, d->type == OverlayType::Ticker);
    form_->setRowVisible(durationRow_, countdown);
    form_->setRowVisible(align_, d->type != OverlayType::Ticker);
    if (auto* l = qobject_cast<QLabel*>(form_->labelForField(text_)))
        l->setText(countdown ? LC_TR("Chữ trước") : LC_TR("Nội dung"));

    if (name_->text() != d->name) name_->setText(d->name);
    if (text_->toPlainText() != d->text) {            // chi ghi khi khac: khong lam nhay con tro luc dang go
        text_->setPlainText(d->text);
        text_->moveCursor(QTextCursor::End);
    }
    image_->setText(QFileInfo(d->image).fileName());
    image_->setToolTip(d->image);
    font_->setCurrentFont(d->font.isEmpty() ? QFont() : QFont(d->font));
    fontSize_->setValue(d->fontSize);
    bold_->setChecked(d->bold);
    italic_->setChecked(d->italic);
    setSwatch(color_, d->color);
    setSwatch(outlineColor_, d->outlineColor);
    setSwatch(bgColor_, d->background);
    outlineWidth_->setValue(d->outlineWidth);
    align_->setCurrentIndex(int(d->align));
    const double r[4] = {d->rect.x(), d->rect.y(), d->rect.width(), d->rect.height()};
    for (int i = 0; i < 4; ++i) rect_[i]->setValue(r[i]);
    opacity_->setValue(d->opacity);
    fade_->setValue(d->fade);
    speed_->setValue(d->speed);
    minutes_->setValue(d->seconds / 60);
    seconds_->setValue(d->seconds % 60);
    alert_->setChecked(d->alert);
    alertStart_->setValue(d->alertSeconds);
    alertFast_->setMaximum(d->alertSeconds);
    alertFast_->setValue(d->fastSeconds);
    alertVolume_->setValue(d->alertVolume);
    syncing_ = false;
}

void OverlayPanel::refreshLive() {
    const std::vector<OverlayFrame> frames = stack()->frames(engine_.now());
    const OverlayDef* cur = current();
    OverlayFrame curFrame;
    for (int r = 0; r < list_->count(); ++r) {
        QListWidgetItem* it = list_->item(r);
        const int id = it->data(Qt::UserRole).toInt();
        auto* w = static_cast<OverlayRow*>(list_->itemWidget(it));
        for (const OverlayFrame& f : frames) {
            if (f.def.id != id) continue;
            if (w) w->update(f, cur && id == cur->id, locked_);
            if (cur && id == cur->id) curFrame = f;
        }
    }
    const bool has = cur != nullptr;
    liveWarn_->setVisible(has && curFrame.on);

    QString warning;
    if (has && cur->type == OverlayType::Logo && !QFileInfo::exists(cur->image))
        warning = LC_TR("⚠ Không tìm thấy file logo: ") + cur->image;
    else if (has && cur->type != OverlayType::Logo && !cur->font.isEmpty() && !QFontDatabase::hasFamily(cur->font))
        warning = QString(LC_TR("⚠ Máy này không có font “%1”, đang dùng font thay thế.")).arg(cur->font);
    else if (has && preview_->clipped(*cur, curFrame.secondsLeft, project_.channels[size_t(ch_)]->size))
        warning = LC_TR("⚠ Chữ không vừa hộp nên bị cắt: tăng chiều rộng/cao hoặc giảm cỡ chữ.");
    warn_->setText(warning);
    warn_->setVisible(!warning.isEmpty());

    const Channel& c = *project_.channels[size_t(ch_)];
    form_->setRowVisible(alertNote_, has && cur->type == OverlayType::Countdown && cur->alert &&
                                         (c.mute.load() || c.volume.load() <= 0 || c.ftbAmount.load() >= 1.0f));
    if (preview_->isVisible()) {
        OverlayDef shown;
        if (has) { shown = *cur; if (curFrame.alerting) shown.color = kAlertColor; }
        preview_->show(has ? &shown : nullptr, curFrame.secondsLeft, c.size, engine_.monitor(ch_).preview);
    }
}

void OverlayPanel::setLocked(bool on) {
    locked_ = on;
    list_->setReorderEnabled(!on);
    for (QPushButton* b : addBtn_) {
        ui::setWhy(b, on ? text::lockedWhy() : QString());
        b->setEnabled(!on);
    }
    syncProps();
    refreshLive();
}

// ---------------- live ----------------

void OverlayPanel::setOn(int id, bool on) {
    stack()->setOn(id, on, engine_.now());
    refreshLive();
}

void OverlayPanel::toggleClock(int id) {
    OverlayFrame f;
    if (!stack()->frame(id, engine_.now(), f)) return;
    if (f.running) stack()->countdownPause(id, engine_.now());
    else stack()->countdownStart(id, engine_.now());
    refreshLive();
}

void OverlayPanel::resetClock(int id) {
    stack()->countdownReset(id);
    refreshLive();
}

void OverlayPanel::hideAll() {
    const double now = engine_.now();
    for (const OverlayDef& d : stack()->defs()) stack()->setOn(d.id, false, now);
    refreshLive();
}

void OverlayPanel::testAlert() {
    const OverlayDef* d = current();
    if (!d || d->type != OverlayType::Countdown) return;
    AudioMixer& m = engine_.audio();
    const int id = kTestToneId - ch_;
    const float volume = float(d->alertVolume) / 100.0f;
    m.stopTones(ch_, id);
    m.playTone(ch_, id, ToneKind::Tick, volume, 0.05);
    m.playTone(ch_, id, ToneKind::Fast, volume, 0.7);
    m.playTone(ch_, id, ToneKind::Fast, volume, 1.2);
    m.playTone(ch_, id, ToneKind::Bell, volume, 1.7);
}

// ---------------- sua ----------------

void OverlayPanel::editDefs(std::vector<OverlayDef> after, const QString& text, int mergeId) {
    if (locked_) return;                                     // khoa van hanh
    std::vector<OverlayDef> before = defs_;
    if (before == after) return;
    defs_ = after;
    stack()->setDefs(after);
    undo_->push(new OverlayCmd(this, ch_, std::move(before), std::move(after), text, mergeId));
}

void OverlayPanel::editCurrent(const std::function<void(OverlayDef&)>& mutate, const QString& text, int field) {
    if (syncing_ || locked_) return;
    const int id = currentId();
    std::vector<OverlayDef> after = defs_;
    for (auto& d : after) {
        if (d.id != id) continue;
        mutate(d);
        const int mergeId = field < 0 ? -1 : 4000000 + ch_ * 100000 + (id % 1000) * 100 + field;
        editDefs(std::move(after), text, mergeId);
        syncProps();
        refreshLive();
        return;
    }
}

void OverlayPanel::place(OverlayPlace where) {
    const QSize canvas = project_.channels[size_t(ch_)]->size;
    editCurrent([where, canvas](OverlayDef& d) { d.rect = placedRect(d.rect, where, canvas); }, text::ovUndoPlace());
}

void OverlayPanel::applyDefs(int ch, const std::vector<OverlayDef>& defs) {
    if (ch < 0 || ch >= int(project_.channels.size())) return;
    project_.channels[size_t(ch)]->overlays.setDefs(defs);
    if (ch != ch_) return;
    const int keep = currentId();
    defs_ = defs;
    reload(keep);
}

int OverlayPanel::addOverlay(OverlayType type) { return add(defaultOverlay(type)); }

int OverlayPanel::addLogo(const QString& imagePath) {
    OverlayDef d = defaultOverlay(OverlayType::Logo);
    d.image = QFileInfo(imagePath).absoluteFilePath();
    return add(d);
}

int OverlayPanel::add(OverlayDef d) {
    if (locked_ || int(defs_.size()) >= kMaxOverlays) return -1;
    d.id = stack()->nextId();
    const QString base = d.name;
    auto taken = [&](const QString& n) { return std::any_of(defs_.begin(), defs_.end(), [&](const OverlayDef& e) { return e.name == n; }); };
    for (int n = 2; taken(d.name); ++n) d.name = QString("%1 %2").arg(base).arg(n);
    std::vector<OverlayDef> after = defs_;
    after.push_back(d);
    editDefs(std::move(after), LC_TR("Thêm ") + QString(typeLabel(d.type)).toLower());
    reload(d.id);
    return d.id;
}

void OverlayPanel::chooseImage() {
    const QString f = QFileDialog::getOpenFileName(this, LC_TR("Chọn logo"), QString(), LC_TR("Ảnh (*.png *.webp *.gif *.jpg *.jpeg *.bmp)"));
    if (!f.isEmpty()) editCurrent([f](OverlayDef& d) { d.image = QFileInfo(f).absoluteFilePath(); }, LC_TR("Đổi ảnh logo"));
}

void OverlayPanel::pickColor(QColor OverlayDef::*field, const QString& title) {
    const OverlayDef* d = current();
    if (!d || locked_) return;
    const int id = d->id;
    const QColor c = QColorDialog::getColor(d->*field, this, title, QColorDialog::ShowAlphaChannel);
    if (!c.isValid() || currentId() != id) return;
    editCurrent([field, c](OverlayDef& e) { e.*field = c; }, LC_TR("Đổi màu lớp phủ"));
}

void OverlayPanel::removeCurrent() { removeId(currentId()); }

void OverlayPanel::removeId(int id) {
    if (id < 0 || locked_) return;
    int row = -1;
    for (int r = 0; r < list_->count(); ++r)
        if (list_->item(r)->data(Qt::UserRole).toInt() == id) row = r;
    if (row < 0) return;
    const int keep = currentId();
    std::vector<OverlayDef> after = defs_;
    after.erase(std::remove_if(after.begin(), after.end(), [id](const OverlayDef& d) { return d.id == id; }), after.end());
    editDefs(std::move(after), text::ovUndoDelete());
    reload(keep == id ? -1 : keep);
    if (keep == id && list_->count() > 0) list_->setCurrentRow(std::min(row, list_->count() - 1));
}

void OverlayPanel::moveCurrent(int dir) {
    const int id = currentId();
    if (id < 0 || locked_) return;
    std::vector<OverlayDef> after = defs_;
    for (int i = 0; i < int(after.size()); ++i) {
        if (after[size_t(i)].id != id) continue;
        const int j = i + dir;
        if (j < 0 || j >= int(after.size())) return;
        std::swap(after[size_t(i)], after[size_t(j)]);
        editDefs(std::move(after), text::ovUndoReorder());
        reload(id);
        return;
    }
}

void OverlayPanel::reorder(const std::vector<int>& rows, int before) {
    if (locked_ || rows.empty()) return;
    std::vector<OverlayDef> shown(defs_.rbegin(), defs_.rend());     // theo thu tu danh sach (tren cung truoc)
    if (rows.front() < 0 || rows.back() >= int(shown.size())) return;
    const int keep = shown[size_t(rows.front())].id;
    shown = movedRows(shown, rows, before);
    editDefs(std::vector<OverlayDef>(shown.rbegin(), shown.rend()), text::ovUndoReorder());
    reload(keep);
}

} // namespace lc
