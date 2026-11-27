#include "Icons.h"
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace lc {

namespace {
// Ve tren luoi 16x16.
void draw(QPainter& p, Glyph g, const QColor& c) {
    const QPen line(c, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(line);
    p.setBrush(Qt::NoBrush);
    auto fill = [&](const QPolygonF& poly) {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPolygon(poly);
        p.setPen(line);
        p.setBrush(Qt::NoBrush);
    };
    switch (g) {
    case Glyph::Import:                                   // thu muc + dau cong
        p.drawPolyline(QPolygonF{{8, 13}, {1.5, 13}, {1.5, 3}, {6, 3}, {7.5, 5}, {14, 5}, {14, 8}});
        p.drawLine(QPointF(12, 10), QPointF(12, 15));
        p.drawLine(QPointF(9.5, 12.5), QPointF(14.5, 12.5));
        break;
    case Glyph::Camera:
        p.drawRoundedRect(QRectF(1.5, 4.5, 9.5, 8), 1.5, 1.5);
        fill({{11, 8.5}, {15, 5.5}, {15, 11.5}});
        break;
    case Glyph::Ndi:                                      // nguon tu mang: cham + hai vong song
        p.setBrush(c);
        p.drawEllipse(QPointF(8, 8), 1.4, 1.4);
        p.setBrush(Qt::NoBrush);
        for (double r : {4.0, 6.8}) {
            p.drawArc(QRectF(8 - r, 8 - r, 2 * r, 2 * r), -45 * 16, 90 * 16);
            p.drawArc(QRectF(8 - r, 8 - r, 2 * r, 2 * r), 135 * 16, 90 * 16);
        }
        break;
    case Glyph::Play: fill({{4, 2.5}, {13.5, 8}, {4, 13.5}}); break;
    case Glyph::Pause:
        p.fillRect(QRectF(3.5, 3, 3.2, 10), c);
        p.fillRect(QRectF(9.3, 3, 3.2, 10), c);
        break;
    case Glyph::Rewind:
        p.fillRect(QRectF(3, 3, 2.2, 10), c);
        fill({{13, 3}, {6, 8}, {13, 13}});
        break;
    case Glyph::Loop:
        p.drawArc(QRectF(2.5, 3.5, 11, 9), 30 * 16, 150 * 16);
        p.drawArc(QRectF(2.5, 3.5, 11, 9), 210 * 16, 150 * 16);
        fill({{14.8, 3.6}, {13.6, 8}, {10.4, 5.4}});
        fill({{1.2, 12.4}, {2.4, 8}, {5.6, 10.6}});
        break;
    case Glyph::Speaker:
    case Glyph::Muted:
        fill({{1.5, 6}, {4.5, 6}, {8.5, 2.5}, {8.5, 13.5}, {4.5, 10}, {1.5, 10}});
        if (g == Glyph::Speaker) {
            p.drawArc(QRectF(6.5, 5, 5, 6), -60 * 16, 120 * 16);
            p.drawArc(QRectF(5.5, 2.5, 9, 11), -60 * 16, 120 * 16);
        } else {
            p.drawLine(QPointF(10.5, 5.5), QPointF(15, 10.5));
            p.drawLine(QPointF(15, 5.5), QPointF(10.5, 10.5));
        }
        break;
    case Glyph::Rename:                                   // but chi
        p.drawPolygon(QPolygonF{{2.5, 13.5}, {3.3, 10}, {10.8, 2.5}, {13.5, 5.2}, {6, 12.7}});
        p.drawLine(QPointF(9.2, 4.2), QPointF(11.8, 6.8));
        break;
    case Glyph::Add:
        p.drawLine(QPointF(8, 3), QPointF(8, 13));
        p.drawLine(QPointF(3, 8), QPointF(13, 8));
        break;
    case Glyph::Screen:                                   // man hinh co chan
        p.drawRoundedRect(QRectF(1.5, 2.5, 13, 9), 1, 1);
        p.drawLine(QPointF(8, 11.5), QPointF(8, 14));
        p.drawLine(QPointF(5, 14), QPointF(11, 14));
        break;
    case Glyph::Delete:                                   // thung rac
        p.drawLine(QPointF(2.5, 4.5), QPointF(13.5, 4.5));
        p.drawPolyline(QPolygonF{{6, 4.5}, {6, 2.5}, {10, 2.5}, {10, 4.5}});
        p.drawPolyline(QPolygonF{{4, 4.5}, {4.8, 13.8}, {11.2, 13.8}, {12, 4.5}});
        p.drawLine(QPointF(8, 7), QPointF(8, 11.5));
        break;
    }
}
} // namespace

QIcon glyphIcon(Glyph g) {
    QIcon icon;
    for (const auto& [mode, color] : {std::pair<QIcon::Mode, QColor>{QIcon::Normal, QColor(0xe6, 0xe8, 0xee)},
                                      std::pair<QIcon::Mode, QColor>{QIcon::Disabled, QColor(0x66, 0x6b, 0x7a)}}) {
        QPixmap pm(48, 48);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.scale(3, 3);
            draw(p, g, color);
        }
        icon.addPixmap(pm, mode);
    }
    return icon;
}

} // namespace lc
