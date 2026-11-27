#include "OverlayPanel.h"
#include "Theme.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
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
enum Field { FName, FText, FSize, FOutlineWidth, FX, FY, FW, FH, FOpacity, FFade, FSpeed, FDuration };

const char* typeLabel(OverlayType t) {
    switch (t) {
    case OverlayType::Title: return "Tiêu đề";
    case OverlayType::Ticker: return "Chữ chạy";
    case OverlayType::Countdown: return "Đếm ngược";
    case OverlayType::Logo: return "Logo";
    }
    return "";
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
    b->setToolTip(QString("%1, đậm đặc %2%").arg(c.name()).arg(int(std::lround(c.alphaF() * 100))));
}

} // namespace

// ---------------- xem truoc ----------------

OverlayPreview::OverlayPreview(QWidget* parent) : QWidget(parent) {
    setObjectName("ovPreview");
    setMinimumHeight(110);
    setMaximumHeight(180);
}

void OverlayPreview::show(const OverlayDef* def, int secondsLeft, QSize canvas, const QImage& background) {
    has_ = def != nullptr;
    if (def) def_ = *def;
    secondsLeft_ = secondsLeft;
    canvas_ = canvas;
    background_ = background;
    update();
}

bool OverlayPreview::clipped(const OverlayDef& def, int secondsLeft, QSize canvas) {
    if (def.type == OverlayType::Ticker || def.type == OverlayType::Logo) return false;
    const OverlayRasterKey key = overlayRasterKey(def, secondsLeft, canvas, canvas);
    if (!(key == clipKey_)) { clipKey_ = key; clipped_ = rasterizeOverlay(key).clipped; }     // chi dung lai khi noi dung doi
    return clipped_;
}

void OverlayPreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#14161c"));
    const QSize area = canvas_.scaled(size(), Qt::KeepAspectRatio);
    if (area.isEmpty()) return;
    const QRect view(QPoint((width() - area.width()) / 2, (height() - area.height()) / 2), area);
    p.fillRect(view, Qt::black);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (!background_.isNull()) p.drawImage(view, background_);
    if (!has_) return;

    const QRect box(view.left() + int(std::lround(def_.rect.x() * view.width())), view.top() + int(std::lround(def_.rect.y() * view.height())),
                    std::max(1, int(std::lround(def_.rect.width() * view.width()))), std::max(1, int(std::lround(def_.rect.height() * view.height()))));
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
}

// ---------------- panel ----------------

