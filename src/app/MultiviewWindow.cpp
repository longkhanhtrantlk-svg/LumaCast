#include "MultiviewWindow.h"
#include "Theme.h"
#include "core/AppSettings.h"
#include "render/OutputWindow.h"
#include <QDateTime>
#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <algorithm>
#include <cmath>

namespace lc {

namespace {
constexpr int kHeader = 34;
constexpr int kLabel = 22;
constexpr int kBorder = 3;

QString nameAt(const std::vector<std::vector<QString>>& names, int ch, int i) {
    if (ch < 0 || ch >= int(names.size()) || i < 0 || i >= int(names[size_t(ch)].size())) return "-";
    return names[size_t(ch)][size_t(i)];
}
} // namespace

MultiviewWindow::MultiviewWindow(Project& project, RenderEngine& engine, QWidget* parent)
    : QWidget(parent, Qt::Tool), project_(project), engine_(engine) {
    setObjectName("multiviewWindow");
    setWindowTitle("LumaCast — Multiview");
    setMinimumSize(480, 300);
    fullBtn_ = new QPushButton("Toàn màn hình (F11)", this);
    fullBtn_->setObjectName("multiviewFullScreen");
    fullBtn_->setFocusPolicy(Qt::NoFocus);               // Space/Enter la TAKE/CUT, khong bam nut nay
    fullBtn_->setAutoDefault(false);
    connect(fullBtn_, &QPushButton::clicked, this, [this] { toggleFullScreen(); });
    new QShortcut(Qt::Key_F11, this, [this] { toggleFullScreen(); });
    new QShortcut(Qt::Key_Escape, this, [this] { if (isFullScreen()) toggleFullScreen(); });
    setFocusPolicy(Qt::StrongFocus);

    const QByteArray geo = appSettings().value("multiview/geometry").toByteArray();
    if (geo.isEmpty() || !restoreGeometry(geo)) {
        QSize sz(1280, 800);
        if (const QScreen* s = QGuiApplication::primaryScreen()) sz = sz.boundedTo(s->availableGeometry().size() * 0.85);
        resize(sz);
    }
    placeButton();
}

MultiviewWindow::~MultiviewWindow() {
    engine_.setSceneThumbChannel(-1);
    storeGeometry();
}

void MultiviewWindow::storeGeometry() const {
    if (isFullScreen()) return;
    QSettings st = appSettings();
    st.setValue("multiview/geometry", saveGeometry());
}

bool MultiviewWindow::screenHasFullscreenOutput(const QScreen* screen) const {
    for (int i = 0; i < engine_.outputCount(); ++i) {
        const auto* w = dynamic_cast<const OutputWindow*>(engine_.output(i));
        if (w && w->isFullscreenOutput() && w->isVisible() && w->screen() == screen) return true;
    }
    return false;
}

bool MultiviewWindow::toggleFullScreen() {
    if (isFullScreen()) {
        showNormal();
        return true;
    }
    if (screenHasFullscreenOutput(screen())) {
        showNotice("Màn hình này đang có Output toàn màn hình — hãy kéo cửa sổ Multiview sang màn hình khác");
        return false;
    }
    showFullScreen();
    return true;
}

void MultiviewWindow::showNotice(const QString& text) {
    notice_ = text;
    noticeUntilMs_ = QDateTime::currentMSecsSinceEpoch() + 6000;
    qWarning().noquote() << "Multiview:" << text;
    update();
}

QString MultiviewWindow::notice() const {
    return QDateTime::currentMSecsSinceEpoch() < noticeUntilMs_ ? notice_ : QString();
}

void MultiviewWindow::refresh(int selectedChannel, const std::vector<std::vector<QString>>& sceneNames) {
    const int channels = int(project_.channels.size());
    if (channels == 0) return;
    channel_ = std::clamp(selectedChannel, 0, channels - 1);
    // Man hinh duoc cam lai / Output chuyen sang man dang xem: roi toan man hinh, khong che man LED.
    if (isFullScreen() && screenHasFullscreenOutput(screen())) {
        toggleFullScreen();
        showNotice("Đã thoát toàn màn hình: màn hình này vừa có Output toàn màn hình");
    }
    const bool minimized = parentWidget() && parentWidget()->window()->isMinimized();
    engine_.setSceneThumbChannel(minimized ? -1 : channel_);
    const RenderEngine::SceneThumbFrame thumbs = engine_.sceneThumbs();
    sceneCount_ = channel_ < int(sceneNames.size()) ? int(sceneNames[size_t(channel_)].size()) : 0;

    std::vector<Tile> tiles;
    tiles.reserve(size_t(channels * 2 + sceneCount_));
    for (int c = 0; c < channels; ++c) {
        const Channel& ch = *project_.channels[size_t(c)];
        const auto mf = engine_.monitor(c);
        const int pg = ch.program, pv = ch.preview;
        const QString prefix = channels > 1 ? ch.name + " · " : QString();
        Tile v;
        v.kind = Kind::Preview;
        v.channel = c;
        v.scene = pv;
        v.mark = Mark::Preview;
        v.title = "PVW · " + prefix + nameAt(sceneNames, c, pv);
        v.image = mf.preview;
        Tile g;
        g.kind = Kind::Program;
        g.channel = c;
        g.scene = pg;
        g.mark = Mark::Program;
        g.onAir = ch.ftbAmount < 1.0f;
        g.title = "PGM · " + prefix + nameAt(sceneNames, c, pg) + (ch.inTransition() ? "  (đang chuyển)" : "");
        g.image = mf.program;
        tiles.push_back(std::move(v));                    // thu tu giong cua so chinh: Preview trai, Program phai
        tiles.push_back(std::move(g));
    }
    const Channel& sel = *project_.channels[size_t(channel_)];
    const int pg = sel.program, pv = sel.preview;
    const bool moving = sel.inTransition();
    for (int i = 0; i < sceneCount_; ++i) {
        Tile t;
        t.kind = Kind::Scene;
        t.channel = channel_;
        t.scene = i;
        // Dang chuyen: ca Scene den (Preview) cung da hien tren Output -> do.
        t.mark = (i == pg || (moving && i == pv)) ? Mark::Program : i == pv ? Mark::Preview : Mark::None;
        t.onAir = t.mark == Mark::Program && sel.ftbAmount < 1.0f;
        t.title = QString("%1  %2").arg(i + 1).arg(nameAt(sceneNames, channel_, i));
        if (thumbs.channel == channel_ && i < int(thumbs.images.size())) t.image = thumbs.images[size_t(i)];
        tiles.push_back(std::move(t));
    }
    tiles_ = std::move(tiles);
    relayout();
    update();
}

// Hang tren: tung cap PVW|PGM cua moi Channel (toi da 60% chieu cao neu co Scene). Phan con lai: luoi Scene sao cho o to nhat.
void MultiviewWindow::relayout() {
    const int channels = int(project_.channels.size());
    if (channels == 0 || int(tiles_.size()) < channels * 2) return;
    const QSize cs = project_.channels[size_t(std::clamp(channel_, 0, channels - 1))]->size;
    const double aspect = std::clamp(cs.height() > 0 ? double(cs.width()) / cs.height() : 16.0 / 9.0, 0.4, 5.0);
    const QRect area = rect().adjusted(6, kHeader, -6, -6);
    const int scenes = int(tiles_.size()) - channels * 2;

    const int pairCols = channels == 1 ? 1 : channels <= 4 ? 2 : 3;
    const int pairRows = (channels + pairCols - 1) / pairCols;
    double w = double(area.width()) / (2 * pairCols);
    double h = w / aspect + kLabel;
    const double maxTop = scenes > 0 ? area.height() * 0.6 : area.height();
    if (h * pairRows > maxTop) {
        h = maxTop / pairRows;
        w = std::max(1.0, (h - kLabel) * aspect);
    }
    const double left = area.left() + (area.width() - w * 2 * pairCols) / 2;
    for (int c = 0; c < channels; ++c) {
        const double x = left + (c % pairCols) * w * 2, y = area.top() + (c / pairCols) * h;
        for (int k = 0; k < 2; ++k)
            tiles_[size_t(c * 2 + k)].rect = QRect(int(x + k * w), int(y), int(w), int(h)).adjusted(3, 3, -3, -3);
    }
    if (scenes <= 0) return;

    const int top = area.top() + int(h * pairRows) + 8;
    const QRect grid(area.left(), top, area.width(), std::max(1, area.bottom() - top));
    int cols = 1;
    double best = 0;
    for (int c = 1; c <= scenes; ++c) {
        const int rows = (scenes + c - 1) / c;
        const double fit = std::min(double(grid.width()) / c, (double(grid.height()) / rows - kLabel) * aspect);
        if (fit > best) { best = fit; cols = c; }
    }
    const double tw = std::max(24.0, best), th = tw / aspect + kLabel;
    const double gx = grid.left() + (grid.width() - tw * cols) / 2;
    for (int i = 0; i < scenes; ++i)
        tiles_[size_t(channels * 2 + i)].rect =
            QRect(int(gx + (i % cols) * tw), int(grid.top() + (i / cols) * th), int(tw), int(th)).adjusted(3, 3, -3, -3);
}

void MultiviewWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(rect(), QColor(0x08, 0x09, 0x0c));
    const int channels = int(project_.channels.size());
    QFont f = font();
    f.setBold(true);
    p.setFont(f);

