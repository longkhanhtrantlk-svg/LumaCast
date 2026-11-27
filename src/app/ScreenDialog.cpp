#include "ScreenDialog.h"
#include "Icons.h"
#include "core/AppSettings.h"
#include "render/OutputHealth.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace lc {

namespace {
QSpinBox* pixels(const char* name, int lo, int hi, int value, const QString& prefix = {}) {
    auto* s = new QSpinBox;
    s->setObjectName(name);
    s->setRange(lo, hi);
    s->setValue(value);
    s->setPrefix(prefix);
    s->setKeyboardTracking(false);          // go xong moi ap vao ban nhap
    return s;
}
QDoubleSpinBox* meters(const char* name, double value, const QString& suffix) {
    auto* s = new QDoubleSpinBox;
    s->setObjectName(name);
    s->setRange(0.1, 500);
    s->setDecimals(2);
    s->setSingleStep(0.5);
    s->setValue(value);
    s->setSuffix(suffix);
    return s;
}
QPushButton* button(const char* name, const QString& text, const QString& tip = {}) {
    auto* b = new QPushButton(text);
    b->setObjectName(name);
    b->setToolTip(tip);
    b->setAutoDefault(false);              // Enter trong o so khong bam nut
    return b;
}
QHBoxLayout* line(std::initializer_list<QWidget*> widgets, bool stretch = true) {
    auto* h = new QHBoxLayout;
    for (QWidget* w : widgets) h->addWidget(w);
    if (stretch) h->addStretch();
    return h;
}
QString screenLabel(int i, const QScreen* s) {
    const QSize px = s->geometry().size() * s->devicePixelRatio();
    return QString("Màn %1: %2 (%3×%4)").arg(i + 1).arg(s->name()).arg(px.width()).arg(px.height());
}
int resolveScreen(const OutputConfig& c) {
    QStringList names;
    for (const QScreen* s : QGuiApplication::screens()) names << s->name();
    return resolveScreenIndex(c, names);
}
bool singleFullOutput(const std::vector<OutputConfig>& outputs, int channel) {
    int n = 0;
    bool full = true;
    for (const OutputConfig& o : outputs)
        if (o.channel == channel && o.kind == OutputKind::Screen) { ++n; full = full && o.crop == QRectF(0, 0, 1, 1); }
    return n == 1 && full;
}
} // namespace

// ---------------------------------------------------------------- ScreenDialog

ScreenDialog::ScreenDialog(Project& project, QWidget* parent) : QDialog(parent), project_(project) {
    setObjectName("screenDialog");
    setWindowTitle("Màn hình và khung hình");
    build();
    QSize want(980, 700);
    if (const QScreen* sc = parent ? parent->screen() : QGuiApplication::primaryScreen())
        want = want.boundedTo(sc->availableGeometry().size() - QSize(20, 60));
    resize(want);
}