OverlayPanel::OverlayPanel(Project& project, RenderEngine& engine, QUndoStack* undo, QWidget* parent)
    : QWidget(parent), project_(project), engine_(engine), undo_(undo) {
    setObjectName("overlayPanel");
    auto* v = new QVBoxLayout(this);
    title_ = new QLabel;
    title_->setWordWrap(true);
    v->addWidget(title_);

    auto* add = new QHBoxLayout;
    const struct { OverlayType type; const char* label; const char* name; } adds[4] = {
        {OverlayType::Title, "+ Tiêu đề", "ovAddTitle"}, {OverlayType::Ticker, "+ Chữ chạy", "ovAddTicker"},
        {OverlayType::Countdown, "+ Đếm ngược", "ovAddCountdown"}, {OverlayType::Logo, "+ Logo…", "ovAddLogo"}};
    for (int i = 0; i < 4; ++i) {
        addBtn_[i] = new QPushButton(adds[i].label);
        addBtn_[i]->setObjectName(adds[i].name);
        add->addWidget(addBtn_[i]);
        const OverlayType t = adds[i].type;
        connect(addBtn_[i], &QPushButton::clicked, this, [this, t] {
            if (t != OverlayType::Logo) { addOverlay(t); return; }
            const QString f = QFileDialog::getOpenFileName(this, "Chọn logo", QString(), "Ảnh (*.png *.webp *.gif *.jpg *.jpeg *.bmp)");
            if (!f.isEmpty()) addLogo(f);
        });
    }
    v->addLayout(add);

    list_ = new QListWidget;
    list_->setObjectName("ovList");
    list_->setToolTip("Đầu danh sách = lớp trên cùng. Tick = đang hiện trên Program.");
    list_->setMinimumHeight(60);
    list_->setMaximumHeight(96);
    v->addWidget(list_);

    toggle_ = new QPushButton("HIỆN lên Program");
    toggle_->setObjectName("ovToggle");
    toggle_->setMinimumHeight(30);
    v->addWidget(toggle_);

    clockRow_ = new QWidget;
    auto* cl = new QHBoxLayout(clockRow_);
    cl->setContentsMargins(0, 0, 0, 0);
    clock_ = new QLabel;
    clock_->setObjectName("ovClock");
    clock_->setStyleSheet("font-size:18px;font-weight:bold");
    startBtn_ = new QPushButton("Bắt đầu");
    startBtn_->setObjectName("ovStart");
    pauseBtn_ = new QPushButton("Tạm dừng");
    pauseBtn_->setObjectName("ovPause");
    resetBtn_ = new QPushButton("Đặt lại");
    resetBtn_->setObjectName("ovReset");
    cl->addWidget(clock_, 1);
    cl->addWidget(startBtn_);
    cl->addWidget(pauseBtn_);
    cl->addWidget(resetBtn_);
    v->addWidget(clockRow_);

    auto* rest = new QWidget;
    auto* rv = new QVBoxLayout(rest);
    rv->setContentsMargins(0, 0, 0, 0);
    auto* ops = new QHBoxLayout;
    upBtn_ = new QPushButton("Lên");
    upBtn_->setObjectName("ovUp");
    downBtn_ = new QPushButton("Xuống");
    downBtn_->setObjectName("ovDown");
    delBtn_ = new QPushButton("Xoá");
    delBtn_->setObjectName("ovDelete");
    ops->addWidget(upBtn_);
    ops->addWidget(downBtn_);
    ops->addWidget(delBtn_);
    rv->addLayout(ops);

    preview_ = new OverlayPreview;
    rv->addWidget(preview_);
    warn_ = new QLabel;
    warn_->setObjectName("ovWarn");
    warn_->setWordWrap(true);
    warn_->setStyleSheet("color:#ff8a65");
    rv->addWidget(warn_);
    rv->addWidget(buildProps());
    rv->addStretch();

    auto* sa = new QScrollArea;
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setWidget(rest);
    v->addWidget(sa, 1);

    connect(list_, &QListWidget::currentRowChanged, this, [this] { syncProps(); refreshLive(); });
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        setOn(it->data(Qt::UserRole).toInt(), it->checkState() == Qt::Checked);
    });
    connect(toggle_, &QPushButton::clicked, this, [this] {
        if (const OverlayDef* d = current()) setOn(d->id, !stack()->isOn(d->id));
    });
    connect(startBtn_, &QPushButton::clicked, this, [this] {
        if (const OverlayDef* d = current()) { stack()->countdownStart(d->id, engine_.now()); refreshLive(); }
    });
    connect(pauseBtn_, &QPushButton::clicked, this, [this] {
        if (const OverlayDef* d = current()) { stack()->countdownPause(d->id, engine_.now()); refreshLive(); }
    });
    connect(resetBtn_, &QPushButton::clicked, this, [this] {
        if (const OverlayDef* d = current()) { stack()->countdownReset(d->id); refreshLive(); }
    });
    connect(upBtn_, &QPushButton::clicked, this, [this] { moveCurrent(+1); });
    connect(downBtn_, &QPushButton::clicked, this, [this] { moveCurrent(-1); });
    connect(delBtn_, &QPushButton::clicked, this, [this] { removeCurrent(); });
    setChannel(0);
}

