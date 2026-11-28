#include "ScreenSetup.h"
#include "core/I18n.h"
#include "media/StaticSources.h"
#include <QGuiApplication>
#include <QScreen>
#include <algorithm>
#include <cmath>

namespace lc {

QSize outputFramePixels(const OutputConfig& c) {
    if (c.kind == OutputKind::Ndi) return c.size;
    const QList<QScreen*> screens = QGuiApplication::screens();
    if (c.fullscreen) {
        QScreen* sc = nullptr;
        if (!c.screenName.isEmpty()) {
            for (QScreen* s : screens)
                if (s->name() == c.screenName) sc = s;
        } else {
            const int i = c.screenIndex < 0 ? 0 : c.screenIndex;
            if (i < screens.size()) sc = screens[i];
        }
        if (!sc) return c.size;                                    // man hinh khong con: chi biet kich thuoc cau hinh
        return sc->geometry().size() * sc->devicePixelRatio();
    }
    const QScreen* primary = QGuiApplication::primaryScreen();
    return c.size * (primary ? primary->devicePixelRatio() : 1.0);
}

QSize outputSignalSize(const OutputConfig& c) { return c.fullscreen && c.kind == OutputKind::Screen ? outputFramePixels(c) : c.size; }

QRect outputPictureRect(const OutputConfig& c, QSize frame) {
    if (c.area.isEmpty()) return QRect(QPoint(0, 0), frame);
    if (c.fullscreen || c.kind == OutputKind::Ndi || c.size.width() <= 0 || c.size.height() <= 0) return c.area;
    // cua so: co the bi thu nho cho vua man hinh / phong theo ti le man hinh -> doi vung LED theo cung ti le
    const double kx = double(frame.width()) / c.size.width(), ky = double(frame.height()) / c.size.height();
    return QRect(int(std::lround(c.area.x() * kx)), int(std::lround(c.area.y() * ky)),
                 int(std::lround(c.area.width() * kx)), int(std::lround(c.area.height() * ky)));
}

int ledPixels(double meters, double pitchMm) {
    if (!(meters > 0) || !(pitchMm > 0)) return 0;
    return int(std::lround(meters * 1000.0 / pitchMm));
}

static QString dims(QSize s) { return QString("%1×%2").arg(s.width()).arg(s.height()); }
static QString times(double k) {
    const QString s = QString::number(k, 'f', 2);
    return i18n::current() == Language::English ? s : QString(s).replace('.', ',');
}

QStringList outputWarnings(const std::vector<OutputConfig>& outputs, int index, QSize canvas, QSize frame) {
    QStringList w;
    if (index < 0 || index >= int(outputs.size()) || canvas.isEmpty() || frame.isEmpty()) return w;
    const OutputConfig& c = outputs[size_t(index)];
    const QRect picture = outputPictureRect(c, frame);
    if (!c.area.isEmpty() && !QRect(QPoint(0, 0), frame).contains(picture))
        w << QString(LC_TR("Vùng LED %1 tại (%2, %3) vượt ra ngoài khung tín hiệu %4: phần thừa bị cắt."))
                 .arg(dims(picture.size())).arg(picture.x()).arg(picture.y()).arg(dims(frame));

    const QSize part(int(std::lround(c.crop.width() * canvas.width())), int(std::lround(c.crop.height() * canvas.height())));
    if (part.isEmpty()) return w;
    if (c.crop != QRectF(0, 0, 1, 1)) {
        WallLayout wall;
        const bool inWall = detectWall(outputs, c.channel, canvas, wall)
                            && std::any_of(wall.cells.begin(), wall.cells.end(), [index](const WallLayout::Cell& x) { return x.output == index; });
        if (!inWall)
            w << QString(LC_TR("Chỉ hiện một phần khung hình (%1 trong %2): hình bị cắt.")).arg(dims(part), dims(canvas));
    }

    QSize shown = part;
    if ((c.rotation / 90) % 2) shown.transpose();
    const double fx = double(picture.width()) / shown.width(), fy = double(picture.height()) / shown.height();
    double sx = 1, sy = 1;
    switch (c.scale) {
    case ScaleMode::Fit: sx = sy = std::min(fx, fy); break;
    case ScaleMode::Fill: sx = sy = std::max(fx, fy); break;
    case ScaleMode::Stretch: sx = fx; sy = fy; break;
    case ScaleMode::Pixel: break;
    }
    const double dw = shown.width() * sx, dh = shown.height() * sy;
    if (std::abs(sx / sy - 1.0) > 0.005)
        w << QString(LC_TR("Hình bị co giãn méo: %1 ép vào %2 (ngang ×%3, dọc ×%4).")).arg(dims(shown), dims(picture.size()), times(sx), times(sy));
    if (dw > picture.width() + 1.5 || dh > picture.height() + 1.5)
        w << QString(LC_TR("Hình bị cắt mép: cần %1 nhưng chỗ hiện chỉ có %2.")).arg(dims(QSize(int(std::lround(dw)), int(std::lround(dh)))), dims(picture.size()));
    else if (dw < picture.width() - 1.5 || dh < picture.height() - 1.5)
        w << QString(LC_TR("Có viền đen: hình chỉ chiếm %1 trong %2.")).arg(dims(QSize(int(std::lround(dw)), int(std::lround(dh)))), dims(picture.size()));
    const double up = std::max(sx, sy), down = std::min(sx, sy);
    if (up > 1.005)
        w << QString(LC_TR("Hình bị phóng to ×%1 (nhoè trên LED): khung hình %2 nhỏ hơn chỗ hiện %3, ảnh/video lớn còn bị thu nhỏ rồi phóng to. "
                     "Đặt khung hình = %3.")).arg(times(up), dims(shown), dims(picture.size()));
    else if (down < 0.995)
        w << QString(LC_TR("Hình bị thu nhỏ ×%1 (mất chi tiết): khung hình %2 lớn hơn chỗ hiện %3.")).arg(times(down), dims(shown), dims(picture.size()));
    return w;
}

QString sceneSizeWarning(const std::vector<std::shared_ptr<Scene>>& scenes, QSize canvas) {
    int n = 0;
    const Scene* first = nullptr;
    for (const auto& s : scenes)
        if (s->size != canvas) { ++n; if (!first) first = s.get(); }
    if (!n) return {};
    return QString(LC_TR("%1 Scene dựng khác khung hình %2 (vd. “%3” %4): lên Program sẽ bị co giãn hoặc thu phóng."))
        .arg(n).arg(dims(canvas), first->name, dims(first->size));
}

Project makeNewProject(QSize canvas, std::vector<OutputConfig> outputs) {
    Project p;
    auto add = [&](const QString& name, SourceDef d) {
        auto s = std::make_shared<Scene>();
        s->name = name;
        s->size = canvas;
        s->layers.push_back({p.addSource(std::move(d)), {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        p.scenes.push_back(s);
    };
    SourceDef black;
    black.type = "color";
    black.color = Qt::black;
    black.label = LC_TR("Đen");
    add(LC_TR("Đen"), black);
    SourceDef test;
    test.type = "testpattern";
    test.size = canvas;
    test.label = LC_TR("Mẫu thử");
    add(LC_TR("Mẫu thử"), test);
    auto ch = std::make_shared<Channel>();
    ch->name = "Channel 1";
    ch->size = canvas;
    ch->scenes = p.scenes;
    ch->program = 0;
    ch->preview = 1;
    p.channels.push_back(ch);
    if (outputs.empty()) {
        OutputConfig o;
        o.name = "Output 1";
        outputs.push_back(o);
    }
    p.outputs = std::move(outputs);
    return p;
}

} // namespace lc