void ScreenDialog::build() {
    auto* outer = new QVBoxLayout(this);
    auto* page = new QWidget;
    auto* columns = new QHBoxLayout(page);
    auto* left = new QVBoxLayout;
    auto* right = new QVBoxLayout;
    columns->addLayout(left, 2);
    columns->addLayout(right, 3);

    // ----- danh sach Output -----
    auto* outputsBox = new QGroupBox("Output");
    auto* ov = new QVBoxLayout(outputsBox);
    list_ = new QListWidget;
    list_->setObjectName("sdOutputs");
    list_->setMinimumHeight(90);
    ov->addWidget(list_);
    addB_ = new QToolButton;
    addB_->setObjectName("sdAdd");
    addB_->setIcon(glyphIcon(Glyph::Add));
    addB_->setToolTip("Thêm Output (một màn hình nữa)");
    removeB_ = new QToolButton;
    removeB_->setObjectName("sdRemove");
    removeB_->setIcon(glyphIcon(Glyph::Delete));
    removeB_->setToolTip("Xoá Output đang chọn");
    ov->addLayout(line({addB_, removeB_}));
    left->addWidget(outputsBox);

    // ----- khung hinh -----
    auto* canvasBox = new QGroupBox("Khung hình (canvas) của kênh");
    auto* cv = new QVBoxLayout(canvasBox);
    canvasTitle_ = new QLabel;
    canvasW_ = pixels("sdCanvasW", kMinCanvasSide, kMaxCanvasSide, 1920, "rộng ");
    canvasH_ = pixels("sdCanvasH", kMinCanvasSide, kMaxCanvasSide, 1080, "cao ");
    auto* match = button("sdMatch", "Bằng chỗ hiện", "Đặt khung hình đúng bằng vùng LED / độ phân giải của Output đang chọn (1:1)");
    cv->addWidget(canvasTitle_);
    cv->addLayout(line({canvasW_, canvasH_, match}));
    allScenesBox_ = new QCheckBox("Đặt mọi Scene theo khung hình này");
    allScenesBox_->setObjectName("sdAllScenes");
    allScenesBox_->setToolTip("Bỏ chọn: chỉ Scene đang dựng đúng bằng khung hình cũ được đổi theo");
    cv->addWidget(allScenesBox_);
    left->addWidget(canvasBox);

    // ----- video wall -----
    auto* wallBox = new QGroupBox("Video wall (nhiều màn ghép)");
    auto* wv = new QVBoxLayout(wallBox);
    cols_ = pixels("sdWallCols", 1, 16, 2, "cột ");
    rows_ = pixels("sdWallRows", 1, 16, 1, "hàng ");
    cellW_ = pixels("sdCellW", kMinCanvasSide, kMaxCanvasSide, 1920, "ô rộng ");
    cellH_ = pixels("sdCellH", kMinCanvasSide, kMaxCanvasSide, 1080, "cao ");
    gapX_ = pixels("sdGapX", 0, kMaxWallGap, 0, "khe ngang ");
    gapY_ = pixels("sdGapY", 0, kMaxWallGap, 0, "khe dọc ");
    auto* makeWallB = button("sdWall", "Tạo video wall", "Thay các Output màn hình của kênh này bằng một lưới cột × hàng, kèm khung hình tương ứng");
    wv->addLayout(line({cols_, rows_}));
    wv->addLayout(line({cellW_, cellH_}));
    wv->addLayout(line({gapX_, gapY_}));
    wv->addLayout(line({makeWallB}));
    left->addWidget(wallBox);
    left->addStretch();

    // ----- Output dang chon -----
    auto* props = new QGroupBox("Output đang chọn");
    auto* form = new QFormLayout(props);
    name_ = new QLineEdit;
    name_->setObjectName("sdName");
    form->addRow("Tên", name_);
    channel_ = new QComboBox;
    channel_->setObjectName("sdChannel");
    for (size_t i = 0; i < project_.channels.size(); ++i)
        channel_->addItem(QString("Kênh %1: %2").arg(i + 1).arg(project_.channels[i]->name));
    form->addRow("Kênh", channel_);
    form->setRowVisible(channel_, project_.channels.size() > 1);
    mode_ = new QComboBox;
    mode_->setObjectName("sdMode");
    mode_->addItems({"Cửa sổ thường (để thử)", "Toàn màn hình (ra LED)"});
    form->addRow("Hiển thị", mode_);
    screen_ = new QComboBox;
    screen_->setObjectName("sdScreen");
    form->addRow("Màn hình", screen_);
    w_ = pixels("sdW", kMinCanvasSide, kMaxCanvasSide, 1920, "rộng ");
    h_ = pixels("sdH", kMinCanvasSide, kMaxCanvasSide, 1080, "cao ");
    auto* sizeRow = new QWidget;
    auto* sh = line({w_, h_});
    sh->setContentsMargins(0, 0, 0, 0);
    sizeRow->setLayout(sh);
    form->addRow("Rộng × cao", sizeRow);
    scale_ = new QComboBox;
    scale_->setObjectName("sdScale");
    scale_->addItems({"Fit (giữ tỉ lệ, có thể viền đen)", "Fill (phủ kín, có thể cắt mép)", "Stretch (kéo cho vừa)", "1:1 (không co giãn)"});
    form->addRow("Đặt hình", scale_);

    areaBox_ = new QGroupBox("LED chỉ lấy một vùng của tín hiệu");
    areaBox_->setObjectName("sdAreaBox");
    areaBox_->setCheckable(true);
    areaBox_->setToolTip("Bật khi LED nhỏ hơn tín hiệu và bộ xử lý lấy đúng một góc của hình: hình chỉ đặt trong vùng này, ngoài vùng để đen");
    auto* av = new QVBoxLayout(areaBox_);
    areaX_ = pixels("sdAreaX", 0, kMaxCanvasSide, 0, "x ");
    areaY_ = pixels("sdAreaY", 0, kMaxCanvasSide, 0, "y ");
    areaW_ = pixels("sdAreaW", kMinCanvasSide, kMaxCanvasSide, 1920, "rộng ");
    areaH_ = pixels("sdAreaH", kMinCanvasSide, kMaxCanvasSide, 1080, "cao ");
    auto* topLeft = button("sdTopLeft", "Góc trên trái");
    auto* center = button("sdCenter", "Giữa khung");
    av->addLayout(line({areaW_, areaH_}));
    av->addLayout(line({areaX_, areaY_, topLeft, center}));
    metersW_ = meters("sdMetersW", 4, " m");
    metersH_ = meters("sdMetersH", 2, " m");
    pitch_ = new QDoubleSpinBox;
    pitch_->setObjectName("sdPitch");
    pitch_->setRange(0.5, 50);
    pitch_->setDecimals(2);
    pitch_->setValue(2.5);
    pitch_->setPrefix("P");
    pitch_->setToolTip("Pixel pitch của tấm LED (mm giữa hai điểm ảnh): P2.5 = 2,5 mm");
    ledPixels_ = new QLabel;
    ledPixels_->setObjectName("sdLedPixels");
    auto* fromMeters = button("sdFromMeters", "Dùng số này", "Điền số điểm ảnh tính được vào rộng/cao của vùng LED");
    av->addLayout(line({new QLabel("Màn LED"), metersW_, new QLabel("×"), metersH_, pitch_}));
    av->addLayout(line({ledPixels_, fromMeters}));
    follow_ = new QCheckBox("Khung hình = đúng vùng LED (1:1)");
    follow_->setObjectName("sdFollow");
    follow_->setChecked(true);
    av->addWidget(follow_);
    form->addRow(areaBox_);
    right->addWidget(props);

    // ----- cau hinh luu san -----
    auto* presetBox = new QGroupBox("Cấu hình LED đã lưu (theo máy này)");
    auto* ph = new QHBoxLayout(presetBox);
    presets_ = new QComboBox;
    presets_->setObjectName("sdPresets");
    auto* load = button("sdPresetLoad", "Dùng", "Điền vùng LED và khung hình đã lưu cho Output đang chọn (còn phải bấm Áp dụng)");
    auto* save = button("sdPresetSave", "Lưu…", "Lưu vùng LED + khung hình hiện tại thành một cấu hình đặt tên");
    auto* del = button("sdPresetDelete", "Xoá", "Xoá cấu hình đã lưu đang chọn");
    ph->addWidget(presets_, 1);
    ph->addWidget(load);
    ph->addWidget(save);
    ph->addWidget(del);
    right->addWidget(presetBox);
    right->addStretch();

    auto* sa = new QScrollArea;
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setWidget(page);
    outer->addWidget(sa, 1);

    warnings_ = new QLabel;
    warnings_->setObjectName("sdWarnings");
    warnings_->setWordWrap(true);
    warnings_->setTextFormat(Qt::PlainText);
    auto* warnArea = new QScrollArea;
    warnArea->setWidgetResizable(true);
    warnArea->setWidget(warnings_);
    warnArea->setMaximumHeight(110);
    warnArea->setMinimumHeight(60);
    outer->addWidget(warnArea);

    patternB_ = button("sdPattern", "Mẫu thử", "Hiện mẫu thử (viền, thước, ô kiểm tra 1:1) trên các Output ĐANG CHẠY; bấm Áp dụng trước nếu vừa đổi");
    patternB_->setCheckable(true);
    applyB_ = button("sdApply", "Áp dụng", "Đổi Output / khung hình ngay. Hoàn tác được (Ctrl+Z). Không đổi Preview/Program");
    auto* close = button("sdClose", "Đóng");
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(patternB_);
    bottom->addStretch();
    bottom->addWidget(applyB_);
    bottom->addWidget(close);
    outer->addLayout(bottom);

    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(applyB_, &QPushButton::clicked, this, [this] { applyDraft(); });
    connect(patternB_, &QPushButton::toggled, this, [this](bool on) { if (testPattern) testPattern(on); });
    connect(this, &QDialog::finished, this, [this] { patternB_->setChecked(false); });
    connect(list_, &QListWidget::currentRowChanged, this, [this] { if (!syncing_) syncForm(); });
    connect(addB_, &QToolButton::clicked, this, [this] {
        OutputConfig o;
        o.channel = channelOfSelected();
        for (int n = int(draft_.outputs.size()) + 1;; ++n) {
            o.name = QString("Output %1").arg(n);
            if (std::none_of(draft_.outputs.begin(), draft_.outputs.end(), [&](const OutputConfig& c) { return c.name == o.name; })) break;
        }
        draft_.outputs.push_back(o);
        syncList(int(draft_.outputs.size()) - 1);
    });
    connect(removeB_, &QToolButton::clicked, this, [this] {
        const int i = selectedOutput();
        if (i < 0 || draft_.outputs.size() < 2) return;
        draft_.outputs.erase(draft_.outputs.begin() + i);
        syncList(std::min(i, int(draft_.outputs.size()) - 1));
    });
    connect(name_, &QLineEdit::textEdited, this, [this](const QString& t) {
        edit([&](OutputConfig& c) { c.name = t; });
        if (const int i = selectedOutput(); i >= 0) list_->item(i)->setText(t);
    });
    connect(channel_, &QComboBox::activated, this, [this](int k) { edit([k](OutputConfig& c) { c.channel = k; }); syncForm(); });
    auto pickScreen = [this] {
        const int k = screen_->currentData().toInt();
        const auto screens = QGuiApplication::screens();
        const bool full = mode_->currentIndex() == 1;
        edit([&](OutputConfig& c) {
            c.fullscreen = full;
            if (full && k >= 0 && k < screens.size()) {
                c.screenIndex = k;
                c.screenName = screens[k]->name();
                c.size = screens[k]->geometry().size() * screens[k]->devicePixelRatio();
            }
        });
        syncForm();
    };
    connect(mode_, &QComboBox::activated, this, [pickScreen] { pickScreen(); });
    connect(screen_, &QComboBox::activated, this, [pickScreen] { pickScreen(); });
    for (QSpinBox* s : {w_, h_})
        connect(s, &QSpinBox::valueChanged, this, [this] { edit([this](OutputConfig& c) { c.size = QSize(w_->value(), h_->value()); }); });
    connect(scale_, &QComboBox::activated, this, [this](int k) { edit([k](OutputConfig& c) { c.scale = ScaleMode(k); }); });
    connect(areaBox_, &QGroupBox::toggled, this, [this](bool on) {
        if (syncing_) return;
        const int i = selectedOutput();
        if (i < 0) return;
        const QSize frame = outputSignalSize(draft_.outputs[size_t(i)]);
        setArea(on ? QRect(QPoint(0, 0), canvasOfSelected().boundedTo(frame)) : QRect());
    });
    for (QSpinBox* s : {areaX_, areaY_, areaW_, areaH_})
        connect(s, &QSpinBox::valueChanged, this, [this] {
            if (!syncing_) setArea(QRect(areaX_->value(), areaY_->value(), areaW_->value(), areaH_->value()));
        });
    connect(topLeft, &QPushButton::clicked, this, [this] { setArea(QRect(0, 0, areaW_->value(), areaH_->value())); });
    connect(center, &QPushButton::clicked, this, [this] {
        const int i = selectedOutput();
        if (i < 0) return;
        const QSize f = outputSignalSize(draft_.outputs[size_t(i)]);
        setArea(QRect(std::max(0, (f.width() - areaW_->value()) / 2), std::max(0, (f.height() - areaH_->value()) / 2),
                      areaW_->value(), areaH_->value()));
    });
    auto showLed = [this] {
        ledPixels_->setText(QString("= %1 × %2 điểm ảnh").arg(ledPixels(metersW_->value(), pitch_->value())).arg(ledPixels(metersH_->value(), pitch_->value())));
    };
    for (QDoubleSpinBox* s : {metersW_, metersH_, pitch_}) connect(s, &QDoubleSpinBox::valueChanged, this, [showLed] { showLed(); });
    showLed();
    connect(fromMeters, &QPushButton::clicked, this, [this] {
        setArea(QRect(areaX_->value(), areaY_->value(), std::clamp(ledPixels(metersW_->value(), pitch_->value()), kMinCanvasSide, kMaxCanvasSide),
                      std::clamp(ledPixels(metersH_->value(), pitch_->value()), kMinCanvasSide, kMaxCanvasSide)));
    });
    connect(follow_, &QCheckBox::toggled, this, [this](bool on) {
        if (on && !syncing_) { followArea(); syncForm(); }
    });
    for (QSpinBox* s : {canvasW_, canvasH_})
        connect(s, &QSpinBox::valueChanged, this, [this] {
            if (syncing_) return;
            setCanvas(channelOfSelected(), QSize(canvasW_->value(), canvasH_->value()));
            updateWarnings();
        });
    connect(match, &QPushButton::clicked, this, [this] {
        const int i = selectedOutput();
        if (i < 0) return;
        const OutputConfig& c = draft_.outputs[size_t(i)];
        QSize s = outputPictureRect(c, outputSignalSize(c)).size();
        if ((c.rotation / 90) % 2) s.transpose();
        setCanvas(c.channel, s);
        syncForm();
    });
    connect(allScenesBox_, &QCheckBox::toggled, this, [this](bool on) {
        if (syncing_) return;
        const int c = channelOfSelected();
        if (c >= 0 && c < int(allScenes_.size())) allScenes_[size_t(c)] = on;
        updateWarnings();
    });
    connect(makeWallB, &QPushButton::clicked, this, [this] { makeWallFromFields(); });
    connect(load, &QPushButton::clicked, this, [this] { loadPreset(presets_->currentText()); });
    connect(save, &QPushButton::clicked, this, [this] {
        const QString name = QInputDialog::getText(this, "Lưu cấu hình LED", "Tên (vd. sự kiện / nhà cung cấp):", QLineEdit::Normal, presets_->currentText());
        if (!name.trimmed().isEmpty()) savePreset(name.trimmed());
    });
    connect(del, &QPushButton::clicked, this, [this] { deletePreset(presets_->currentText()); });
}

