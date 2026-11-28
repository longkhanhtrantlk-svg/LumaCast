#include "LiveDialogs.h"
#include "core/I18n.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include "AboutInfo.h"
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lc {

// ---------------- NDI vao ----------------

NdiSourceDialog::NdiSourceDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(LC_TR("Thêm nguồn NDI"));
    setObjectName("ndiSourceDialog");
    resize(520, 420);
    auto* v = new QVBoxLayout(this);
    note_ = new QLabel;
    note_->setObjectName("ndiNote");
    note_->setWordWrap(true);
    v->addWidget(note_);
    {   // NDI(R): dong ghi nhan + lien ket toi ndi.video gan noi dung NDI (SPEC 5.1)
        auto* nl = new QLabel(QString(LC_TR("%1<br><a href=\"https://ndi.video\">Tìm hiểu thêm về NDI® tại ndi.video</a>")).arg(ndiTrademarkNotice().toHtmlEscaped()));
        nl->setObjectName("ndiLink");
        nl->setOpenExternalLinks(true);
        nl->setWordWrap(true);
        v->addWidget(nl);
    }
    list_ = new QListWidget;
    list_->setObjectName("ndiList");
    v->addWidget(list_, 1);
    refreshBtn_ = new QPushButton(LC_TR("Quét lại"));
    refreshBtn_->setObjectName("ndiRefresh");
    v->addWidget(refreshBtn_);
    auto* form = new QFormLayout;
    nameEdit_ = new QLineEdit;
    nameEdit_->setObjectName("ndiName");
    nameEdit_->setPlaceholderText(LC_TR("MÁY (Tên nguồn) — chọn ở trên hoặc nhập tay"));
    labelEdit_ = new QLineEdit;
    labelEdit_->setObjectName("ndiLabel");
    labelEdit_->setPlaceholderText(LC_TR("Tên hiển thị (tuỳ chọn)"));
    form->addRow(LC_TR("Nguồn NDI:"), nameEdit_);
    form->addRow(LC_TR("Nhãn:"), labelEdit_);
    v->addLayout(form);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    ok_ = bb->button(QDialogButtonBox::Ok);
    ok_->setText(LC_TR("Thêm"));
    ok_->setEnabled(false);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(refreshBtn_, &QPushButton::clicked, this, [this] { refresh(); });
    connect(nameEdit_, &QLineEdit::textChanged, this, [this](const QString& t) { ok_->setEnabled(!t.trimmed().isEmpty()); });
    connect(list_, &QListWidget::currentRowChanged, this, [this](int r) {
        if (r >= 0 && r < list_->count()) nameEdit_->setText(list_->item(r)->text());
    });
    connect(this, &NdiSourceDialog::listed, this, &NdiSourceDialog::onListed, Qt::QueuedConnection);
    refresh();
}

NdiSourceDialog::~NdiSourceDialog() {
    if (th_.joinable()) th_.join();
}

void NdiSourceDialog::refresh() {
    auto b = ndiBackend();
    if (!b->available()) {
        note_->setText(LC_TR("<b>NDI chưa dùng được:</b> ") + b->unavailableReason() +
                       LC_TR("<br>Cài NDI Runtime (miễn phí, từ ndi.video) rồi khởi động lại LumaCast. Bạn vẫn có thể thêm nguồn bằng cách nhập tên — nguồn sẽ tự kết nối khi NDI sẵn sàng."));
        return;
    }
    if (busy_.exchange(true)) return;
    note_->setText(LC_TR("Đang quét mạng tìm nguồn NDI..."));
    refreshBtn_->setEnabled(false);
    if (th_.joinable()) th_.join();
    QPointer<NdiSourceDialog> self(this);
    th_ = std::thread([self, b] {
        const auto list = b->listSources(1500);
        QStringList n, u;
        for (const auto& s : list) { n << s.name; u << s.url; }
        if (self) emit self->listed(n, u);                 // signal tu thread khac -> queued
    });
}

void NdiSourceDialog::onListed(const QStringList& names, const QStringList& urls) {
    busy_ = false;
    refreshBtn_->setEnabled(true);
    const QString cur = nameEdit_->text();
    list_->clear();
    list_->addItems(names);
    urls_ = urls;
    note_->setText(names.isEmpty() ? LC_TR("Không thấy nguồn NDI nào trên mạng. Kiểm tra mạng/tường lửa hoặc nhập tên nguồn bằng tay.")
                                   : QString(LC_TR("Tìm thấy %1 nguồn NDI. Chọn một nguồn:")).arg(names.size()));
    nameEdit_->setText(cur);
}

