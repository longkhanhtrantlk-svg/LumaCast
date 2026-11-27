#pragma once
#include <QObject>
#include <QWidget>

class QDockWidget;
class QMainWindow;
class QMenu;
class QTabBar;

namespace lc {

// Thanh ten cua mot khung (thay thanh tieu de mac dinh). Hai dang:
//  - dung mot minh: mot "the" dang chon mang ten khung (cung kieu voi hang tab), keo de doi cho khung;
//  - da gop tab: chi con mot vach mong lien mau noi dung, keo vach nay = keo CA NHOM (thao tac keo goc cua Qt).
// Khong tu xu ly chuot: su kien troi len QDockWidget nen Qt keo / tha / nhap doi nhu thanh tieu de thuong.
class DockTitle : public QWidget {
public:
    static constexpr int kHeaderHeight = 28, kStripHeight = 8;
    explicit DockTitle(QDockWidget* dock);
    void setTabbed(bool on);
    bool tabbed() const { return tabbed_; }
    void setLocked(bool on);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    QString shownText() const;                    // ten dang ve tren the (co the bi cat khi khung qua hep)

protected:
    void paintEvent(QPaintEvent*) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    void syncHint();
    QDockWidget* dock_;
    bool tabbed_ = false, locked_ = false;
};

// Kieu "hang tab tren dau" (nhu Photoshop) cho cac khung cua mot cua so: ten khung nam tren, tab dang chon lien mau noi dung,
// khong co dong ten lap lai. Chuot phai vao ten = menu Tach khung / Gan lai / Dong.
class DockChrome : public QObject {
public:
    explicit DockChrome(QMainWindow* window);
    void adopt();                           // goi sau khi cac khung da duoc tao
    void schedule();                        // cap nhat o vong su kien ke tiep (gop nhieu thay doi)
    void refresh();                         // cap nhat ngay
    void setLocked(bool on);                // khoa: khong keo tab / vach, menu bi mo
    // Menu cua mot khung (nguoi goi huy). Dung cho chuot phai va cho test.
    QMenu* menuFor(QDockWidget* dock, QWidget* parent = nullptr);
    static bool isDockTabBar(const QTabBar* bar);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    QDockWidget* dockNamed(const QString& title) const;
    void style(QTabBar* bar);
    bool tabbed(const QDockWidget* dock) const;
    QMainWindow* window_;
    bool locked_ = false;
    bool pending_ = false;
};

} // namespace lc