int ScreenDialog::selectedOutput() const {
    const int i = list_->currentRow();
    return i >= 0 && i < int(draft_.outputs.size()) ? i : -1;
}

void ScreenDialog::selectOutput(int i) { list_->setCurrentRow(i); }

int ScreenDialog::channelOfSelected() const {
    const int i = selectedOutput();
    return i >= 0 ? draft_.outputs[size_t(i)].channel : 0;
}

QSize ScreenDialog::canvasOfSelected() const {
    const int c = channelOfSelected();
    return c >= 0 && c < int(draft_.canvases.size()) ? draft_.canvases[size_t(c)] : QSize(1920, 1080);
}

void ScreenDialog::reload() {
    if (!current) return;
    const int keep = selectedOutput();
    draft_ = current();
    allScenes_.assign(draft_.canvases.size(), 0);
    refreshPresets();
    syncList(std::max(0, std::min(keep, int(draft_.outputs.size()) - 1)));
}

void ScreenDialog::syncList(int select) {
    syncing_ = true;
    list_->clear();
    for (const OutputConfig& c : draft_.outputs) list_->addItem(c.name);
    list_->setCurrentRow(select);
    syncing_ = false;
    syncForm();
}

void ScreenDialog::syncForm() {
    const int i = selectedOutput();
    syncing_ = true;
    removeB_->setEnabled(draft_.outputs.size() > 1 && i >= 0);
    if (i >= 0) {
        const OutputConfig c = draft_.outputs[size_t(i)];
        const bool ndi = c.kind == OutputKind::Ndi;
        if (name_->text() != c.name) name_->setText(c.name);
        channel_->setCurrentIndex(c.channel);
        mode_->setCurrentIndex(c.fullscreen ? 1 : 0);
        mode_->setEnabled(!ndi);
        screen_->clear();
        const auto screens = QGuiApplication::screens();
        int sel = -1;
        for (int k = 0; k < screens.size(); ++k) {
            screen_->addItem(screenLabel(k, screens[k]), k);
            if (c.screenName.isEmpty() ? k == std::max(0, c.screenIndex) : screens[k]->name() == c.screenName) sel = k;
        }
        if (sel < 0 && c.fullscreen) {                               // man cua project khong co tren may nay: giu nguyen
            screen_->addItem("(không có trên máy này) " + c.screenName, -1);
            sel = screen_->count() - 1;
        }
        screen_->setCurrentIndex(std::max(0, sel));
        screen_->setEnabled(c.fullscreen && !ndi);
        const QSize frame = outputSignalSize(c);
        w_->setValue(frame.width());
        h_->setValue(frame.height());
        w_->setEnabled(!c.fullscreen);
        h_->setEnabled(!c.fullscreen);
        scale_->setCurrentIndex(int(c.scale));
        areaBox_->setEnabled(!ndi);
        areaBox_->setChecked(!c.area.isEmpty());
        const QRect a = c.area.isEmpty() ? QRect(QPoint(0, 0), frame) : c.area;
        areaX_->setValue(a.x());
        areaY_->setValue(a.y());
        areaW_->setValue(a.width());
        areaH_->setValue(a.height());
        follow_->setEnabled(singleFullOutput(draft_.outputs, c.channel));
        const QSize canvas = canvasOfSelected();
        canvasTitle_->setText(QString("Kênh %1").arg(c.channel + 1));
        canvasW_->setValue(canvas.width());
        canvasH_->setValue(canvas.height());
        allScenesBox_->setChecked(c.channel < int(allScenes_.size()) && allScenes_[size_t(c.channel)]);
    }
    syncing_ = false;
    updateWarnings();
}

