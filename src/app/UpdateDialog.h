#pragma once
#include "core/Update.h"
#include <QDialog>
#include <functional>

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;

namespace lc {

// Hop thoai Cap nhat. La cua so Tool con (khong chan): Space/Enter van la TAKE/CUT, nut khong nhan tieu diem nen ban phim
// khong the vo tinh cap nhat. Cap nhat / quay ve chi di qua onApply / onRollback cua cua so chinh (co xac nhan).
class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    UpdateDialog(update::Service& service, std::function<void()> onApply, std::function<void()> onRollback, QWidget* parent = nullptr);
    void sync();                           // goi khi trang thai hoac khoa van hanh doi

private:
    update::Service& service_;
    QLabel *current_, *status_, *notes_;
    QProgressBar* progress_;
    QPushButton *check_, *download_, *apply_, *rollback_;
    QCheckBox* auto_;
};

} // namespace lc
