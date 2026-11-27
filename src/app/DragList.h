#pragma once
#include <QListWidget>
#include <algorithm>
#include <functional>
#include <vector>

namespace lc {

// Danh sach doi thu tu bang keo chuot. KHONG dung QDrag: danh sach khong tu them/bot muc, nen tha ra ngoai chi la huy (khong the
// "xoa bang cach keo ra ngoai", cung khong tha nham sang danh sach khac). Tha trong danh sach thi goi onReorder va de chu du lieu
// sua (qua Hoan tac) roi dung lai danh sach.
class DragList : public QListWidget {
public:
    explicit DragList(QWidget* parent = nullptr);

    // rows = cac hang bi keo (tang dan); before = chen truoc hang nay (count() = cuoi), theo chi so TRUOC khi doi.
    std::function<void(const std::vector<int>& rows, int before)> onReorder;
    void setReorderEnabled(bool on);
    bool reorderEnabled() const { return enabled_; }
    bool dragging() const { return dragging_; }

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

private:
    int gapAt(const QPoint& pos) const;      // vi tri chen theo con tro; -1 = ngoai danh sach
    void cancel();
    bool enabled_ = true;
    bool dragging_ = false;
    int pressRow_ = -1;
    int gap_ = -1;
    QPoint press_;
};

// Doi cho trong mot vector theo ket qua keo: tra thu tu moi (cac phan tu bi keo giu nguyen thu tu giua chung).
template <class T>
std::vector<T> movedRows(const std::vector<T>& v, const std::vector<int>& rows, int before) {
    std::vector<T> moved, rest;
    int at = 0;
    for (int i = 0; i < int(v.size()); ++i) {
        const bool taken = std::find(rows.begin(), rows.end(), i) != rows.end();
        (taken ? moved : rest).push_back(v[size_t(i)]);
        if (!taken && i < before) ++at;
    }
    rest.insert(rest.begin() + at, moved.begin(), moved.end());
    return rest;
}

} // namespace lc
