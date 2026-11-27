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
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QUndoStack;

namespace lc {

// Xem truoc mot lop phu tren canvas cua Channel (nen = hinh dang o Preview): chinh vi tri/kieu chu ma KHONG dua len Program.
class OverlayPreview : public QWidget {
public:
    explicit OverlayPreview(QWidget* parent = nullptr);
    void show(const OverlayDef* def, int secondsLeft, QSize canvas, const QImage& background);
    // Chu cua lop phu dang xem co bi cat boi hop khong (tinh o do phan giai that cua canvas).
    bool clipped(const OverlayDef& def, int secondsLeft, QSize canvas);
    QSize sizeHint() const override { return {320, 180}; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    OverlayDef def_;
    bool has_ = false;
    int secondsLeft_ = 0;
    QSize canvas_{1920, 1080};
    QImage background_;
    OverlayRasterKey key_, clipKey_;
    OverlayBitmap bitmap_;
    bool clipped_ = false;
};

// Dock "Overlay" (SRC-08): lop phu cua Channel dang chon.
//  - SUA (them/xoa/thu tu/noi dung/kieu/vi tri): di qua editDefs => Hoan tac/Lam lai, bi Khoa van hanh chan.
//  - LIVE (hien/an, Bat dau/Tam dung/Dat lai dong ho): van dung duoc khi khoa, khong vao ngan xep Hoan tac.
class OverlayPanel : public QWidget {
    Q_OBJECT
public:
    OverlayPanel(Project& project, RenderEngine& engine, QUndoStack* undo, QWidget* parent = nullptr);

    void setChannel(int ch);
    int channel() const { return ch_; }
    void setLocked(bool on);
    bool locked() const { return locked_; }
    void refreshLive();                          // goi theo timer UI

    // Sua. Them moi luon o trang thai TAT. Tra id, -1 neu bi khoa / qua gioi han.
    int addOverlay(OverlayType type);
    int addLogo(const QString& imagePath);
    void removeCurrent();
    void moveCurrent(int dir);                   // +1 = len tren (ve sau)
    void editCurrent(const std::function<void(OverlayDef&)>& mutate, const QString& text, int field = -1);
    // Goi boi lenh Hoan tac/Lam lai.
    void applyDefs(int ch, const std::vector<OverlayDef>& defs);

    // Live.
    void setOn(int id, bool on);

    int currentId() const;
    void selectId(int id);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;   // Enter trong o nhap chi xac nhan, KHONG thanh CUT

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
    QListWidget* list_ = nullptr;
    QPushButton* addBtn_[4] = {};
    QPushButton *upBtn_ = nullptr, *downBtn_ = nullptr, *delBtn_ = nullptr;
    QPushButton* toggle_ = nullptr;
    QWidget* clockRow_ = nullptr;
    QLabel* clock_ = nullptr;
    QPushButton *startBtn_ = nullptr, *pauseBtn_ = nullptr, *resetBtn_ = nullptr;
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
};

} // namespace lc
