#include "Theme.h"
#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace lc {

void applyDarkTheme(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));
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
        QDockWidget::title { background: #232734; padding: 5px 8px; }
        QPushButton { padding: 6px 12px; border: 1px solid #353b4d; border-radius: 4px; }
        QPushButton:hover { border-color: #2f7de1; }
        QPushButton:checked { background: #2f7de1; border-color: #2f7de1; }
        QPushButton#cut { font-weight: 700; }
        QPushButton#take { background: #2f7de1; border-color: #2f7de1; font-weight: 700; }
        QPushButton#blackAll:checked, QPushButton#ftb:checked { background: #e5322d; border-color: #e5322d; }
        QToolButton:checked { background: #2f7de1; }
        QListWidget { border: 1px solid #2a2f3d; }
        QGroupBox { border: 1px solid #2a2f3d; border-radius: 4px; margin-top: 10px; padding-top: 6px; }
        QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
    )");
}

} // namespace lc
