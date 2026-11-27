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
    case Glyph::Params:                                   // ba thanh truot
        for (double y : {4.0, 8.0, 12.0}) p.drawLine(QPointF(2, y), QPointF(14, y));
        p.setBrush(c);
        for (const QPointF& k : {QPointF(10.5, 4), QPointF(5, 8), QPointF(9, 12)}) p.drawEllipse(k, 1.7, 1.7);
        break;
    case Glyph::Black:                                    // man hinh to den (khung + ruot dac)
        p.drawRoundedRect(QRectF(1.5, 2.5, 13, 11), 1, 1);
        p.fillRect(QRectF(4, 5, 8, 6), c);
        break;
    case Glyph::Freeze:                                   // bong tuyet
        for (int a = 0; a < 3; ++a) {
            p.save();
            p.translate(8, 8);
            p.rotate(60 * a);
            p.drawLine(QPointF(0, -6.5), QPointF(0, 6.5));
            p.drawPolyline(QPolygonF{{-2, -6}, {0, -4}, {2, -6}});
            p.drawPolyline(QPolygonF{{-2, 6}, {0, 4}, {2, 6}});
            p.restore();
        }
        break;
    case Glyph::Pattern:                                  // luoi mau thu
        p.drawRect(QRectF(2, 3, 12, 10));
        p.drawLine(QPointF(6, 3), QPointF(6, 13));
        p.drawLine(QPointF(10, 3), QPointF(10, 13));
        p.drawLine(QPointF(2, 8), QPointF(14, 8));
        break;
    case Glyph::PatternAll:
        p.drawRect(QRectF(1.5, 4.5, 10, 9));
        p.drawLine(QPointF(6.5, 4.5), QPointF(6.5, 13.5));
        p.drawLine(QPointF(1.5, 9), QPointF(11.5, 9));
        p.drawPolyline(QPolygonF{{4.5, 2}, {14.5, 2}, {14.5, 11}});
        break;
    case Glyph::Refresh:
        p.drawArc(QRectF(3, 3, 10, 10), 40 * 16, 290 * 16);
        fill({{13.8, 2.2}, {13.8, 7}, {9.4, 6}});
        break;
    case Glyph::Folder:
        p.drawPolygon(QPolygonF{{1.5, 13}, {1.5, 3}, {6, 3}, {7.5, 5}, {14.5, 5}, {14.5, 13}});
        break;
    case Glyph::Prev:
        p.fillRect(QRectF(3, 3, 2, 10), c);
        fill({{13.5, 3}, {6.5, 8}, {13.5, 13}});
        break;
    case Glyph::Next:
        p.fillRect(QRectF(11, 3, 2, 10), c);
        fill({{2.5, 3}, {9.5, 8}, {2.5, 13}});
        break;
    case Glyph::Stop: p.fillRect(QRectF(3.5, 3.5, 9, 9), c); break;
    case Glyph::AddScene:                                 // o hinh + dau cong
        p.drawRect(QRectF(1.5, 2.5, 9.5, 8));
        p.drawLine(QPointF(12, 9), QPointF(12, 15));
        p.drawLine(QPointF(9, 12), QPointF(15, 12));
        break;
    case Glyph::AddFile:                                  // to giay + dau cong
        p.drawPolyline(QPolygonF{{7.5, 14}, {2.5, 14}, {2.5, 2}, {8, 2}, {11, 5}, {11, 7.5}});
        p.drawLine(QPointF(12, 9), QPointF(12, 15));
        p.drawLine(QPointF(9, 12), QPointF(15, 12));
        break;
    case Glyph::Remove: p.drawLine(QPointF(3, 8), QPointF(13, 8)); break;
    case Glyph::Up:
        p.drawLine(QPointF(8, 13), QPointF(8, 3));
        p.drawPolyline(QPolygonF{{4, 7}, {8, 3}, {12, 7}});
        break;
    case Glyph::Down:
        p.drawLine(QPointF(8, 3), QPointF(8, 13));
        p.drawPolyline(QPolygonF{{4, 9}, {8, 13}, {12, 9}});
        break;
    case Glyph::Eye:
    case Glyph::EyeOff: {
        QPainterPath eye(QPointF(1.5, 8));
        eye.quadTo(8, 0.8, 14.5, 8);
        eye.quadTo(8, 15.2, 1.5, 8);
        p.drawPath(eye);
        p.setBrush(c);
        p.drawEllipse(QPointF(8, 8), 2.1, 2.1);
        if (g == Glyph::EyeOff) p.drawLine(QPointF(2.5, 13.5), QPointF(13.5, 2.5));
        break;
    }
    case Glyph::Reset:                                    // mui ten quay nguoc
        p.drawArc(QRectF(3, 3, 10, 10), 210 * 16, -300 * 16);
        fill({{2.2, 2.4}, {2.2, 7.4}, {6.8, 6.2}});
        break;
    case Glyph::Close:
        p.drawLine(QPointF(4, 4), QPointF(12, 12));
        p.drawLine(QPointF(12, 4), QPointF(4, 12));
        break;
    case Glyph::Grip:                                     // tay keo
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        for (double x : {6.0, 10.0})
            for (double y : {4.0, 8.0, 12.0}) p.drawEllipse(QPointF(x, y), 1.1, 1.1);
        break;
    case Glyph::TitleText:                                // chu T
        p.drawPolyline(QPolygonF{{3, 5}, {3, 3}, {13, 3}, {13, 5}});
        p.drawLine(QPointF(8, 3), QPointF(8, 13));
        p.drawLine(QPointF(6, 13), QPointF(10, 13));
        break;
    case Glyph::Ticker:                                   // dong chu chay: khung + mui ten sang trai
        p.drawRect(QRectF(1.5, 3.5, 13, 9));
        p.drawLine(QPointF(4, 8), QPointF(12, 8));
        p.drawPolyline(QPolygonF{{6.5, 5.8}, {4, 8}, {6.5, 10.2}});
        break;
    case Glyph::Countdown:                                // dong ho
        p.drawEllipse(QPointF(8, 9), 5.5, 5.5);
        p.drawLine(QPointF(8, 9), QPointF(8, 5.5));
        p.drawLine(QPointF(8, 9), QPointF(10.5, 10));
        p.drawLine(QPointF(6.5, 1.5), QPointF(9.5, 1.5));
        break;
    case Glyph::Logo:                                     // anh: khung + nui + mat troi
        p.drawRect(QRectF(1.5, 2.5, 13, 11));
        p.drawPolyline(QPolygonF{{2, 12}, {6, 7}, {9, 10.5}, {11, 8.5}, {14, 12}});
        p.setBrush(c);
        p.drawEllipse(QPointF(11, 5.5), 1.3, 1.3);
        break;
    case Glyph::PosCenter:
    case Glyph::PosTopLeft:
    case Glyph::PosTopRight:
    case Glyph::PosBottomLeft:
    case Glyph::PosBottomRight: {                         // man hinh voi mot o dac o vi tri tuong ung
        p.drawRect(QRectF(1.5, 2.5, 13, 11));
        const QPointF at = g == Glyph::PosCenter ? QPointF(5.5, 6) : g == Glyph::PosTopLeft ? QPointF(3, 4)
                           : g == Glyph::PosTopRight ? QPointF(8, 4) : g == Glyph::PosBottomLeft ? QPointF(3, 8) : QPointF(8, 8);
        p.fillRect(QRectF(at.x(), at.y(), 5, 4), c);
        break;
    }
    case Glyph::Fit:                                      // hinh nam TRON trong khung, con vien
        p.drawRect(QRectF(1.5, 2.5, 13, 11));
        p.fillRect(QRectF(2.5, 6, 11, 4), c);
        break;
    case Glyph::Fill:                                     // hinh phu kin khung va thua ra hai ben
        p.fillRect(QRectF(3.5, 4, 9, 8), c);
        p.drawLine(QPointF(2.5, 2.5), QPointF(2.5, 13.5));
        p.drawLine(QPointF(13.5, 2.5), QPointF(13.5, 13.5));
        p.drawLine(QPointF(0.5, 6), QPointF(0.5, 10));
        p.drawLine(QPointF(15.5, 6), QPointF(15.5, 10));
        break;
    case Glyph::Stretch:                                  // keo gian bon huong
        p.drawLine(QPointF(3, 8), QPointF(13, 8));
        p.drawLine(QPointF(8, 3), QPointF(8, 13));
        p.drawPolyline(QPolygonF{{5, 6}, {3, 8}, {5, 10}});
        p.drawPolyline(QPolygonF{{11, 6}, {13, 8}, {11, 10}});
        p.drawPolyline(QPolygonF{{6, 5}, {8, 3}, {10, 5}});
        p.drawPolyline(QPolygonF{{6, 11}, {8, 13}, {10, 11}});
        break;
    case Glyph::ZoomIn:
    case Glyph::ZoomOut:
    case Glyph::ZoomFit:                                  // kinh lup
        p.drawEllipse(QPointF(6.8, 6.8), 4.8, 4.8);
        p.drawLine(QPointF(10.5, 10.5), QPointF(14.2, 14.2));
        if (g == Glyph::ZoomFit) {
            p.drawLine(QPointF(4.5, 6.8), QPointF(9.1, 6.8));
            p.drawPolyline(QPolygonF{{5.8, 5.3}, {4.3, 6.8}, {5.8, 8.3}});
            p.drawPolyline(QPolygonF{{7.8, 5.3}, {9.3, 6.8}, {7.8, 8.3}});
        } else {
            p.drawLine(QPointF(4.5, 6.8), QPointF(9.1, 6.8));
            if (g == Glyph::ZoomIn) p.drawLine(QPointF(6.8, 4.5), QPointF(6.8, 9.1));
        }
        break;
    case Glyph::FullScreen:
        p.drawPolyline(QPolygonF{{2, 6}, {2, 2}, {6, 2}});
        p.drawPolyline(QPolygonF{{10, 2}, {14, 2}, {14, 6}});
        p.drawPolyline(QPolygonF{{14, 10}, {14, 14}, {10, 14}});
        p.drawPolyline(QPolygonF{{6, 14}, {2, 14}, {2, 10}});
        break;
    case Glyph::Layout:                                   // cua so chia khung
        p.drawRect(QRectF(1.5, 2.5, 13, 11));
        p.drawLine(QPointF(10, 2.5), QPointF(10, 13.5));
        p.drawLine(QPointF(1.5, 9.5), QPointF(10, 9.5));
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
