#include "InputDialog.h"
#include "core/AppSettings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace lc {

InputDialog::InputDialog(MidiService& midi, const QStringList& outputNames, const KeyMap& keys, QWidget* parent)
    : QDialog(parent), midi_(midi), original_(midi.config()), draft_(midi.config()) {
    setWindowTitle("Phím tắt và MIDI");
    setObjectName("inputDialog");
    resize(860, 640);
    midi_.setSuspended(true);                              // trong luc gan nut: khong chay lenh tu MIDI
    auto* root = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    midiOn_ = new QCheckBox("Bật điều khiển MIDI");
    midiOn_->setObjectName("midiEnable");
    midiOn_->setChecked(draft_.enabled);
    device_ = new QComboBox;
    device_->setObjectName("midiDevice");
    device_->setMinimumWidth(220);
    auto* again = new QPushButton("Kết nối lại");
    again->setObjectName("midiReconnect");
    again->setAutoDefault(false);
    top->addWidget(midiOn_);
    top->addWidget(new QLabel("Thiết bị:"));
    top->addWidget(device_, 1);
    top->addWidget(again);
    root->addLayout(top);
    status_ = new QLabel;
    status_->setObjectName("midiStatus");
    status_->setWordWrap(true);
    root->addWidget(status_);
    last_ = new QLabel;
    last_->setObjectName("midiLast");
    root->addWidget(last_);

    auto* help = new QLabel("Space luôn là TAKE, Enter luôn là CUT (TAKE/CUT có thể thêm một phím phụ). Bấm <b>Học</b> rồi nhấn nút trên thiết bị MIDI. "
                            "Nút MIDI tác động lên kênh và hiệu ứng đang chọn; chọn Preview từ MIDI không bao giờ đổi Program. "
                            "Trong lúc hộp thoại này mở, nút MIDI không điều khiển chương trình. Không có phím hay nút nào mở khoá được.");
    help->setWordWrap(true);
    root->addWidget(help);

    // ---- bang lenh ----
    int outputs = int(outputNames.size());                  // hien ca Output chua co trong project nhung da gan (de thay va xoa duoc)
    for (const QString& id : keys.keys()) outputs = std::max(outputs, id.startsWith("output.") ? LiveAction::fromId(id).index : 0);
    for (const QString& id : draft_.map.keys()) outputs = std::max(outputs, id.startsWith("output.") ? LiveAction::fromId(id).index : 0);
    const QStringList ids = LiveAction::allIds(outputs);

    table_ = new QTableWidget(int(ids.size()), 5);
    table_->setObjectName("inputTable");
    table_->setHorizontalHeaderLabels({"Lệnh", "Phím tắt", "Nút MIDI", "", ""});
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->setColumnWidth(1, 230);
    table_->setColumnWidth(2, 160);
    table_->setColumnWidth(3, 130);
    table_->setColumnWidth(4, 60);
    for (int r = 0; r < ids.size(); ++r) {
        const LiveAction a = LiveAction::fromId(ids[r]);
        Row row;
        row.id = ids[r];
        const bool isOutput = a.kind == LiveAction::OutputBlack || a.kind == LiveAction::OutputFreeze || a.kind == LiveAction::OutputPattern;
        row.name = a.label(isOutput && a.index <= outputNames.size() ? outputNames[a.index - 1] : QString());
        if (isOutput && a.index > outputNames.size()) row.name += "  (không có trong project này)";
        table_->setItem(r, 0, new QTableWidgetItem(row.name));

        auto* keyCell = new QWidget;
        auto* kl = new QHBoxLayout(keyCell);
        kl->setContentsMargins(2, 0, 2, 0);
        const QString fixed = shortcuts::fixedKeysText(a);
        if (!fixed.isEmpty()) kl->addWidget(new QLabel(fixed + "  +"));
        row.key = new QKeySequenceEdit;
        row.key->setObjectName("key:" + row.id);
        row.key->setMaximumSequenceLength(1);
        row.key->setClearButtonEnabled(true);
        row.key->setKeySequence(keys.value(row.id));
        if (auto* line = row.key->findChild<QLineEdit*>()) line->setPlaceholderText(fixed.isEmpty() ? "Nhấn phím…" : "Phím phụ…");
        kl->addWidget(row.key, 1);
        table_->setCellWidget(r, 1, keyCell);
        connect(row.key, &QKeySequenceEdit::keySequenceChanged, this, [this] { validate(); });

        row.midi = new QLabel;
        row.midi->setObjectName("midiLabel:" + row.id);
        row.midi->setContentsMargins(6, 0, 6, 0);
        table_->setCellWidget(r, 2, row.midi);
        if (a.externalAllowed()) {
            row.learn = new QPushButton("Học");
            row.learn->setObjectName("learn:" + row.id);
            row.learn->setAutoDefault(false);
            table_->setCellWidget(r, 3, row.learn);
            connect(row.learn, &QPushButton::clicked, this, [this, id = row.id] { if (learning_ == id) stopLearn(); else startLearn(id); });
            auto* clr = new QPushButton("Xoá");
            clr->setObjectName("clearMidi:" + row.id);
            clr->setAutoDefault(false);
            table_->setCellWidget(r, 4, clr);
            connect(clr, &QPushButton::clicked, this, [this, id = row.id] {
                draft_.map.remove(id);
                if (learning_ == id) learning_.clear();
                refreshRows();
                validate();
            });
        }
        rows_ << row;
    }
    root->addWidget(table_, 1);

    err_ = new QLabel;
    err_->setObjectName("inputError");
    err_->setStyleSheet("color:#ff6b6b;font-weight:bold");
    err_->setWordWrap(true);
    root->addWidget(err_);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto* restore = bb->addButton("Khôi phục phím mặc định", QDialogButtonBox::ResetRole);
    restore->setObjectName("restoreDefaults");
    restore->setAutoDefault(false);
    root->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &InputDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &InputDialog::reject);
    connect(restore, &QPushButton::clicked, this, [this] {
        const KeyMap def = shortcuts::defaults();
        for (const Row& r : std::as_const(rows_)) r.key->setKeySequence(def.value(r.id));
        validate();
    });

    connect(midiOn_, &QCheckBox::toggled, this, [this] { applyDevice(); });
    connect(device_, &QComboBox::activated, this, [this] { applyDevice(); });
    connect(again, &QPushButton::clicked, this, [this] { midi_.reconnect(); refreshDevices(); refreshRows(); });
    connect(&midi_, &MidiService::statusChanged, this, [this] {
        if (!midi_.connected()) learning_.clear();
        refreshDevices();
        refreshRows();
    });
    connect(&midi_, &MidiService::pressed, this, [this](const MidiBinding& b) {
        last_->setText("Vừa nhận: " + b.label());
        if (learning_.isEmpty()) return;
        draft_.map.insert(learning_, b);
        learning_.clear();
        refreshRows();
        validate();
    });
    auto* scan = new QTimer(this);                          // danh sach thiet bi doi khi cam/rut trong luc hop thoai mo
    connect(scan, &QTimer::timeout, this, [this] { if (midi_.devices() != deviceList_) refreshDevices(); });
    scan->start(1000);

    refreshDevices();
    refreshRows();
    validate();
    table_->setFocus();                                     // khong de o nhap phim nhan focus san (se an phim go nham)
}

