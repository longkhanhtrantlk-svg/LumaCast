#pragma once
#include "core/Overlay.h"
#include <QWidget>
#include <functional>
#include <vector>

class QHBoxLayout;
class QLabel;
class QPushButton;
class QScrollArea;

namespace lc {

// Dai "Dang hien" duoi man Program: moi lop phu dang hien mot the co nut ✕ (chi AN, khong xoa) va nut "Tat tat ca".
// Chieu cao co dinh de man hinh khong nhay khi bat / tat. Dieu khien LIVE: van dung duoc khi Khoa van hanh.
class NowShowing : public QWidget {
public:
    static constexpr int kHeight = 30;
    explicit NowShowing(QWidget* parent = nullptr);
    void refresh(const std::vector<OverlayFrame>& frames);      // moi lop phu cua kenh dang chon (loc cai dang hien)
    std::function<void(int id)> onHide;
    std::function<void()> onHideAll;

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;       // lan chuot de cuon ngang khi nhieu the

private:
    struct Chip { int id = 0; QWidget* box = nullptr; QLabel* text = nullptr; };
    void rebuild(const std::vector<OverlayFrame>& shown);
    std::vector<Chip> chips_;
    QLabel* none_ = nullptr;
    QHBoxLayout* row_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QPushButton* hideAll_ = nullptr;
};

} // namespace lc