void ScreenDialog::edit(const std::function<void(OutputConfig&)>& mutate) {
    const int i = selectedOutput();
    if (syncing_ || i < 0) return;
    mutate(draft_.outputs[size_t(i)]);
    updateWarnings();
}

void ScreenDialog::setCanvas(int channel, QSize s) {
    if (channel < 0 || channel >= int(draft_.canvases.size())) return;
    draft_.canvases[size_t(channel)] = s.expandedTo(QSize(kMinCanvasSide, kMinCanvasSide)).boundedTo(QSize(kMaxCanvasSide, kMaxCanvasSide));
}

void ScreenDialog::setArea(const QRect& r) {
    const int i = selectedOutput();
    if (i < 0 || draft_.outputs[size_t(i)].kind == OutputKind::Ndi) return;
    draft_.outputs[size_t(i)].area = r;
    followArea();
    syncForm();
}

// "Khung hinh = vung LED": chi khi kenh co dung mot Output man hinh lay ca khung (khong phai video wall).
void ScreenDialog::followArea() {
    const int i = selectedOutput();
    if (i < 0 || !follow_->isChecked()) return;
    const OutputConfig& c = draft_.outputs[size_t(i)];
    if (c.area.isEmpty() || !singleFullOutput(draft_.outputs, c.channel)) return;
    QSize s = c.area.size();
    if ((c.rotation / 90) % 2) s.transpose();
    setCanvas(c.channel, s);
}