InputDialog::~InputDialog() { finish(); }

void InputDialog::finish() {
    if (finished_) return;
    finished_ = true;
    learning_.clear();
    if (!saved_ && (midi_.config().enabled != original_.enabled || midi_.config().device != original_.device)) midi_.apply(original_);
    midi_.setSuspended(false);
}

void InputDialog::done(int r) {
    finish();
    QDialog::done(r);
}

void InputDialog::refreshDevices() {
    if (device_->view()->isVisible()) return;              // dang mo danh sach: khong doi duoi tay nguoi dung
    deviceList_ = midi_.devices();
    QSignalBlocker block(device_);
    device_->clear();
    device_->addItem("(chưa chọn)", QString());
    for (const QString& d : std::as_const(deviceList_)) device_->addItem(d, d);
    if (!draft_.device.isEmpty() && !deviceList_.contains(draft_.device)) device_->addItem(draft_.device + "  (chưa cắm)", draft_.device);
    device_->setCurrentIndex(std::max(0, device_->findData(draft_.device)));
    status_->setText(midi_.statusText() + (deviceList_.isEmpty() ? "  ·  Máy chưa thấy cổng MIDI nào." : ""));
}

void InputDialog::applyDevice() {
    draft_.enabled = midiOn_->isChecked();
    draft_.device = device_->currentData().toString();
    learning_.clear();
    MidiConfig c = original_;                               // chi doi cong de hoc nut; bang gan nut chi doi khi bam OK
    c.enabled = draft_.enabled;
    c.device = draft_.device;
    midi_.apply(c);
    refreshDevices();
    refreshRows();
}

