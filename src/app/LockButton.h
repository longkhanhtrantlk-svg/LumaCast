#pragma once
#include <QPushButton>
#include <QTimer>

namespace lc {

// Nut Khoa van hanh: khi mo khoa, bam mot lan la khoa; khi dang khoa phai GIU nut (mac dinh 1 giay) moi mo khoa,
// tranh bam nham/cham nham trong luc dang On Air. Nha som thi huy.
class LockButton : public QPushButton {
    Q_OBJECT
public:
    explicit LockButton(QWidget* parent = nullptr);
    bool isLocked() const { return locked_; }
    void setLocked(bool on);
    void setShortcutText(const QString& key);       // phim khoa hien trong goi y (rong = khong co)
    int holdMs() const { return holdMs_; }
    void setHoldMs(int ms) { holdMs_ = ms; }
    bool holding() const { return timer_.isActive(); }

signals:
    void lockRequested();
    void unlockConfirmed();

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;

private:
    void startHold();
    void cancelHold();
    bool locked_ = false;
    QString shortcut_ = "Ctrl+L";
    int holdMs_ = 1000;
    QTimer timer_;
};

} // namespace lc
