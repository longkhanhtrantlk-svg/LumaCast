#include "DragList.h"
#include "Theme.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

namespace lc {

DragList::DragList(QWidget* parent) : QListWidget(parent) {
    setDragDropMode(QAbstractItemView::NoDragDrop);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setResizeMode(QListView::Adjust);          // khung hep lai thi dong (va nut trong dong) hep theo, khong bi cat
}

void DragList::setReorderEnabled(bool on) {
    enabled_ = on;
    if (!on) cancel();
}

void DragList::cancel() {
    dragging_ = false;
    pressRow_ = -1;
    gap_ = -1;
    unsetCursor();
    viewport()->update();
}

int DragList::gapAt(const QPoint& pos) const {
    if (!viewport()->rect().adjusted(0, -24, 0, 24).contains(pos)) return -1;
    for (int r = 0; r < count(); ++r) {
        const QRect rect = visualItemRect(item(r));
        if (pos.y() < rect.center().y()) return r;
    }
    return count();
}

void DragList::mousePressEvent(QMouseEvent* e) {
    cancel();
    if (e->button() == Qt::LeftButton) {
        press_ = e->position().toPoint();
        const QModelIndex at = indexAt(press_);
        pressRow_ = at.isValid() ? at.row() : -1;
        // bam vao muc DANG CHON (khong giu Ctrl/Shift): giu nguyen lua chon nhieu muc de keo ca nhom
        if (pressRow_ >= 0 && item(pressRow_)->isSelected() && selectedItems().size() > 1 && e->modifiers() == Qt::NoModifier) return;
    }
    QListWidget::mousePressEvent(e);
}

void DragList::mouseMoveEvent(QMouseEvent* e) {
    if (!(e->buttons() & Qt::LeftButton) || pressRow_ < 0) {
        QListWidget::mouseMoveEvent(e);
        return;
    }
    const QPoint pos = e->position().toPoint();
    if (!dragging_ && enabled_ && (pos - press_).manhattanLength() >= QApplication::startDragDistance()) {
        dragging_ = true;
        setCursor(Qt::ClosedHandCursor);
    }
    if (!dragging_) return;                                  // khong chuyen cho lop cha: keo khong phai la quet chon
    if (pos.y() < 12) verticalScrollBar()->setValue(verticalScrollBar()->value() - 12);
    else if (pos.y() > viewport()->height() - 12) verticalScrollBar()->setValue(verticalScrollBar()->value() + 12);
    gap_ = gapAt(pos);
    viewport()->update();
}

void DragList::mouseReleaseEvent(QMouseEvent* e) {
    if (!dragging_) {
        const int row = pressRow_;
        pressRow_ = -1;
        // bam (khong keo) vao mot muc trong nhom dang chon: thu lua chon ve rieng muc do, nhu danh sach thuong
        if (row >= 0 && row < count() && e->modifiers() == Qt::NoModifier && selectedItems().size() > 1 && item(row)->isSelected()) {
            clearSelection();
            setCurrentRow(row);
        }
        QListWidget::mouseReleaseEvent(e);
        return;
    }
    const int row = pressRow_, before = gapAt(e->position().toPoint());
    std::vector<int> rows;
    if (row >= 0 && row < count()) {
        if (item(row)->isSelected())
            for (int r = 0; r < count(); ++r) { if (item(r)->isSelected()) rows.push_back(r); }
        else rows.push_back(row);
    }
    cancel();
    setState(QAbstractItemView::NoState);
    e->accept();
    if (before < 0 || rows.empty() || !onReorder) return;                  // tha ra ngoai = huy
    std::vector<int> order;
    order.resize(size_t(count()));
    for (int i = 0; i < count(); ++i) order[size_t(i)] = i;
    if (movedRows(order, rows, before) != order) onReorder(rows, before);
}

void DragList::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape && dragging_) {
        cancel();
        e->accept();
        return;
    }
    QListWidget::keyPressEvent(e);
}

void DragList::paintEvent(QPaintEvent* e) {
    QListWidget::paintEvent(e);
    if (!dragging_ || gap_ < 0 || count() == 0) return;
    const int y = gap_ < count() ? visualItemRect(item(gap_)).top() : visualItemRect(item(count() - 1)).bottom();
    QPainter p(viewport());
    p.fillRect(QRect(2, std::clamp(y - 2, 0, viewport()->height() - 4), viewport()->width() - 4, 4), QColor(colors::kAccent));
}

} // namespace lc