QWidget* OverlayPanel::buildProps() {
    props_ = new QWidget;
    props_->setObjectName("ovProps");
    form_ = new QFormLayout(props_);
    form_->setRowWrapPolicy(QFormLayout::DontWrapRows);

    auto row = [](std::initializer_list<QWidget*> widgets) {
        auto* w = new QWidget;
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
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

    name_ = new QLineEdit;
    name_->setObjectName("ovName");
    name_->setMaxLength(120);
    form_->addRow("Tên", name_);
    text_ = new QPlainTextEdit;
    text_->setObjectName("ovText");
    text_->setMaximumHeight(70);
    text_->setTabChangesFocus(true);
    form_->addRow("Nội dung", text_);

    image_ = new QLineEdit;
    image_->setObjectName("ovImage");
    image_->setReadOnly(true);
    auto* pick = new QPushButton("Chọn…");
    pick->setObjectName("ovImageBtn");
    imageRow_ = row({image_});
    imageRow_->layout()->addWidget(pick);
    form_->addRow("Ảnh", imageRow_);

    font_ = new QFontComboBox;
    font_->setObjectName("ovFont");
    font_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    form_->addRow("Font", font_);
    fontSize_ = new QSpinBox;
    fontSize_->setObjectName("ovFontSize");
    fontSize_->setRange(4, 2000);
    fontSize_->setSuffix(" px");
    fontSize_->setKeyboardTracking(false);
    bold_ = new QCheckBox("Đậm");
    bold_->setObjectName("ovBold");
    italic_ = new QCheckBox("Nghiêng");
    italic_->setObjectName("ovItalic");
    styleRow_ = row({fontSize_, bold_, italic_});
    form_->addRow("Cỡ", styleRow_);

    color_ = new QPushButton("Chữ");
    color_->setObjectName("ovColor");
    outlineColor_ = new QPushButton("Viền");
    outlineColor_->setObjectName("ovOutlineColor");
    bgColor_ = new QPushButton("Nền");
    bgColor_->setObjectName("ovBgColor");
    form_->addRow("Màu", row({color_, outlineColor_, bgColor_}));
    outlineWidth_ = dspin("ovOutlineWidth", 0, 50, 1, 1);
    outlineWidth_->setSuffix(" px");
    outlineWidth_->setSpecialValueText("không viền");
    form_->addRow("Viền", outlineWidth_);
    align_ = new QComboBox;
    align_->setObjectName("ovAlign");
    align_->addItems({"Trái", "Giữa", "Phải"});
    form_->addRow("Căn", align_);

    const char* rectName[4] = {"ovX", "ovY", "ovW", "ovH"};
    const char* prefix[4] = {"x ", "y ", "rộng ", "cao "};
    for (int i = 0; i < 4; ++i) {
        rect_[i] = dspin(rectName[i], i < 2 ? -4 : 0.005, 8, 0.01, 3);
        rect_[i]->setPrefix(prefix[i]);
    }
    posRow_ = row({rect_[0], rect_[1]});
    sizeRow_ = row({rect_[2], rect_[3]});
    form_->addRow("Vị trí", posRow_);
    form_->addRow("Kích thước", sizeRow_);
    opacity_ = dspin("ovOpacity", 0, 1, 0.05, 2);
    form_->addRow("Độ đậm", opacity_);
    fade_ = dspin("ovFade", 0, 10, 0.1, 1);
    fade_->setSuffix(" s");
    fade_->setSpecialValueText("cắt ngay");
    form_->addRow("Mờ dần", fade_);
    speed_ = dspin("ovSpeed", 1, 5000, 10, 0);
    speed_->setSuffix(" px/s");
    form_->addRow("Tốc độ", speed_);
    minutes_ = new QSpinBox;
    minutes_->setObjectName("ovMinutes");
    minutes_->setRange(0, kMaxCountdownSeconds / 60);
    minutes_->setSuffix(" phút");
    minutes_->setKeyboardTracking(false);
    seconds_ = new QSpinBox;
    seconds_->setObjectName("ovSeconds");
    seconds_->setRange(0, 59);
    seconds_->setSuffix(" giây");
    seconds_->setKeyboardTracking(false);
    durationRow_ = row({minutes_, seconds_});
    form_->addRow("Thời lượng", durationRow_);

    // ---- noi tin hieu ----
    connect(name_, &QLineEdit::textEdited, this, [this](const QString& s) {
        editCurrent([s](OverlayDef& d) { d.name = s; }, "Đổi tên lớp phủ", FName);
    });
    connect(text_, &QPlainTextEdit::textChanged, this, [this] {
        const QString s = text_->toPlainText().left(kMaxOverlayText);
        editCurrent([s](OverlayDef& d) { d.text = s; }, "Sửa chữ lớp phủ", FText);
    });
    connect(pick, &QPushButton::clicked, this, [this] { chooseImage(); });
    connect(font_, &QComboBox::textActivated, this, [this] {      // chi khi NGUOI DUNG chon (khong phai khi hop tu nap font)
        const QString fam = font_->currentFont().family();
        editCurrent([fam](OverlayDef& d) { d.font = fam; }, "Đổi font lớp phủ");
    });
    connect(fontSize_, &QSpinBox::valueChanged, this, [this](int n) {
        editCurrent([n](OverlayDef& d) { d.fontSize = n; }, "Đổi cỡ chữ", FSize);
    });
    connect(bold_, &QCheckBox::toggled, this, [this](bool on) { editCurrent([on](OverlayDef& d) { d.bold = on; }, "Đổi chữ đậm"); });
    connect(italic_, &QCheckBox::toggled, this, [this](bool on) { editCurrent([on](OverlayDef& d) { d.italic = on; }, "Đổi chữ nghiêng"); });
    connect(color_, &QPushButton::clicked, this, [this] { pickColor(&OverlayDef::color, "Màu chữ"); });
    connect(outlineColor_, &QPushButton::clicked, this, [this] { pickColor(&OverlayDef::outlineColor, "Màu viền"); });
    connect(bgColor_, &QPushButton::clicked, this, [this] { pickColor(&OverlayDef::background, "Màu nền (alpha 0 = không nền)"); });
    connect(outlineWidth_, &QDoubleSpinBox::valueChanged, this, [this](double x) {
        editCurrent([x](OverlayDef& d) { d.outlineWidth = float(x); }, "Đổi độ dày viền", FOutlineWidth);
    });
    connect(align_, &QComboBox::currentIndexChanged, this, [this](int i) {
        editCurrent([i](OverlayDef& d) { d.align = OverlayAlign(i); }, "Đổi căn lề");
    });
    for (int i = 0; i < 4; ++i)
        connect(rect_[i], &QDoubleSpinBox::valueChanged, this, [this, i](double x) {
            editCurrent([i, x](OverlayDef& d) {
                QRectF& r = d.rect;
                if (i == 0) r.moveLeft(x); else if (i == 1) r.moveTop(x); else if (i == 2) r.setWidth(x); else r.setHeight(x);
            }, "Đổi vị trí/kích thước lớp phủ", FX + i);
        });
    connect(opacity_, &QDoubleSpinBox::valueChanged, this, [this](double x) {
        editCurrent([x](OverlayDef& d) { d.opacity = float(x); }, "Đổi độ đậm lớp phủ", FOpacity);
    });
    connect(fade_, &QDoubleSpinBox::valueChanged, this, [this](double x) {
        editCurrent([x](OverlayDef& d) { d.fade = float(x); }, "Đổi thời gian mờ dần", FFade);
    });
    connect(speed_, &QDoubleSpinBox::valueChanged, this, [this](double x) {
        editCurrent([x](OverlayDef& d) { d.speed = x; }, "Đổi tốc độ chữ chạy", FSpeed);
    });
    auto duration = [this] {
        const int s = std::clamp(minutes_->value() * 60 + seconds_->value(), 1, kMaxCountdownSeconds);
        editCurrent([s](OverlayDef& d) { d.seconds = s; }, "Đổi thời lượng đếm ngược", FDuration);
    };
    connect(minutes_, &QSpinBox::valueChanged, this, duration);
    connect(seconds_, &QSpinBox::valueChanged, this, duration);
    for (QWidget* w : std::initializer_list<QWidget*>{name_, font_, fontSize_, outlineWidth_, align_, rect_[0], rect_[1], rect_[2], rect_[3],
                                                      opacity_, fade_, speed_, minutes_, seconds_})
        w->installEventFilter(this);
    return props_;
}

bool OverlayPanel::eventFilter(QObject* obj, QEvent* ev) {
    if (ev->type() == QEvent::ShortcutOverride) {
        const int key = static_cast<QKeyEvent*>(ev)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) { ev->accept(); return true; }
    }
    return QWidget::eventFilter(obj, ev);
}

