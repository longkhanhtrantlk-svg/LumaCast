#include "LiveDialogs.h"
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
    setWindowTitle("Thêm nguồn NDI");
    setObjectName("ndiSourceDialog");
    resize(520, 420);
    auto* v = new QVBoxLayout(this);
    note_ = new QLabel;
    note_->setObjectName("ndiNote");
    note_->setWordWrap(true);
    v->addWidget(note_);
    {   // NDI(R): dong ghi nhan + lien ket toi ndi.video gan noi dung NDI (SPEC 5.1)
        auto* nl = new QLabel(QString("%1<br><a href=\"https://ndi.video\">Tìm hiểu thêm về NDI® tại ndi.video</a>").arg(ndiTrademarkNotice().toHtmlEscaped()));
        nl->setObjectName("ndiLink");
        nl->setOpenExternalLinks(true);
        nl->setWordWrap(true);
        v->addWidget(nl);
    }
    list_ = new QListWidget;
    list_->setObjectName("ndiList");
    v->addWidget(list_, 1);
    refreshBtn_ = new QPushButton("Quét lại");
    refreshBtn_->setObjectName("ndiRefresh");
    v->addWidget(refreshBtn_);
    auto* form = new QFormLayout;
    nameEdit_ = new QLineEdit;
    nameEdit_->setObjectName("ndiName");
    nameEdit_->setPlaceholderText("MÁY (Tên nguồn) — chọn ở trên hoặc nhập tay");
    labelEdit_ = new QLineEdit;
    labelEdit_->setObjectName("ndiLabel");
    labelEdit_->setPlaceholderText("Tên hiển thị (tuỳ chọn)");
    form->addRow("Nguồn NDI:", nameEdit_);
    form->addRow("Nhãn:", labelEdit_);
    v->addLayout(form);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    ok_ = bb->button(QDialogButtonBox::Ok);
    ok_->setText("Thêm");
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
        note_->setText("<b>NDI chưa dùng được:</b> " + b->unavailableReason() +
                       "<br>Cài NDI Runtime (miễn phí, từ ndi.video) rồi khởi động lại LumaCast. Bạn vẫn có thể thêm nguồn bằng cách nhập tên — nguồn sẽ tự kết nối khi NDI sẵn sàng.");
        return;
    }
    if (busy_.exchange(true)) return;
    note_->setText("Đang quét mạng tìm nguồn NDI...");
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
    note_->setText(names.isEmpty() ? "Không thấy nguồn NDI nào trên mạng. Kiểm tra mạng/tường lửa hoặc nhập tên nguồn bằng tay."
                                   : QString("Tìm thấy %1 nguồn NDI. Chọn một nguồn:").arg(names.size()));
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
    setWindowTitle("Thêm camera / card capture");
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
    labelEdit_->setPlaceholderText("Tên hiển thị (tuỳ chọn)");
    form->addRow("Thiết bị:", dev_);
    form->addRow("Chế độ:", fmt_);
    form->addRow("Nhãn:", labelEdit_);
    v->addLayout(form);
    auto* rb = new QPushButton("Quét lại thiết bị");
    rb->setObjectName("camRefresh");
    v->addWidget(rb);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    ok_ = bb->button(QDialogButtonBox::Ok);
    ok_->setText("Thêm");
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
        note_->setText("<b>Camera chưa dùng được:</b> " + b->unavailableReason());
        ok_->setEnabled(false);
        return;
    }
    devs_ = b->list();
    for (const auto& d : devs_) dev_->addItem(d.name, d.id);
    note_->setText(devs_.empty() ? "Không thấy camera nào. Cắm thiết bị (hoặc đóng chương trình khác đang dùng nó) rồi bấm Quét lại."
                                 : QString("Tìm thấy %1 thiết bị.").arg(devs_.size()));
    ok_->setEnabled(!devs_.empty());
    onDeviceChanged();
}

void CameraSourceDialog::onDeviceChanged() {
    fmt_->clear();
    fmts_.clear();
    const int i = dev_->currentIndex();
    if (i < 0 || i >= int(devs_.size())) return;
    fmt_->addItem("Để thiết bị chọn");
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

// ---------------- NDI ra ----------------

NdiOutputDialog::NdiOutputDialog(const QStringList& channelNames, QWidget* parent) : QDialog(parent) {
    setWindowTitle("Thêm Output NDI");
    setObjectName("ndiOutputDialog");
    auto* v = new QVBoxLayout(this);
    note_ = new QLabel;
    note_->setObjectName("ndiOutNote");
    note_->setWordWrap(true);
    auto b = ndiBackend();
    note_->setText(b->available() ? "Phát Program của một Channel ra mạng. Phần mềm khác (vMix, OBS, Resolume...) sẽ thấy nguồn này trong danh sách NDI."
                                  : "<b>NDI chưa dùng được:</b> " + b->unavailableReason() + "<br>Output vẫn được tạo và sẽ tự phát khi NDI sẵn sàng.");
    v->addWidget(note_);
    {   // NDI(R): dong ghi nhan + lien ket toi ndi.video gan noi dung NDI (SPEC 5.1)
        auto* nl = new QLabel(QString("%1<br><a href=\"https://ndi.video\">Tìm hiểu thêm về NDI® tại ndi.video</a>").arg(ndiTrademarkNotice().toHtmlEscaped()));
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
    audio_ = new QCheckBox("Gửi kèm âm thanh của Channel");
    audio_->setChecked(true);
    audio_->setObjectName("ndiOutAudio");
    form->addRow("Tên nguồn NDI:", name_);
    form->addRow("Channel:", channel_);
    form->addRow("Rộng:", w_);
    form->addRow("Cao:", h_);
    form->addRow(QString(), audio_);
    v->addLayout(form);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText("Thêm");
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString NdiOutputDialog::name() const { return name_->text().trimmed(); }
int NdiOutputDialog::channel() const { return channel_->currentIndex(); }
QSize NdiOutputDialog::size() const { return QSize(w_->value(), h_->value()); }
bool NdiOutputDialog::audio() const { return audio_->isChecked(); }

} // namespace lc
