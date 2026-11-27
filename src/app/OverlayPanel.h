#pragma once
#include "render/Engine.h"
#include "render/OverlayRaster.h"
#include <QWidget>
#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QSplitter;
class QToolButton;
class QUndoStack;

namespace lc {

class DragList;
class OverlayRow;

// Vi tri dat nhanh cua mot lop phu tren canvas.
enum class OverlayPlace { Center, TopLeft, TopRight, BottomLeft, BottomRight };
// Hop moi (chuan hoa) khi dat nhanh `rect` vao `place`, cach mep 3% chieu cao canvas.
QRectF placedRect(const QRectF& rect, OverlayPlace place, QSize canvas);

// Xem truoc mot lop phu tren canvas cua Channel (nen = hinh dang o Preview), keo chuot de dat vi tri / doi co.
class OverlayPreview : public QWidget {
public:
    explicit OverlayPreview(QWidget* parent = nullptr);
    void show(const OverlayDef* def, int secondsLeft, QSize canvas, const QImage& background);
    // Chu cua lop phu dang xem co bi cat boi hop khong (tinh o do phan giai that cua canvas).
    bool clipped(const OverlayDef& def, int secondsLeft, QSize canvas);
    void setEditable(bool on);
    QRect viewRect() const;                      // vung canvas trong widget
    QRect boxRect() const;                       // hop cua lop phu (toa do widget)
    // Dang keo: hop moi (chuan hoa), co chu moi (keo goc doi ca co chu); `gesture` rieng cho moi lan keo (de gop Hoan tac).
    std::function<void(const QRectF& rect, int fontSize, int gesture)> onDrag;
    QSize sizeHint() const override { return {240, 140}; }
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
private:
    enum Edge { Left = 1, Right = 2, Top = 4, Bottom = 8, Move = 16 };
    int hit(const QPoint& p) const;
    OverlayDef def_;
    bool has_ = false, editable_ = true;
    int secondsLeft_ = 0;
    QSize canvas_{1920, 1080};
    QImage background_;
    OverlayRasterKey key_, clipKey_;
    OverlayBitmap bitmap_;
    bool clipped_ = false;
    int mode_ = 0;                               // dang keo gi (to hop Edge), 0 = khong
    bool moved_ = false;
    QPoint press_;
    QRectF startRect_;
    int startFont_ = 0;
    int gesture_ = 0;
};

// Dock "Overlay" (SRC-08): lop phu cua Channel dang chon.
//  - Danh sach: moi muc mot dong (tay keo, mat hien/an, ten, dong ho, thung rac); dong dang hien nen do; keo de doi thu tu.
//  - SUA (them/xoa/thu tu/noi dung/kieu/vi tri): di qua editDefs => Hoan tac/Lam lai, bi Khoa van hanh chan.
//  - LIVE (hien/an, chay/tam dung/dat lai dong ho): van dung duoc khi khoa, khong vao ngan xep Hoan tac.
class OverlayPanel : public QWidget {
    Q_OBJECT
public:
    OverlayPanel(Project& project, RenderEngine& engine, QUndoStack* undo, QWidget* parent = nullptr);

    void setChannel(int ch);
    int channel() const { return ch_; }
    void setLocked(bool on);
    bool locked() const { return locked_; }
    void refreshLive();                          // goi theo timer UI
    void preferList(bool on);                    // true: danh sach chiem phan lon (dang dien), false: chia cho phan chinh sua

    // Sua. Them moi luon o trang thai TAT. Tra id, -1 neu bi khoa / qua gioi han.
    int addOverlay(OverlayType type);
    int addLogo(const QString& imagePath);
    void removeCurrent();
    void removeId(int id);
    void moveCurrent(int dir);                   // +1 = len tren (ve sau)
    void reorder(const std::vector<int>& rows, int before);    // theo hang cua danh sach (hang 0 = lop tren cung)
    void place(OverlayPlace where);
    void editCurrent(const std::function<void(OverlayDef&)>& mutate, const QString& text, int field = -1);
    // Goi boi lenh Hoan tac/Lam lai.
    void applyDefs(int ch, const std::vector<OverlayDef>& defs);

    // Live.
    void setOn(int id, bool on);
    void toggleClock(int id);                    // chay <-> tam dung
    void resetClock(int id);
    void hideAll();
    void testAlert();                            // "Nghe thu": vai tieng mau qua bus cua kenh

    int currentId() const;
    void selectId(int id);
    DragList* list() const { return list_; }
    QWidget* row(int id) const;                  // dong cua lop phu trong danh sach
    OverlayPreview* preview() const { return preview_; }

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;   // Enter trong o nhap chi xac nhan (KHONG CUT); Delete trong danh sach = xoa

private:
    const OverlayDef* current() const;
    OverlayStack* stack() const;
    void editDefs(std::vector<OverlayDef> after, const QString& text, int mergeId = -1);
    int add(OverlayDef d);                       // them vao tren cung, trang thai TAT
    void reload(int selectId);                   // doc lai danh sach + o nhap
    void syncProps();
    void pickColor(QColor OverlayDef::*field, const QString& title);
    void chooseImage();
    QWidget* buildProps();

    Project& project_;
    RenderEngine& engine_;
    QUndoStack* undo_;
    int ch_ = 0;
    bool locked_ = false;
    bool syncing_ = false;
    std::vector<OverlayDef> defs_;               // ban sao cua channel dang chon (UI la noi duy nhat sua dinh nghia)

    QLabel* title_ = nullptr;
    QSplitter* split_ = nullptr;
    DragList* list_ = nullptr;
    QPushButton* addBtn_[4] = {};
    QPushButton* placeBtn_[5] = {};
    QLabel* liveWarn_ = nullptr;
    OverlayPreview* preview_ = nullptr;
    QLabel* warn_ = nullptr;

    QWidget* props_ = nullptr;
    QFormLayout* form_ = nullptr;
    QLineEdit* name_ = nullptr;
    QPlainTextEdit* text_ = nullptr;
    QLineEdit* image_ = nullptr;
    QWidget* imageRow_ = nullptr;
    QFontComboBox* font_ = nullptr;
    QSpinBox* fontSize_ = nullptr;
    QWidget* styleRow_ = nullptr;
    QCheckBox *bold_ = nullptr, *italic_ = nullptr;
    QPushButton *color_ = nullptr, *outlineColor_ = nullptr, *bgColor_ = nullptr;
    QDoubleSpinBox* outlineWidth_ = nullptr;
    QComboBox* align_ = nullptr;
    QDoubleSpinBox* rect_[4] = {};
    QWidget *posRow_ = nullptr, *sizeRow_ = nullptr, *durationRow_ = nullptr;
    QDoubleSpinBox *opacity_ = nullptr, *fade_ = nullptr, *speed_ = nullptr;
    QSpinBox *minutes_ = nullptr, *seconds_ = nullptr;
    QWidget* alertRow_ = nullptr;
    QCheckBox* alert_ = nullptr;
    QPushButton* alertTest_ = nullptr;
    QSpinBox *alertStart_ = nullptr, *alertFast_ = nullptr, *alertVolume_ = nullptr;
    QLabel* alertNote_ = nullptr;
};

} // namespace lc