    const QString note = notice();
    p.setPen(note.isEmpty() ? QColor(0xd0, 0xd4, 0xe0) : QColor(colors::kWarn));
    QString head = note;
    if (head.isEmpty() && channel_ < channels)
        head = QString("Scene của kênh %1: %2  —  bấm = đưa vào Preview; Space = TAKE, Enter = CUT")
                   .arg(channel_ + 1).arg(project_.channels[size_t(channel_)]->name);
    const QRect headRect(10, 0, std::max(0, fullBtn_->x() - 20), kHeader);
    p.drawText(headRect, Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(head, Qt::ElideRight, headRect.width()));

    // Danh dau Channel dang chon (Space/Enter va luoi Scene thuoc ve no)
    if (channels > 1 && int(tiles_.size()) >= channels * 2) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x2c, 0x31, 0x3f));
        p.drawRoundedRect(tiles_[size_t(channel_ * 2)].rect.united(tiles_[size_t(channel_ * 2 + 1)].rect).adjusted(-3, -3, 3, 3), 4, 4);
    }

    for (const Tile& t : tiles_) {
        const QColor mark = t.mark == Mark::Program ? QColor(colors::kProgram)
                            : t.mark == Mark::Preview ? QColor(colors::kPreview) : QColor(0x3a, 0x3f, 0x4d);
        p.fillRect(t.rect, QColor(0x0b, 0x0c, 0x10));
        const QRect picture = t.rect.adjusted(kBorder, kBorder, -kBorder, -kLabel);
        if (t.image.isNull()) {
            p.setPen(QColor(0x66, 0x6b, 0x7a));
            p.drawText(picture, Qt::AlignCenter, t.scene < 0 ? "Chưa có hình" : "…");
        } else {
            const QSize s = t.image.size().scaled(picture.size(), Qt::KeepAspectRatio);
            p.drawImage(QRect(QPoint(picture.center().x() - s.width() / 2, picture.center().y() - s.height() / 2), s), t.image);
        }
        const QRect label(t.rect.left() + kBorder + 3, t.rect.bottom() - kLabel + 1, std::max(0, t.rect.width() - 2 * kBorder - 6), kLabel - kBorder);
        p.setPen(t.mark == Mark::None ? QColor(0xe6, 0xe8, 0xee) : mark);
        p.drawText(label, Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(t.title, Qt::ElideMiddle, label.width()));

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(mark, kBorder, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        p.drawRect(t.rect.adjusted(1, 1, -2, -2));

        // Nhan chu kem mau (khong chi dua vao mau de phan biet)
        QString badge;
        if (t.kind == Kind::Program) badge = t.onAir ? "ON AIR" : QString();
        else if (t.kind == Kind::Scene) badge = t.mark == Mark::Program ? "PGM" : t.mark == Mark::Preview ? "PVW" : QString();
        if (!badge.isEmpty()) {
            const int bw = p.fontMetrics().horizontalAdvance(badge) + 12;
            const QRect b(t.rect.right() - bw - 6, t.rect.top() + 6, bw, 18);
            p.setPen(Qt::NoPen);
            p.setBrush(mark);
            p.drawRoundedRect(b, 4, 4);
            p.setPen(Qt::white);
            p.drawText(b, Qt::AlignCenter, badge);
        }
    }
}

void MultiviewWindow::placeButton() {
    const QSize s = fullBtn_->sizeHint();
    fullBtn_->setGeometry(width() - s.width() - 6, (kHeader - s.height()) / 2, s.width(), s.height());
}

void MultiviewWindow::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    placeButton();
    relayout();
}

void MultiviewWindow::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    for (const Tile& t : tiles_) {
        if (!t.rect.contains(e->position().toPoint())) continue;
        if (t.kind == Kind::Scene) emit scenePicked(t.scene);
        else if (t.channel != channel_) emit channelPicked(t.channel);
        return;
    }
}

void MultiviewWindow::hideEvent(QHideEvent* e) {
    engine_.setSceneThumbChannel(-1);                    // khong ai xem: render thread khong ve anh thu nho Scene nua
    storeGeometry();
    QWidget::hideEvent(e);
}

void MultiviewWindow::changeEvent(QEvent* e) {
    QWidget::changeEvent(e);
    if (e->type() != QEvent::WindowStateChange) return;
    fullBtn_->setText(isFullScreen() ? "Thoát toàn màn hình (Esc)" : "Toàn màn hình (F11)");
    placeButton();
}

} // namespace lc