void ScreenDialog::makeWallFromFields() {
    const int ch = channelOfSelected();
    const int cols = cols_->value(), rows = rows_->value();
    const QSize cell(cellW_->value(), cellH_->value()), gap(cols > 1 ? gapX_->value() : 0, rows > 1 ? gapY_->value() : 0);
    const QSize canvas = wallCanvas(cols, rows, cell, gap);
    if (canvas.width() > kMaxCanvasSide || canvas.height() > kMaxCanvasSide) {
        warnings_->setText(QString("Không tạo được video wall: khung hình %1×%2 vượt quá %3 điểm ảnh.").arg(canvas.width()).arg(canvas.height()).arg(kMaxCanvasSide));
        return;
    }
    const int sel = selectedOutput();
    const bool full = sel >= 0 && draft_.outputs[size_t(sel)].fullscreen;
    const int first = sel >= 0 ? std::max(0, draft_.outputs[size_t(sel)].screenIndex) : 0;
    std::vector<OutputConfig> wall = makeWall(ch, cols, rows, cell, first, full, gap);
    const auto screens = QGuiApplication::screens();
    for (OutputConfig& o : wall)
        if (full && o.screenIndex < screens.size()) o.screenName = screens[o.screenIndex]->name();
    std::vector<OutputConfig> next = wall;
    for (const OutputConfig& o : draft_.outputs)
        if (o.channel != ch || o.kind != OutputKind::Screen) next.push_back(o);     // giu Output NDI va kenh khac
    draft_.outputs = std::move(next);
    setCanvas(ch, canvas);
    syncList(0);
}

