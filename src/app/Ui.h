#pragma once
#include "Icons.h"
#include <QString>

class QPushButton;
class QToolButton;
class QWidget;

namespace lc::ui {

// Nut phu chi co bieu tuong: re chuot thay ten (va phim tat neu co). Khong nhan focus: Space/Enter luon la TAKE/CUT.
QPushButton* iconButton(Glyph g, const char* name, const QString& tip, QWidget* parent = nullptr);
QToolButton* iconToolButton(Glyph g, const char* name, const QString& tip, QWidget* parent = nullptr);

// Nut dang mo thi tooltip noi them ly do. setWhy: ly do rieng cua widget ("" = khong co);
// khoa van hanh (setLockedState) la ly do mac dinh va uu tien khi ca khung chua widget bi vo hieu.
void setWhy(QWidget* w, const QString& why);
void setLockedState(bool locked);
QString hintText(const QWidget* w);          // dung nhu tooltip se hien
void installHints();                         // goi mot lan (an toan khi goi lai): loc su kien ToolTip toan ung dung

} // namespace lc::ui
