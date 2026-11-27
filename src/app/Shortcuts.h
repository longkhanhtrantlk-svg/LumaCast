#pragma once
#include "control/LiveActions.h"
#include <QKeySequence>
#include <QMap>
#include <QObject>
#include <QSettings>
#include <functional>

class QShortcut;
class QWidget;

namespace lc {

// Phim tat tu dat (UI-07), luu theo may bang QSettings (khong vao project).
// Moi lenh co toi da MOT phim gan duoc. Space = TAKE va Enter = CUT la phim CO DINH (khong doi, khong gan cho lenh khac);
// TAKE/CUT co the them mot phim phu.
using KeyMap = QMap<QString, QKeySequence>;             // ma lenh -> phim (khong co muc = chua gan)

namespace shortcuts {

KeyMap defaults();                                      // y nhu truoc: Ctrl+L khoa, B = BLACK ALL, 1-9 = Preview
QString fixedKeysText(const LiveAction& a);             // "Space" / "Enter" cho TAKE/CUT, rong voi lenh khac
// Rong neu gan duoc; nguoc lai la ly do (phim co dinh, phim cua menu/timeline/he dieu hanh...).
QString reservedReason(const QKeySequence& k);
QStringList duplicates(const KeyMap& map);              // ma cac lenh dang dung chung phim
KeyMap load(QSettings& st);                             // mac dinh + phan da doi; muc sai/trung/bi giu cho bi bo qua
void save(QSettings& st, const KeyMap& map);            // chi ghi phan khac mac dinh

} // namespace shortcuts

// Tao QShortcut tren cua so theo KeyMap (kem phim co dinh); goi apply() lai de doi.
class ShortcutBinder : public QObject {
public:
    ShortcutBinder(QWidget* window, std::function<void(const LiveAction&)> run);
    void apply(const KeyMap& map);
    const KeyMap& map() const { return map_; }

private:
    void add(const QKeySequence& key, const LiveAction& a);
    QWidget* window_;
    std::function<void(const LiveAction&)> run_;
    KeyMap map_;
    QList<QShortcut*> shortcuts_;
};

} // namespace lc