OverlayStack* OverlayPanel::stack() const { return &project_.channels[size_t(ch_)]->overlays; }

void OverlayPanel::setChannel(int ch) {
    if (ch < 0 || ch >= int(project_.channels.size())) return;
    ch_ = ch;
    title_->setText(QString("Lớp phủ của <b>Kênh %1</b>: đè lên Program, giữ nguyên khi đổi Scene. FTB che cả lớp phủ.").arg(ch + 1));
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
            auto* it = new QListWidgetItem;
            it->setData(Qt::UserRole, defs_[size_t(i)].id);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(Qt::Unchecked);
            list_->addItem(it);
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
    clockRow_->setVisible(d && d->type == OverlayType::Countdown);
    if (!d) return;
    syncing_ = true;                                  // dat gia tri len o nhap khong duoc tao buoc sua
    const bool text = d->type != OverlayType::Logo;
    form_->setRowVisible(text_, text);
    form_->setRowVisible(imageRow_, !text);
    form_->setRowVisible(font_, text);
    form_->setRowVisible(styleRow_, text);
    color_->setVisible(text);
    outlineColor_->setVisible(text);
    form_->setRowVisible(outlineWidth_, text);
    form_->setRowVisible(speed_, d->type == OverlayType::Ticker);
    form_->setRowVisible(durationRow_, d->type == OverlayType::Countdown);
    form_->setRowVisible(align_, d->type != OverlayType::Ticker);
    if (auto* l = qobject_cast<QLabel*>(form_->labelForField(text_)))
        l->setText(d->type == OverlayType::Countdown ? "Chữ trước" : "Nội dung");

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
    syncing_ = false;
}