QStringList ScreenDialog::warnings() const {
    QStringList w;
    const auto screens = QGuiApplication::screens();
    const QScreen* control = parentWidget() ? parentWidget()->screen() : nullptr;
    std::vector<int> usedScreens;
    for (int i = 0; i < int(draft_.outputs.size()); ++i) {
        const OutputConfig& c = draft_.outputs[size_t(i)];
        if (c.channel < 0 || c.channel >= int(draft_.canvases.size())) continue;
        for (const QString& t : outputWarnings(draft_.outputs, i, draft_.canvases[size_t(c.channel)], outputSignalSize(c)))
            w << c.name + ": " + t;
        if (!c.fullscreen || c.kind != OutputKind::Screen) continue;
        const int k = resolveScreen(c);
        if (k < 0) { w << c.name + ": màn hình này không có trên máy — Output sẽ không hiện."; continue; }
        if (std::find(usedScreens.begin(), usedScreens.end(), k) != usedScreens.end())
            w << c.name + ": trùng màn hình với Output khác (sẽ che nhau).";
        usedScreens.push_back(k);
        if (control && screens[k] == control) w << c.name + ": màn này đang chứa bảng điều khiển — Output toàn màn hình sẽ che nó.";
    }
    for (int c = 0; c < int(draft_.canvases.size()) && c < int(project_.channels.size()); ++c) {
        if (c < int(allScenes_.size()) && allScenes_[size_t(c)]) continue;
        // Scene dung bang khung hinh cu se duoc doi theo, chi canh bao cac Scene co kich thuoc rieng
        const QSize old = project_.channels[size_t(c)]->size, now = draft_.canvases[size_t(c)];
        std::vector<std::shared_ptr<Scene>> own;
        for (const auto& s : project_.channels[size_t(c)]->scenes)
            if (s->size != old && s->size != now) own.push_back(s);
        const QString t = sceneSizeWarning(own, now);
        if (!t.isEmpty()) w << t;
    }
    return w;
}

