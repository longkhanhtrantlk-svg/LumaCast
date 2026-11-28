#include "Shortcuts.h"
#include "core/I18n.h"
#include <QDebug>
#include <QShortcut>
#include <QWidget>

namespace lc {

namespace shortcuts {

KeyMap defaults() {
    KeyMap m;
    m.insert("lock", QKeySequence(Qt::CTRL | Qt::Key_L));
    m.insert("blackAll", QKeySequence(Qt::Key_B));
    m.insert("slide.next", QKeySequence(Qt::Key_PageDown));     // but trinh chieu (CTL-02)
    m.insert("slide.prev", QKeySequence(Qt::Key_PageUp));
    for (int i = 0; i < LiveAction::kPreviewCount; ++i)
        m.insert(QString("preview.%1").arg(i + 1), QKeySequence(Qt::Key_1 + i));
    return m;
}

QString fixedKeysText(const LiveAction& a) {
    if (a.kind == LiveAction::Take) return "Space";
    if (a.kind == LiveAction::Cut) return "Enter";
    return {};
}

QString reservedReason(const QKeySequence& k) {
    if (k.isEmpty()) return {};
    const QKeyCombination c = k[0];
    const int key = c.key();
    const Qt::KeyboardModifiers mods = c.keyboardModifiers() & ~Qt::KeypadModifier;
    if (key == Qt::Key_Space) return LC_TR("Space luôn là TAKE");
    if (key == Qt::Key_Return || key == Qt::Key_Enter) return LC_TR("Enter luôn là CUT");
    if (key == Qt::Key_Tab || key == Qt::Key_Backtab) return LC_TR("Tab dùng để chuyển ô trong cửa sổ");
    if (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_unknown)
        return LC_TR("Cần một phím thật, không chỉ phím bổ trợ");
    if (mods == Qt::NoModifier && (key == Qt::Key_Escape || key == Qt::Key_Delete || key == Qt::Key_Backspace))
        return LC_TR("Phím này dùng trong Timeline / hộp thoại");
    if (mods == Qt::ControlModifier && (key == Qt::Key_C || key == Qt::Key_V || key == Qt::Key_A))
        return LC_TR("Phím này dùng để sao chép / dán / chọn tất cả");
    if ((mods == Qt::ControlModifier && (key == Qt::Key_O || key == Qt::Key_S || key == Qt::Key_Z || key == Qt::Key_Y || key == Qt::Key_Q)) ||
        (mods == (Qt::ControlModifier | Qt::ShiftModifier) && (key == Qt::Key_S || key == Qt::Key_Z)))
        return LC_TR("Phím này thuộc menu (Mở / Lưu / Hoàn tác / Làm lại / Thoát)");
    for (auto standard : {QKeySequence::Open, QKeySequence::Save, QKeySequence::SaveAs, QKeySequence::Undo, QKeySequence::Redo, QKeySequence::Quit})
        for (const QKeySequence& s : QKeySequence::keyBindings(standard))
            if (s == k) return LC_TR("Phím này thuộc menu (Mở / Lưu / Hoàn tác / Làm lại / Thoát)");
    if (mods == Qt::ControlModifier && key == Qt::Key_M) return LC_TR("Ctrl+M mở cửa sổ Mapping trực quan");
    if (mods == Qt::ControlModifier && (key == Qt::Key_F1 || key == Qt::Key_F2 || key == Qt::Key_F3)) return LC_TR("Ctrl+F1 / F2 / F3 đổi bố cục khung");
    if (mods == Qt::NoModifier && key == Qt::Key_F9) return LC_TR("F9 ẩn / hiện cột phải");
    if (mods == Qt::NoModifier && key == Qt::Key_F11) return LC_TR("F11 phóng toàn màn hình Multiview");
    if (mods == Qt::AltModifier && (key == Qt::Key_F || key == Qt::Key_E || key == Qt::Key_V || key == Qt::Key_C || key == Qt::Key_H || key == Qt::Key_F4))
        return LC_TR("Phím này mở menu hoặc đóng cửa sổ");
    if (i18n::current() == Language::English && mods == Qt::AltModifier && key == Qt::Key_S)      // menu "&Settings"
        return LC_TR("Phím này mở menu hoặc đóng cửa sổ");
    return {};
}

QStringList duplicates(const KeyMap& map) {
    QStringList dup;
    for (auto a = map.begin(); a != map.end(); ++a)
        for (auto b = map.begin(); b != map.end(); ++b)
            if (a.key() != b.key() && !a.value().isEmpty() && a.value() == b.value()) { dup << a.key(); break; }
    return dup;
}

KeyMap load(QSettings& st) {
    const KeyMap def = defaults();
    KeyMap changed;
    QStringList cleared;
    st.beginGroup("shortcuts");
    for (const QString& id : st.childKeys()) {
        if (!LiveAction::fromId(id).valid()) continue;
        const QString text = st.value(id).toString();
        if (text.isEmpty()) { cleared << id; continue; }
        const QKeySequence k = QKeySequence::fromString(text, QKeySequence::PortableText);
        if (k.count() == 1 && k[0].key() != Qt::Key_unknown && reservedReason(k).isEmpty()) changed.insert(id, k);
        else qWarning().noquote() << QString("Phím tắt: bỏ qua giá trị không hợp lệ '%1' cho %2").arg(text.left(40), id);
    }
    st.endGroup();

    // Phan nguoi dung dat duoc uu tien; mac dinh nao trung voi no thi nhuong. Trung giua hai muc nguoi dung: giu lenh dung truoc.
    KeyMap out;
    auto used = [&](const QKeySequence& k) { for (const QKeySequence& have : std::as_const(out)) if (have == k) return true; return false; };
    QMap<int, QString> ordered;
    for (auto it = changed.constBegin(); it != changed.constEnd(); ++it) ordered.insert(LiveAction::fromId(it.key()).order(), it.key());
    for (const QString& id : std::as_const(ordered)) {
        if (!used(changed.value(id))) out.insert(id, changed.value(id));
        else qWarning().noquote() << "Phím tắt: bỏ phím trùng của" << id;
    }
    for (auto it = def.constBegin(); it != def.constEnd(); ++it)
        if (!changed.contains(it.key()) && !cleared.contains(it.key()) && !used(it.value())) out.insert(it.key(), it.value());
    return out;
}

void save(QSettings& st, const KeyMap& map) {
    const KeyMap def = defaults();
    st.remove("shortcuts");
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        if (!it.value().isEmpty() && def.value(it.key()) != it.value()) st.setValue("shortcuts/" + it.key(), it.value().toString(QKeySequence::PortableText));
    for (auto it = def.constBegin(); it != def.constEnd(); ++it)
        if (map.value(it.key()).isEmpty()) st.setValue("shortcuts/" + it.key(), QString());      // da go phim mac dinh
}

} // namespace shortcuts

ShortcutBinder::ShortcutBinder(QWidget* window, std::function<void(const LiveAction&)> run)
    : QObject(window), window_(window), run_(std::move(run)) {}

void ShortcutBinder::add(const QKeySequence& key, const LiveAction& a) {
    auto* s = new QShortcut(key, window_);
    // Giu phim: lenh bat/tat (FTB, Den/Dung/Mau, khoa) khong lap de khong nhap nhay; cac phim co tu truoc giu hanh vi cu.
    s->setAutoRepeat(a.kind != LiveAction::SlideNext && a.kind != LiveAction::SlidePrev && a.kind != LiveAction::Ftb && a.kind != LiveAction::Lock && a.kind != LiveAction::OutputBlack &&
                     a.kind != LiveAction::OutputFreeze && a.kind != LiveAction::OutputPattern);
    QObject::connect(s, &QShortcut::activated, this, [this, a] { run_(a); });
    shortcuts_ << s;
}

void ShortcutBinder::apply(const KeyMap& map) {
    qDeleteAll(shortcuts_);                                // xoa ngay: khong duoc ton tai song song (Qt se bao phim mo ho)
    shortcuts_.clear();
    map_ = map;
    const LiveAction take{LiveAction::Take, 0}, cut{LiveAction::Cut, 0};
    add(QKeySequence(Qt::Key_Space), take);
    add(QKeySequence(Qt::Key_Return), cut);
    add(QKeySequence(Qt::Key_Enter), cut);
    for (auto it = map_.constBegin(); it != map_.constEnd(); ++it) {
        const LiveAction a = LiveAction::fromId(it.key());
        if (a.valid() && !it.value().isEmpty() && shortcuts::reservedReason(it.value()).isEmpty()) add(it.value(), a);
    }
}

} // namespace lc
