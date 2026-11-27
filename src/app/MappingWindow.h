#pragma once
#include "render/Engine.h"
#include <QImage>
#include <QTimer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace lc {

class InspectorPanel;

// ---------------- Hinh hoc mapping (diem anh cua Channel, so nguyen) ----------------
// Vung cat luu trong OutputConfig::crop la toa do chuan hoa 0..1; moi thao tac keo/nhap tinh bang diem anh Channel de
// khong troi lech do so thuc.
namespace mapping {

enum Edge { None = 0, Left = 1, Top = 2, Right = 4, Bottom = 8, Move = 16 };

QRect toPixels(const QRectF& crop, QSize canvas);
QRectF toCrop(const QRect& px, QSize canvas);
// Ap dung do doi (dx, dy) len `start` theo canh/goc dang keo; ket qua luon nam trong canvas va rong/cao >= 1.
// others: vung cua cac Output khac cung Channel (de bat dinh); snap = khoang bat dinh (diem anh Channel, 0 = tat).
QRect drag(const QRect& start, int edges, int dx, int dy, QSize canvas, const std::vector<QRect>& others, int snap);
// So diem anh that cua Output (man hinh fullscreen: theo man hinh; cua so: kich thuoc cau hinh x ti le DPI).
QSize outputPixels(const OutputConfig& cfg);

}   // namespace mapping

// Canvas: ve hinh Program cua Channel + vung cat cua tung Output; keo de di chuyen, keo canh/goc de doi kich thuoc,
// phim mui ten nhich 1 diem anh (Shift = 10), Esc huy lan keo dang do, giu Alt de tat bat dinh.
class MappingCanvas : public QWidget {
    Q_OBJECT
public:
    explicit MappingCanvas(InspectorPanel& inspector, QWidget* parent = nullptr);
    void setChannel(int channel, QSize size);
    void setProgramImage(const QImage& img) { image_ = img; update(); }
    void select(int output);                    // chi so Output (-1 = khong chon)
    int selected() const { return selected_; }
    void setSnap(bool on) { snap_ = on; }
    void setReadOnly(bool on);
    bool dragging() const { return dragEdges_ != mapping::None; }
    // Vi tri tren widget cua mot diem tren canvas Channel (cho test va ve).
    QPointF toWidget(const QPointF& channelPx) const;
    QSize sizeHint() const override { return {760, 440}; }
    QSize minimumSizeHint() const override { return {320, 200}; }

signals:
    void selectionChanged(int output);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    std::vector<int> outputs() const;           // Output (man hinh/NDI) thuoc Channel dang xem
    QRect cropPx(int output) const;
    QRectF canvasRect() const;                  // vung ve canvas Channel tren widget (giu ti le)
    QRectF widgetRect(const QRect& px) const;
    int hitEdges(const QRectF& r, const QPointF& pos) const;
    int outputAt(const QPointF& pos) const;
    void endDrag();

    InspectorPanel& inspector_;
    int channel_ = 0;
    QSize size_{1920, 1080};
    QImage image_;
    int selected_ = -1;
    bool snap_ = true;
    bool readOnly_ = false;
    int dragEdges_ = mapping::None;
    QPointF dragFrom_;
    QRect dragStart_;
    QRectF dragStartCrop_;
    int dragMergeId_ = 0;
};

// Cua so "Mapping truc quan". Qt::Tool (con cua cua so chinh) nen phim tat cua cua so chinh (Space = TAKE, Enter = CUT,
// Ctrl+Z...) van chay khi cua so nay dang duoc chon. Moi chinh sua di qua InspectorPanel (Hoan tac + Khoa van hanh).
class MappingWindow : public QWidget {
    Q_OBJECT
public:
    MappingWindow(Project& project, RenderEngine& engine, InspectorPanel& inspector, QWidget* parent);
    void setLocked(bool on);
    void showChannel(int channel);
    MappingCanvas* canvas() const { return canvas_; }

protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private:
    void rebuildOutputs();
    void sync();                                // o nhap + dong thong tin theo Output dang chon
    void applySpin(int field, int value);
    QSize channelSize() const;
    int channel() const;

    Project& project_;
    RenderEngine& engine_;
    InspectorPanel& inspector_;
    MappingCanvas* canvas_ = nullptr;
    QComboBox* chanCombo_ = nullptr;
    QComboBox* outCombo_ = nullptr;
    QSpinBox* spin_[4] = {};                    // x, y, rong, cao (diem anh Channel)
    QPushButton* oneToOne_ = nullptr;
    QPushButton* full_ = nullptr;
    QCheckBox* snapBox_ = nullptr;
    QLabel* info_ = nullptr;
    QLabel* note_ = nullptr;
    QTimer imageTimer_;
    bool locked_ = false;
};

}   // namespace lc
