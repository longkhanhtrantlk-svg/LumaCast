#include "OverlayRaster.h"
#include <QFont>
#include <QFontMetricsF>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <QTextLayout>
#include <QTextOption>
#include <algorithm>
#include <cmath>

namespace lc {

namespace {
constexpr int kMaxBox = 8192;          // canh hop toi da
constexpr int kMaxStrip = 32768;       // do dai toi da cua dai chu chay (px)
} // namespace

OverlayRasterKey overlayRasterKey(const OverlayDef& d, int secondsLeft, QSize canvas, QSize target) {
    const double sx = canvas.height() > 0 ? double(target.height()) / canvas.height() : 1.0;
    OverlayRasterKey k;
    k.type = d.type;
    k.align = d.align;
    const int bw = std::clamp(int(std::lround(d.rect.width() * target.width())), 1, kMaxBox);
    const int bh = std::clamp(int(std::lround(d.rect.height() * target.height())), 1, kMaxBox);
    k.box = QSize(d.type == OverlayType::Ticker ? 0 : bw, bh);
    if (d.type == OverlayType::Logo) {
        k.image = d.image;
        return k;
    }
    k.text = overlayDisplayText(d, secondsLeft);
    k.font = d.font;
    k.fontPx = std::clamp(int(std::lround(d.fontSize * sx)), 1, 4000);
    k.bold = d.bold;
    k.italic = d.italic;
    k.color = d.color.rgba();
    k.outlineColor = d.outlineColor.rgba();
    k.outlinePx = d.outlineWidth * sx;
    return k;
}

static QFont fontOf(const OverlayRasterKey& k) {
    QFont f;
    if (!k.font.isEmpty()) f.setFamily(k.font);
    f.setPixelSize(k.fontPx);
    f.setBold(k.bold);
    f.setItalic(k.italic);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

static void paintPath(QPainter& p, const QPainterPath& path, const OverlayRasterKey& k) {
    if (k.outlinePx > 0 && qAlpha(k.outlineColor) > 0)
        p.strokePath(path, QPen(QColor::fromRgba(k.outlineColor), k.outlinePx * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.fillPath(path, QColor::fromRgba(k.color));
}

static QImage blank(int w, int h) {
    QImage img(w, h, QImage::Format_RGBA8888_Premultiplied);
    img.fill(Qt::transparent);
    return img;
}

static OverlayBitmap single(QImage img) {
    OverlayBitmap b;
    b.size = img.size();
    b.tiles.push_back(std::move(img));
    return b;
}

static OverlayBitmap rasterLogo(const OverlayRasterKey& k) {
    if (k.image.isEmpty()) return {};
    QImageReader reader(k.image);
    reader.setAutoTransform(true);
    QImage src = reader.read();
    if (src.isNull()) return {};
    QImage img = src.scaled(k.box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (img.isNull()) return {};
    return single(img.convertToFormat(QImage::Format_RGBA8888_Premultiplied));
}

static OverlayBitmap rasterTicker(const OverlayRasterKey& k) {
    QString text = k.text.normalized(QString::NormalizationForm_C);
    text.replace("\r\n", "\n");
    text.replace('\n', QStringLiteral("   "));
    if (text.trimmed().isEmpty()) return {};
    const QFont f = fontOf(k);
    const QFontMetricsF fm(f);
    const double pad = std::ceil(k.outlinePx) + 2;
    const int w = std::clamp(int(std::ceil(fm.horizontalAdvance(text) + pad * 2)), 1, kMaxStrip);
    const int h = k.box.height();
    QPainterPath path;
    path.addText(QPointF(pad, (h - (fm.ascent() + fm.descent())) / 2 + fm.ascent()), f, text);
    OverlayBitmap b;
    b.size = QSize(w, h);
    for (int x = 0; x < w; x += OverlayBitmap::kTileWidth) {
        QImage img = blank(std::min(OverlayBitmap::kTileWidth, w - x), h);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.translate(-x, 0);
        paintPath(p, path, k);
        p.end();
        b.tiles.push_back(std::move(img));
    }
    return b;
}

// Tieu de / dem nguoc: tu xuong dong theo be rong hop, can giua theo chieu doc.
static OverlayBitmap rasterBlock(const OverlayRasterKey& k) {
    QString text = k.text.normalized(QString::NormalizationForm_C);
    if (text.trimmed().isEmpty()) return {};
    const QFont f = fontOf(k);
    const QFontMetricsF fm(f);
    const double pad = std::ceil(k.outlinePx) + std::max(2.0, k.fontPx * 0.15);
    const double avail = std::max(1.0, k.box.width() - pad * 2);

    QStringList lines;
    text.replace("\r\n", "\n");
    for (const QString& para : text.split('\n')) {
        if (para.isEmpty()) { lines << QString(); continue; }
        QTextLayout layout(para, f);
        QTextOption opt;
        // dong ho khong duoc xuong dong
        opt.setWrapMode(k.type == OverlayType::Countdown ? QTextOption::NoWrap : QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(opt);
        layout.beginLayout();
        for (;;) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(avail);
            lines << para.mid(line.textStart(), line.textLength()).trimmed();
        }
        layout.endLayout();
    }

    const double lineH = std::max(1.0, fm.lineSpacing());
    const double blockH = fm.ascent() + fm.descent() + lineH * (lines.size() - 1);
    double baseline = (k.box.height() - blockH) / 2 + fm.ascent();
    QPainterPath path;
    for (const QString& line : lines) {
        const double w = fm.horizontalAdvance(line);
        const double x = k.align == OverlayAlign::Left ? pad : k.align == OverlayAlign::Right ? k.box.width() - pad - w
                                                                                             : (k.box.width() - w) / 2;
        if (!line.isEmpty()) path.addText(QPointF(x, baseline), f, line);
        baseline += lineH;
    }
    path.setFillRule(Qt::WindingFill);
    // Dau tieng Viet chong cao (Ễ, Ậ) co the vuot dong chuan: day khoi chu theo MUC that de khong bi cat khi con vua hop;
    // neu cao hon hop thi giu tron dong dau.
    const QRectF ink = path.boundingRect().adjusted(-k.outlinePx, -k.outlinePx, k.outlinePx, k.outlinePx);
    bool clipped = false;
    if (!path.isEmpty()) {
        const double h = k.box.height();
        if (ink.height() > h) { path.translate(0, -ink.top()); clipped = true; }
        else if (ink.top() < 0) path.translate(0, -ink.top());
        else if (ink.bottom() > h) path.translate(0, h - ink.bottom());
        clipped = clipped || ink.left() < -0.5 || ink.right() > k.box.width() + 0.5;
    }
    QImage img = blank(k.box.width(), k.box.height());
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    paintPath(p, path, k);
    p.end();
    OverlayBitmap b = single(std::move(img));
    b.clipped = clipped;
    return b;
}

OverlayBitmap rasterizeOverlay(const OverlayRasterKey& k) {
    switch (k.type) {
    case OverlayType::Logo: return rasterLogo(k);
    case OverlayType::Ticker: return rasterTicker(k);
    case OverlayType::Title:
    case OverlayType::Countdown: return rasterBlock(k);
    }
    return {};
}

double overlayContentX(OverlayType type, OverlayAlign align, double boxW, double contentW, double onSeconds, double speedPx) {
    if (type == OverlayType::Ticker) {
        const double period = boxW + contentW;
        return period > 0 ? boxW - std::fmod(std::max(0.0, onSeconds) * speedPx, period) : 0.0;
    }
    if (type == OverlayType::Logo)
        return align == OverlayAlign::Left ? 0.0 : align == OverlayAlign::Right ? boxW - contentW : (boxW - contentW) / 2;
    return 0.0;                                  // tieu de / dem nguoc: anh rong bang hop, can le nam san trong anh
}

} // namespace lc