QString NdiSourceDialog::name() const { return nameEdit_->text().trimmed(); }
QString NdiSourceDialog::label() const { return labelEdit_->text().trimmed(); }
QString NdiSourceDialog::url() const {
    for (int i = 0; i < list_->count() && i < urls_.size(); ++i)
        if (list_->item(i)->text() == name()) return urls_[i];
    return {};
}

// ---------------- Camera ----------------

CameraSourceDialog::CameraSourceDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(LC_TR("Thêm camera / card capture"));
    setObjectName("cameraSourceDialog");
    auto* v = new QVBoxLayout(this);
    note_ = new QLabel;
    note_->setObjectName("camNote");
    note_->setWordWrap(true);
    v->addWidget(note_);
    auto* form = new QFormLayout;
    dev_ = new QComboBox;
    dev_->setObjectName("camDevice");
    fmt_ = new QComboBox;
    fmt_->setObjectName("camFormat");
    labelEdit_ = new QLineEdit;
    labelEdit_->setObjectName("camLabel");
    labelEdit_->setPlaceholderText(LC_TR("Tên hiển thị (tuỳ chọn)"));
    form->addRow(LC_TR("Thiết bị:"), dev_);
    form->addRow(LC_TR("Chế độ:"), fmt_);
    form->addRow(LC_TR("Nhãn:"), labelEdit_);
    v->addLayout(form);
    auto* rb = new QPushButton(LC_TR("Quét lại thiết bị"));
    rb->setObjectName("camRefresh");
    v->addWidget(rb);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    ok_ = bb->button(QDialogButtonBox::Ok);
    ok_->setText(LC_TR("Thêm"));
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(rb, &QPushButton::clicked, this, [this] { refresh(); });
    connect(dev_, &QComboBox::currentIndexChanged, this, [this] { onDeviceChanged(); });
    refresh();
}

void CameraSourceDialog::refresh() {
    auto b = cameraBackend();
    dev_->clear();
    devs_.clear();
    if (!b->available()) {
        note_->setText(LC_TR("<b>Camera chưa dùng được:</b> ") + b->unavailableReason());
        ok_->setEnabled(false);
        return;
    }
    devs_ = b->list();
    for (const auto& d : devs_) dev_->addItem(d.name, d.id);
    note_->setText(devs_.empty() ? LC_TR("Không thấy camera nào. Cắm thiết bị (hoặc đóng chương trình khác đang dùng nó) rồi bấm Quét lại.")
                                 : QString(LC_TR("Tìm thấy %1 thiết bị.")).arg(devs_.size()));
    ok_->setEnabled(!devs_.empty());
    onDeviceChanged();
}

void CameraSourceDialog::onDeviceChanged() {
    fmt_->clear();
    fmts_.clear();
    const int i = dev_->currentIndex();
    if (i < 0 || i >= int(devs_.size())) return;
    fmt_->addItem(LC_TR("Để thiết bị chọn"));
    fmts_.push_back({0, 0, 0});
    auto f = cameraBackend()->formats(devs_[size_t(i)].id);
    std::sort(f.begin(), f.end(), [](const CameraFormat& a, const CameraFormat& b) { return a.w != b.w ? a.w > b.w : a.fps > b.fps; });
    for (const auto& x : f) { fmt_->addItem(x.text()); fmts_.push_back(x); }
    if (fmts_.size() > 1) fmt_->setCurrentIndex(1);       // mac dinh: do phan giai cao nhat
}

QString CameraSourceDialog::deviceId() const { const int i = dev_->currentIndex(); return i >= 0 && i < int(devs_.size()) ? devs_[size_t(i)].id : QString(); }
QString CameraSourceDialog::deviceName() const { const int i = dev_->currentIndex(); return i >= 0 && i < int(devs_.size()) ? devs_[size_t(i)].name : QString(); }
CameraFormat CameraSourceDialog::format() const { const int i = fmt_->currentIndex(); return i >= 0 && i < int(fmts_.size()) ? fmts_[size_t(i)] : CameraFormat{}; }
QString CameraSourceDialog::label() const { return labelEdit_->text().trimmed(); }

// ---------------- Cua so / slideshow ----------------

WindowSourceDialog::WindowSourceDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(LC_TR("Thêm slideshow PowerPoint / cửa sổ"));
    setObjectName("windowSourceDialog");
    resize(520, 380);
    auto* v = new QVBoxLayout(this);
    note_ = new QLabel;
    note_->setObjectName("winNote");
    note_->setWordWrap(true);
    v->addWidget(note_);
    list_ = new QListWidget;
    list_->setObjectName("winList");
    v->addWidget(list_, 1);
    auto* form = new QFormLayout;
    labelEdit_ = new QLineEdit;
    labelEdit_->setObjectName("winLabel");
    labelEdit_->setPlaceholderText(LC_TR("Tên hiển thị (tuỳ chọn)"));
    form->addRow(LC_TR("Nhãn:"), labelEdit_);
    v->addLayout(form);
    auto* rb = new QPushButton(LC_TR("Quét lại cửa sổ"));
    rb->setObjectName("winRefresh");
    v->addWidget(rb);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    ok_ = bb->button(QDialogButtonBox::Ok);
    ok_->setText(LC_TR("Thêm"));
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(rb, &QPushButton::clicked, this, [this] { refresh(); });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] { if (ok_->isEnabled()) accept(); });
    refresh();
}