void OverlayPanel::refreshLive() {
    const std::vector<OverlayFrame> frames = stack()->frames(engine_.now());
    const OverlayDef* cur = current();
    OverlayFrame curFrame;
    {
        QSignalBlocker b(list_);
        for (int r = 0; r < list_->count(); ++r) {
            QListWidgetItem* it = list_->item(r);
            const int id = it->data(Qt::UserRole).toInt();
            for (const OverlayFrame& f : frames) {
                if (f.def.id != id) continue;
                const QString type = typeLabel(f.def.type);
                const QString label = QString("%1%2%3%4").arg(f.on ? "● " : "", f.def.name,
                                                              f.def.name.startsWith(type) ? QString() : "  ·  " + type,
                                                              f.on ? "   ĐANG HIỆN" : "");
                const Qt::CheckState cs = f.on ? Qt::Checked : Qt::Unchecked;
                if (it->text() != label) it->setText(label);
                if (it->checkState() != cs) it->setCheckState(cs);
                it->setForeground(f.on ? QColor(colors::kProgram) : QColor(0xe6, 0xe8, 0xee));
                if (cur && id == cur->id) curFrame = f;
            }
        }
    }
    const bool has = cur != nullptr;
    toggle_->setEnabled(has);
    toggle_->setText(has && curFrame.on ? "ẨN khỏi Program" : "HIỆN lên Program");
    toggle_->setStyleSheet(has && curFrame.on ? "background:#8a1c1c;color:white;font-weight:bold" : QString());
    if (has && cur->type == OverlayType::Countdown) {
        clock_->setText(formatCountdown(curFrame.secondsLeft, cur->seconds >= 3600));
        startBtn_->setEnabled(!curFrame.running && curFrame.secondsLeft > 0);
        pauseBtn_->setEnabled(curFrame.running);
    }
    const int row = list_->currentRow(), n = list_->count();
    upBtn_->setEnabled(!locked_ && row > 0);
    downBtn_->setEnabled(!locked_ && row >= 0 && row < n - 1);
    delBtn_->setEnabled(!locked_ && has);

    QString warning;
    if (has && cur->type == OverlayType::Logo && !QFileInfo::exists(cur->image))
        warning = "⚠ Không tìm thấy file logo: " + cur->image;
    else if (has && cur->type != OverlayType::Logo && !cur->font.isEmpty() && !QFontDatabase::hasFamily(cur->font))
        warning = QString("⚠ Máy này không có font “%1”, đang dùng font thay thế.").arg(cur->font);
    else if (has && preview_->clipped(*cur, curFrame.secondsLeft, project_.channels[size_t(ch_)]->size))
        warning = "⚠ Chữ không vừa hộp nên bị cắt: tăng chiều rộng/cao hoặc giảm cỡ chữ.";
    warn_->setText(warning);
    warn_->setVisible(!warning.isEmpty());

    if (preview_->isVisible()) {
        const Channel& c = *project_.channels[size_t(ch_)];
        preview_->show(cur, curFrame.secondsLeft, c.size, engine_.monitor(ch_).preview);
    }
}

