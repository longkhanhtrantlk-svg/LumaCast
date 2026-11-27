#include "Theme.h"
#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>

namespace lc {

namespace {
// Vung tha khi keo khung: Fusion ve rat mo, o day to dac mau nhan voi vien day de thay ro khung se nam o dau.
class DockDropStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    void drawControl(ControlElement e, const QStyleOption* o, QPainter* p, const QWidget* w) const override {
        if (e != CE_RubberBand) { QProxyStyle::drawControl(e, o, p, w); return; }
        const QColor accent(colors::kAccent);
        p->save();
        p->fillRect(o->rect, QColor(accent.red(), accent.green(), accent.blue(), 110));
        p->setPen(QPen(accent.lighter(140), 4));
        p->drawRect(o->rect.adjusted(2, 2, -3, -3));
        p->restore();
    }
    int styleHint(StyleHint h, const QStyleOption* o, const QWidget* w, QStyleHintReturn* r) const override {
        return h == SH_RubberBand_Mask ? 0 : QProxyStyle::styleHint(h, o, w, r);
    }
};
} // namespace

void applyDarkTheme(QApplication& app) {
    app.setStyle(new DockDropStyle(QStyleFactory::create("Fusion")));
    QPalette p;
    const QColor bg(colors::kBg), panel(colors::kPanel), text(0xe6, 0xe8, 0xee), accent(colors::kAccent);
    p.setColor(QPalette::Window, panel);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, bg);
    p.setColor(QPalette::AlternateBase, panel);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0x27, 0x2b, 0x37));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::ToolTipBase, panel);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x90, 0xa2));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x66, 0x6b, 0x7a));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x66, 0x6b, 0x7a));
    app.setPalette(p);
    app.setStyleSheet(R"(
        QDockWidget { font-weight: 600; }
        QPushButton { padding: 6px 12px; background: #272b37; border: 1px solid #4d5570; border-radius: 4px; }
        QPushButton:hover { border-color: #2f7de1; }
        QPushButton:checked { background: #2f7de1; border-color: #2f7de1; }
        QPushButton#cut { font-weight: 700; }
        QPushButton#take { background: #2f7de1; border-color: #2f7de1; font-weight: 700; }
        QPushButton#blackAll:checked, QPushButton#ftb:checked { background: #e5322d; border-color: #e5322d; }
        QToolButton:checked { background: #2f7de1; }
        QPushButton[iconOnly="true"] { padding: 4px 6px; }
        QPushButton[compact="true"] { padding: 3px 8px; }
        QPushButton[swatch="true"] { padding: 3px 4px; }
        QToolButton#layoutButton { padding: 5px 20px 5px 8px; border: 1px solid #4d5570; border-radius: 4px; background: #272b37; }
        QToolButton#layoutButton::menu-indicator { subcontrol-position: right center; subcontrol-origin: padding; right: 4px; }
        QToolButton[iconOnly="true"] { padding: 3px; }
        QMainWindow::separator { background: #232734; width: 5px; height: 5px; }
        QMainWindow::separator:hover { background: #2f7de1; }
        QMainWindow > QTabBar, QDockWidgetGroupWindow > QTabBar { background: #08090c; qproperty-drawBase: 0; }
        QMainWindow > QTabBar::tab, QDockWidgetGroupWindow > QTabBar::tab {
            background: #08090c; color: #8a90a2; font-weight: 700; padding: 6px 12px; border: 0; margin: 3px 0 0 0; }
        QMainWindow > QTabBar::tab:selected, QDockWidgetGroupWindow > QTabBar::tab:selected { background: #1b1e27; color: #ffffff; }
        QMainWindow > QTabBar::tab:hover:!selected, QDockWidgetGroupWindow > QTabBar::tab:hover:!selected { color: #e6e8ee; }
        QListWidget { border: 1px solid #2a2f3d; }
        QGroupBox { border: 1px solid #2a2f3d; border-radius: 4px; margin-top: 10px; padding-top: 6px; }
        QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
    )");
}

} // namespace lc