void ScreenDialog::updateWarnings() {
    const QStringList w = warnings();
    warnings_->setText(w.isEmpty() ? QString("✓ Không có co giãn hay cắt hình.") : "⚠ " + w.join("\n⚠ "));
    warnings_->setStyleSheet(w.isEmpty() ? "color:#81c784" : "color:#ffb74d");
}

bool ScreenDialog::applyDraft() {
    if (!apply) return false;
    for (OutputConfig& o : draft_.outputs)
        if (o.name.trimmed().isEmpty()) o.name = "Output";
    const QScreen* control = parentWidget() ? parentWidget()->screen() : nullptr;
    const auto screens = QGuiApplication::screens();
    const ScreenSetup now = current ? current() : ScreenSetup{};
    bool covers = false;
    for (const OutputConfig& c : draft_.outputs) {
        if (!c.fullscreen || c.kind != OutputKind::Screen) continue;
        const int k = resolveScreen(c);
        const bool already = std::any_of(now.outputs.begin(), now.outputs.end(), [&](const OutputConfig& o) { return o.fullscreen && resolveScreen(o) == k; });
        covers = covers || (k >= 0 && control && screens[k] == control && !already);
    }
    if (covers) {
        const QString q = "Một Output toàn màn hình sẽ nằm trên chính màn đang chứa bảng điều khiển và che nó.\n"
                          "Muốn thoát: bấm vào hình đó rồi nhấn Esc.\n\nVẫn áp dụng?";
        const bool yes = confirm ? confirm(q)
                                 : QMessageBox::warning(this, "Che bảng điều khiển", q, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
        if (!yes) return false;
    }
    if (!apply(draft_, allScenes_)) return false;
    patternB_->setChecked(false);
    reload();
    return true;
}

// ---------------- cau hinh LED luu san ----------------

QStringList ScreenDialog::presetNames() const {
    QSettings st = appSettings();
    st.beginGroup("ledPresets");
    return st.childGroups();
}

void ScreenDialog::refreshPresets() {
    const QString keep = presets_->currentText();
    presets_->clear();
    presets_->addItems(presetNames());
    const int k = presets_->findText(keep);
    if (k >= 0) presets_->setCurrentIndex(k);
}

void ScreenDialog::savePreset(const QString& name) {
    const int i = selectedOutput();
    const QString key = QString(name).replace('/', '-').replace('\\', '-').trimmed();
    if (i < 0 || key.isEmpty()) return;
    QSettings st = appSettings();
    st.beginGroup("ledPresets/" + key);
    st.setValue("canvas", canvasOfSelected());
    st.setValue("area", draft_.outputs[size_t(i)].area);
    st.setValue("pitch", pitch_->value());
    st.endGroup();
    st.sync();
    refreshPresets();
    presets_->setCurrentText(key);
}

bool ScreenDialog::loadPreset(const QString& name) {
    const int i = selectedOutput();
    if (i < 0 || name.isEmpty() || !presetNames().contains(name)) return false;
    QSettings st = appSettings();
    st.beginGroup("ledPresets/" + name);
    const QSize canvas = st.value("canvas").toSize();
    const QRect area = st.value("area").toRect();
    pitch_->setValue(st.value("pitch", 2.5).toDouble());
    if (draft_.outputs[size_t(i)].kind != OutputKind::Ndi) draft_.outputs[size_t(i)].area = area;
    if (!canvas.isEmpty()) setCanvas(channelOfSelected(), canvas);
    syncForm();
    return true;
}

void ScreenDialog::deletePreset(const QString& name) {
    if (name.isEmpty()) return;
    QSettings st = appSettings();
    st.remove("ledPresets/" + name);
    st.sync();
    refreshPresets();
}

// ---------------------------------------------------------------- NewProjectDialog

NewProjectDialog::NewProjectDialog(QWidget* parent) : QDialog(parent) {
    setObjectName("newProjectDialog");
    setWindowTitle("Project mới");
    auto* v = new QVBoxLayout(this);
    v->addWidget(new QLabel("Khung hình (canvas). Một màn LED qua một dây HDMI: để 1920×1080, đặt vùng LED sau trong \"Màn hình và khung hình\"."));
    w_ = pixels("npW", kMinCanvasSide, kMaxCanvasSide, 1920, "rộng ");
    h_ = pixels("npH", kMinCanvasSide, kMaxCanvasSide, 1080, "cao ");
    v->addLayout(line({w_, h_}));
    wall_ = new QGroupBox("Video wall: nhiều màn ghép (khung hình = ô × số màn + khe)");
    wall_->setObjectName("npWall");
    wall_->setCheckable(true);
    wall_->setChecked(false);
    auto* g = new QVBoxLayout(wall_);
    cols_ = pixels("npCols", 1, 16, 2, "cột ");
    rows_ = pixels("npRows", 1, 16, 1, "hàng ");
    gapX_ = pixels("npGapX", 0, kMaxWallGap, 0, "khe ngang ");
    gapY_ = pixels("npGapY", 0, kMaxWallGap, 0, "khe dọc ");
    fullscreen_ = new QCheckBox("Mỗi ô toàn màn hình trên một màn (màn 2, 3…)");
    fullscreen_->setObjectName("npFullscreen");
    g->addWidget(new QLabel("Rộng × cao ở trên là cỡ của MỘT ô."));
    g->addLayout(line({cols_, rows_}));
    g->addLayout(line({gapX_, gapY_}));
    g->addWidget(fullscreen_);
    v->addWidget(wall_);
    info_ = new QLabel;
    info_->setObjectName("npInfo");
    info_->setWordWrap(true);
    v->addWidget(info_);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Ok)->setText("Tạo…");
    box->button(QDialogButtonBox::Cancel)->setText("Huỷ");
    v->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto update = [this, box] {
        const QString p = problem();
        box->button(QDialogButtonBox::Ok)->setEnabled(p.isEmpty());
        const Project pr = p.isEmpty() ? project() : Project{};
        info_->setText(!p.isEmpty() ? p
                                    : QString("Khung hình %1×%2, %3 Output.").arg(pr.channels[0]->size.width()).arg(pr.channels[0]->size.height()).arg(pr.outputs.size()));
    };
    for (QSpinBox* s : {w_, h_, cols_, rows_, gapX_, gapY_}) connect(s, &QSpinBox::valueChanged, this, update);
    connect(wall_, &QGroupBox::toggled, this, update);
    update();
}

QString NewProjectDialog::problem() const {
    if (!wall_->isChecked()) return {};
    const QSize canvas = wallCanvas(cols_->value(), rows_->value(), QSize(w_->value(), h_->value()), QSize(gapX_->value(), gapY_->value()));
    if (canvas.width() > kMaxCanvasSide || canvas.height() > kMaxCanvasSide)
        return QString("Khung hình %1×%2 vượt quá %3 điểm ảnh.").arg(canvas.width()).arg(canvas.height()).arg(kMaxCanvasSide);
    return {};
}

Project NewProjectDialog::project() const {
    const QSize cell(w_->value(), h_->value());
    if (!wall_->isChecked()) {
        OutputConfig o;
        o.name = "Output 1";
        o.size = cell;
        return makeNewProject(cell, {o});
    }
    const int cols = cols_->value(), rows = rows_->value();
    const QSize gap(cols > 1 ? gapX_->value() : 0, rows > 1 ? gapY_->value() : 0);
    const bool full = fullscreen_->isChecked();
    return makeNewProject(wallCanvas(cols, rows, cell, gap), makeWall(0, cols, rows, cell, full ? 1 : 0, full, gap));
}

} // namespace lc
