#include "MonitorWidget.h"
#include <QPainter>

namespace lc {

MonitorWidget::MonitorWidget(const QString& title, const QColor& color, bool program, QWidget* parent)
    : QWidget(parent), title_(title), color_(color), program_(program) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void MonitorWidget::setImage(const QImage& img) { img_ = img; update(); }
void MonitorWidget::setSceneName(const QString& n) { if (n != scene_) { scene_ = n; update(); } }
void MonitorWidget::setOnAir(bool on) { if (on != onAir_) { onAir_ = on; update(); } }

void MonitorWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(rect(), QColor(0x0b, 0x0c, 0x10));
    p.setPen(QPen(color_, 3));
    p.drawRect(rect().adjusted(1, 1, -2, -2));

    const int head = 26;
    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    p.setPen(color_);
    p.drawText(QRect(10, 2, width() - 20, head), Qt::AlignVCenter | Qt::AlignLeft, title_);
    p.setPen(QColor(0xd0, 0xd4, 0xe0));
    p.drawText(QRect(10, 2, width() - 20, head), Qt::AlignVCenter | Qt::AlignRight, scene_);

    const QRect area(6, head + 2, width() - 12, height() - head - 8);
    if (img_.isNull()) {
        p.setPen(QColor(0x66, 0x6b, 0x7a));
        p.drawText(area, Qt::AlignCenter, "Chua co hinh");
    } else {
        QSize s = img_.size().scaled(area.size(), Qt::KeepAspectRatio);
        QRect r(QPoint(area.center().x() - s.width() / 2, area.center().y() - s.height() / 2), s);
        p.drawImage(r, img_);
    }
    if (program_ && onAir_) {
        const QRect badge(width() - 86, head + 8, 76, 20);
        p.setPen(Qt::NoPen);
        p.setBrush(color_);
        p.drawRoundedRect(badge, 4, 4);
        p.setPen(Qt::white);
        p.drawText(badge, Qt::AlignCenter, "ON AIR");
    }
}

} // namespace lc
