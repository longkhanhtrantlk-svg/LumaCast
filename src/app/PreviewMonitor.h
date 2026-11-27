#pragma once
#include "MonitorWidget.h"
#include <QRectF>
#include <functional>
#include <vector>

namespace lc {

// Man Preview co the keo hinh bang chuot: keo trong khung = doi vi tri, keo canh = doi co mot chieu, keo goc = doi co giu ti le.
// Man chi biet cac khung (chuan hoa theo Scene); cua so chinh doc/ghi Layer.
class PreviewMonitor : public MonitorWidget {
public:
    struct Box { int layer = 0; QRectF rect; };       // thu tu tu duoi len tren (phan tu cuoi o tren cung)
    explicit PreviewMonitor(const QColor& color, QWidget* parent = nullptr);

    // key doi (Scene khac) = bo chon. onAir: Scene nay dang o Program (canh bao). selected: layer dang chon (-1 = khong).
    void setBoxes(const void* key, std::vector<Box> boxes, int selected, bool onAir, bool editable);
    std::function<void(int layer)> onPick;                                     // bam len mot hinh
    std::function<void(int layer, const QRectF& rect, int gesture)> onDrag;    // gesture rieng cho moi lan keo
    bool active() const { return active_; }                                    // dang hien khung chon
    QRect boxRect(const QRectF& normalized) const;

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void paintExtra(QPainter& p) override;

private:
    enum Edge { Left = 1, Right = 2, Top = 4, Bottom = 8, Move = 16 };
    const Box* selectedBox() const;
    int edgesAt(const QRect& box, const QPoint& p) const;
    int layerAt(const QPoint& p) const;            // layer tren cung duoi con tro, -1 neu khong co
    void shape(int edges);

    const void* key_ = nullptr;
    std::vector<Box> boxes_;
    int selected_ = -1, hover_ = -1;
    bool onAir_ = false, editable_ = false, active_ = false;
    int mode_ = 0, dragLayer_ = -1, gesture_ = 0;
    bool moved_ = false;
    QPoint press_;
    QRectF start_;
};

} // namespace lc
