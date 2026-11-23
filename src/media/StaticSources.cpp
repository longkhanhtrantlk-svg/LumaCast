#include "StaticSources.h"
#include <QFileInfo>
#include <QPainter>

namespace lc {

QImage makeTestPattern(int w, int h, const QString& label) {
    QImage img(w, h, QImage::Format_RGBA8888);
    img.fill(QColor(24, 24, 24));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    // luoi 10x10 o
    p.setPen(QPen(QColor(70, 70, 70), 1));
    for (int i = 1; i < 10; ++i) {
        p.drawLine(w * i / 10, 0, w * i / 10, h);
        p.drawLine(0, h * i / 10, w, h * i / 10);
    }
    // thanh mau o tren
    const QColor bars[] = {Qt::white, Qt::yellow, Qt::cyan, Qt::green, Qt::magenta, Qt::red, Qt::blue, Qt::black};
    const int bh = h / 6;
    for (int i = 0; i < 8; ++i)
        p.fillRect(w * i / 8, 0, w / 8 + 1, bh, bars[i]);
    // dai xam o duoi
    for (int x = 0; x < w; ++x) {
        const int v = 255 * x / qMax(1, w - 1);
        p.setPen(QColor(v, v, v));
        p.drawLine(x, h - bh, x, h);
    }
    // duong cheo + tron giua
    p.setPen(QPen(QColor(200, 200, 200), 2));
    p.drawLine(0, 0, w, h);
    p.drawLine(w, 0, 0, h);
    const int r = qMin(w, h) / 3;
    p.setPen(QPen(Qt::white, 3));
    p.drawEllipse(QPoint(w / 2, h / 2), r, r);
    // vien 4 canh de nhin thay mep man hinh LED
    p.setPen(QPen(Qt::red, 4));
    p.drawRect(2, 2, w - 4, h - 4);

    // chu
    QFont f = p.font();
    f.setPixelSize(qMax(12, h / 10));
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRect(0, 0, w, h), Qt::AlignCenter,
               label.isEmpty() ? QString("%1 x %2").arg(w).arg(h) : label + QString("\n%1 x %2").arg(w).arg(h));
    f.setPixelSize(qMax(10, h / 20));
    p.setFont(f);
    const int m = 12;
    p.drawText(m, bh + m, "TL");
    p.drawText(QRect(0, bh + m - 14, w - m, 40), Qt::AlignRight, "TR");
    p.drawText(m, h - bh - m, "BL");
    p.drawText(QRect(0, h - bh - m - 26, w - m, 40), Qt::AlignRight, "BR");
    p.end();
    return img;
}

ImageSource::ImageSource(QImage img, QString name)
    : img_(img.convertToFormat(QImage::Format_RGBA8888)), name_(std::move(name)) {}

std::shared_ptr<ImageSource> ImageSource::fromFile(const QString& path) {
    QImage img(path);
    if (img.isNull()) return nullptr;
    return std::make_shared<ImageSource>(img, QFileInfo(path).fileName());
}

TexRef ImageSource::acquire(GL& gl) {
    if (!uploaded_ && !img_.isNull()) {
        tex_.upload(gl, img_.constBits(), img_.width(), img_.height());
        uploaded_ = true;
    }
    return {tex_.id(), tex_.w(), tex_.h()};
}

TexRef ColorSource::acquire(GL& gl) {
    if (!uploaded_) {
        const uint8_t px[4] = {uint8_t(color_.red()), uint8_t(color_.green()), uint8_t(color_.blue()),
                               uint8_t(color_.alpha())};
        tex_.upload(gl, px, 1, 1);
        uploaded_ = true;
    }
    return {tex_.id(), tex_.w(), tex_.h()};
}

} // namespace lc