void InputDialog::startLearn(const QString& actionId) {
    if (!midi_.connected() || !LiveAction::fromId(actionId).externalAllowed()) return;
    learning_ = actionId;
    refreshRows();
}

void InputDialog::stopLearn() {
    learning_.clear();
    refreshRows();
}

void InputDialog::refreshRows() {
    for (const Row& r : std::as_const(rows_)) {
        if (!r.learn) {                                     // khoa van hanh: chi tren may
            r.midi->setText("— (chỉ trên máy)");
            continue;
        }
        const bool me = learning_ == r.id;
        r.midi->setText(me ? "Nhấn nút trên thiết bị…" : draft_.map.value(r.id).label());
        r.learn->setText(me ? "Huỷ học" : "Học");
        r.learn->setEnabled(midi_.connected());
    }
}

KeyMap InputDialog::keys() const {
    KeyMap m;
    for (const Row& r : rows_)
        if (!r.key->keySequence().isEmpty()) m.insert(r.id, r.key->keySequence());
    return m;
}

QString InputDialog::validate() {
    const KeyMap k = keys();
    const QStringList dupKeys = shortcuts::duplicates(k);
    const QStringList dupMidi = MidiConfig::duplicates(draft_.map);
    QStringList errors;
    for (int i = 0; i < rows_.size(); ++i) {
        const Row& r = rows_[i];
        QStringList why;
        const QKeySequence seq = k.value(r.id);
        const QString reserved = shortcuts::reservedReason(seq);
        if (!reserved.isEmpty()) why << QString("phím %1: %2").arg(seq.toString(QKeySequence::NativeText), reserved);
        if (dupKeys.contains(r.id)) why << QString("trùng phím %1").arg(seq.toString(QKeySequence::NativeText));
        if (dupMidi.contains(r.id)) why << "trùng nút MIDI " + draft_.map.value(r.id).label();
        QTableWidgetItem* it = table_->item(i, 0);
        if (!it) continue;
        it->setText(why.isEmpty() ? r.name : "⚠ " + r.name);
        it->setForeground(why.isEmpty() ? palette().text() : QBrush(QColor(0xff, 0x6b, 0x6b)));
        it->setToolTip(why.join("\n"));
        for (const QString& w : std::as_const(why)) errors << r.name + ": " + w;
    }
    QString text = errors.mid(0, 4).join("\n");
    if (errors.size() > 4) text += QString("\n… và %1 lỗi nữa").arg(errors.size() - 4);
    err_->setText(text);
    return text;
}

QString InputDialog::applyAndSave() {
    stopLearn();
    const QString err = validate();
    if (!err.isEmpty()) return err;
    QSettings st = appSettings();
    shortcuts::save(st, keys());
    draft_.enabled = midiOn_->isChecked();
    draft_.device = device_->currentData().toString();
    draft_.save(st);
    midi_.apply(draft_);
    saved_ = true;
    qInfo().noquote() << QString("Đã lưu phím tắt và MIDI (%1 phím, %2 nút MIDI, MIDI %3)")
                             .arg(keys().size()).arg(draft_.map.size()).arg(draft_.enabled ? "BẬT" : "TẮT");
    return {};
}

void InputDialog::accept() {
    if (applyAndSave().isEmpty()) QDialog::accept();        // co loi: giu hop thoai de sua
}

} // namespace lc
