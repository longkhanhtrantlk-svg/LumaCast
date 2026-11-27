// Mo phong keo chuot cho test: gui su kien that vao widget (khong di chuyen con tro cua may).
#pragma once
#include <QApplication>
#include <QListWidget>
#include <QMouseEvent>
#include <QWidget>

namespace testdrag {

inline void send(QWidget* w, QEvent::Type type, const QPoint& pos, Qt::MouseButtons held, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QMouseEvent e(type, QPointF(pos), QPointF(w->mapToGlobal(pos)), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, held, mods);
    QApplication::sendEvent(w, &e);
}

inline void press(QWidget* w, const QPoint& at) { send(w, QEvent::MouseButtonPress, at, Qt::LeftButton); }
inline void move(QWidget* w, const QPoint& at) { send(w, QEvent::MouseMove, at, Qt::LeftButton); }
inline void release(QWidget* w, const QPoint& at) { send(w, QEvent::MouseButtonRelease, at, Qt::NoButton); }

// Keo tu `from` den `to` (qua vai diem giua), toa do cua w.
inline void drag(QWidget* w, const QPoint& from, const QPoint& to) {
    press(w, from);
    for (int i = 1; i <= 4; ++i) move(w, from + (to - from) * i / 4);
    release(w, to);
}

// Keo hang `row` cua danh sach toi truoc hang `before` (count() = cuoi danh sach).
inline void dragRow(QListWidget* list, int row, int before) {
    QWidget* v = list->viewport();
    const QPoint from = list->visualItemRect(list->item(row)).center();
    const QPoint to = before < list->count() ? QPoint(from.x(), list->visualItemRect(list->item(before)).top() + 2)
                                             : QPoint(from.x(), list->visualItemRect(list->item(list->count() - 1)).bottom() - 2);
    drag(v, from, to);
}

} // namespace testdrag
