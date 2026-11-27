#include "LockButton.h"
#include <QMouseEvent>

namespace lc {

LockButton::LockButton(QWidget* parent) : QPushButton(parent) {
    setObjectName("lockButton");
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, [this] { setDown(false); emit unlockConfirmed(); });
    connect(this, &QPushButton::clicked, this, [this] { if (!locked_) emit lockRequested(); });
    setLocked(false);
}

void LockButton::setLocked(bool on) {
    locked_ = on;
    cancelHold();
    setText(on ? "🔒 ĐANG KHOÁ — giữ 1 giây để mở" : "🔓 Khoá vận hành");
    setToolTip(on ? "Mọi điều khiển SỬA đang bị khoá; điều khiển live vẫn hoạt động.\nGiữ nút này khoảng 1 giây để mở khoá."
                  : "Khoá các điều khiển sửa (Inspector, Timeline, Nhập media, Plug-in, Hoàn tác…) khi đang On Air.\nCUT/TAKE/FTB/Đen/chọn Scene… vẫn dùng được." + (shortcut_.isEmpty() ? QString() : " (" + shortcut_ + ")"));
    setStyleSheet(on ? "QPushButton{background:#8a1c1c;color:white;font-weight:bold;border:1px solid #ff6b6b}" : QString());
}

void LockButton::setShortcutText(const QString& key) {
    shortcut_ = key;
    if (!locked_) setLocked(false);
}

void LockButton::startHold() { if (!timer_.isActive()) { setDown(true); timer_.start(holdMs_); } }
void LockButton::cancelHold() { timer_.stop(); setDown(false); }

void LockButton::mousePressEvent(QMouseEvent* e) {
    if (!locked_) { QPushButton::mousePressEvent(e); return; }
    if (e->button() == Qt::LeftButton) startHold();
}
void LockButton::mouseReleaseEvent(QMouseEvent* e) {
    if (!locked_) { QPushButton::mouseReleaseEvent(e); return; }
    cancelHold();
}
void LockButton::mouseMoveEvent(QMouseEvent* e) {
    if (locked_ && !rect().contains(e->position().toPoint())) cancelHold();       // keo chuot ra khoi nut = huy
    else if (!locked_) QPushButton::mouseMoveEvent(e);
}
// Co y KHONG mo khoa bang ban phim: Space/Enter luon la TAKE/CUT (phim tat cua MainWindow), ke ca khi nut nay dang giu focus.
void LockButton::focusOutEvent(QFocusEvent* e) { cancelHold(); QPushButton::focusOutEvent(e); }

} // namespace lc