void OverlayPanel::setLocked(bool on) {
    locked_ = on;
    for (QPushButton* b : addBtn_) b->setEnabled(!on);
    syncProps();
    refreshLive();
}

void OverlayPanel::setOn(int id, bool on) {
    stack()->setOn(id, on, engine_.now());
    refreshLive();
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
    editDefs(std::move(after), "Thêm " + QString(typeLabel(d.type)).toLower());
    reload(d.id);
    return d.id;
}

void OverlayPanel::chooseImage() {
    const QString f = QFileDialog::getOpenFileName(this, "Chọn logo", QString(), "Ảnh (*.png *.webp *.gif *.jpg *.jpeg *.bmp)");
    if (!f.isEmpty()) editCurrent([f](OverlayDef& d) { d.image = QFileInfo(f).absoluteFilePath(); }, "Đổi ảnh logo");
}

void OverlayPanel::pickColor(QColor OverlayDef::*field, const QString& title) {
    const OverlayDef* d = current();
    if (!d || locked_) return;
    const int id = d->id;
    const QColor c = QColorDialog::getColor(d->*field, this, title, QColorDialog::ShowAlphaChannel);
    if (!c.isValid() || currentId() != id) return;
    editCurrent([field, c](OverlayDef& e) { e.*field = c; }, "Đổi màu lớp phủ");
}

void OverlayPanel::removeCurrent() {
    const int id = currentId(), row = list_->currentRow();
    if (id < 0 || locked_) return;
    std::vector<OverlayDef> after = defs_;
    after.erase(std::remove_if(after.begin(), after.end(), [id](const OverlayDef& d) { return d.id == id; }), after.end());
    editDefs(std::move(after), "Xoá lớp phủ");
    reload(-1);
    if (list_->count() > 0) list_->setCurrentRow(std::min(row, list_->count() - 1));
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
        editDefs(std::move(after), "Đổi thứ tự lớp phủ");
        reload(id);
        return;
    }
}

} // namespace lc