void WindowSourceDialog::refresh() {
    auto b = windowCaptureBackend();
    list_->clear();
    targets_.clear();
    if (!b->available()) {
        note_->setText(LC_TR("<b>Chưa bắt được cửa sổ:</b> ") + b->unavailableReason());
        ok_->setEnabled(false);
        return;
    }
    note_->setText(LC_TR("Trong PowerPoint chọn <i>Slide Show → Set Up Slide Show → Browsed by an individual (window)</i> rồi bấm F5. "
                         "Cửa sổ có thể nằm phía sau LumaCast, nhưng đừng thu nhỏ."));
    list_->addItem(LC_TR("Slideshow PowerPoint (tự nhận, mở trước hay sau đều được)"));
    targets_.push_back({kSlideShowTarget, {}, {}});
    for (const CaptureWindow& w : b->list()) {
        list_->addItem(QString("%1  —  %2").arg(w.title.isEmpty() ? w.windowClass : w.title, w.app));
        targets_.push_back({w.app, w.windowClass, w.title});
    }
    list_->setCurrentRow(0);
    ok_->setEnabled(true);
}

WindowTarget WindowSourceDialog::target() const {
    const int i = list_->currentRow();
    return i >= 0 && i < int(targets_.size()) ? targets_[size_t(i)] : WindowTarget{};
}
QString WindowSourceDialog::label() const { return labelEdit_->text().trimmed(); }

// ---------------- NDI ra ----------------

NdiOutputDialog::NdiOutputDialog(const QStringList& channelNames, QWidget* parent) : QDialog(parent) {
    setWindowTitle(LC_TR("Thêm Output NDI"));
    setObjectName("ndiOutputDialog");
    auto* v = new QVBoxLayout(this);
    note_ = new QLabel;
    note_->setObjectName("ndiOutNote");
    note_->setWordWrap(true);
    auto b = ndiBackend();
    note_->setText(b->available() ? LC_TR("Phát Program của một Channel ra mạng. Phần mềm khác (vMix, OBS, Resolume...) sẽ thấy nguồn này trong danh sách NDI.")
                                  : LC_TR("<b>NDI chưa dùng được:</b> ") + b->unavailableReason() + LC_TR("<br>Output vẫn được tạo và sẽ tự phát khi NDI sẵn sàng."));
    v->addWidget(note_);
    {   // NDI(R): dong ghi nhan + lien ket toi ndi.video gan noi dung NDI (SPEC 5.1)
        auto* nl = new QLabel(QString(LC_TR("%1<br><a href=\"https://ndi.video\">Tìm hiểu thêm về NDI® tại ndi.video</a>")).arg(ndiTrademarkNotice().toHtmlEscaped()));
        nl->setObjectName("ndiLink");
        nl->setOpenExternalLinks(true);
        nl->setWordWrap(true);
        v->addWidget(nl);
    }
    auto* form = new QFormLayout;
    name_ = new QLineEdit("LumaCast Program");
    name_->setObjectName("ndiOutName");
    name_->setMaxLength(100);
    channel_ = new QComboBox;
    channel_->setObjectName("ndiOutChannel");
    channel_->addItems(channelNames);
    w_ = new QSpinBox; w_->setRange(16, 8192); w_->setValue(1920); w_->setObjectName("ndiOutW");
    h_ = new QSpinBox; h_->setRange(16, 8192); h_->setValue(1080); h_->setObjectName("ndiOutH");
    audio_ = new QCheckBox(LC_TR("Gửi kèm âm thanh của Channel"));
    audio_->setChecked(true);
    audio_->setObjectName("ndiOutAudio");
    form->addRow(LC_TR("Tên nguồn NDI:"), name_);
    form->addRow("Channel:", channel_);
    form->addRow(LC_TR("Rộng:"), w_);
    form->addRow(LC_TR("Cao:"), h_);
    form->addRow(QString(), audio_);
    v->addLayout(form);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(LC_TR("Thêm"));
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString NdiOutputDialog::name() const { return name_->text().trimmed(); }
int NdiOutputDialog::channel() const { return channel_->currentIndex(); }
QSize NdiOutputDialog::size() const { return QSize(w_->value(), h_->value()); }
bool NdiOutputDialog::audio() const { return audio_->isChecked(); }

} // namespace lc
