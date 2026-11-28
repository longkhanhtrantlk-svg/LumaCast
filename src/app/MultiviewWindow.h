#pragma once
#include "render/Engine.h"
#include "core/I18n.h"
#include <QImage>
#include <QRect>
#include <QString>
#include <QWidget>
#include <vector>

class QPushButton;
class QScreen;

namespace lc {

// Multiview (UI-06): luoi Preview/Program cua moi Channel + mot o cho moi Scene cua Channel dang chon.
// Qt::Tool (con cua cua so chinh) nen phim tat cua cua so chinh (Space = TAKE, Enter = CUT, 1-9...) van chay khi cua so nay
// duoc chon. Bam o chi doi PREVIEW (o Scene) hoac doi Channel dang chon (o PGM/PVW): KHONG BAO GIO dua gi len Program.
// F11 / nut = toan man hinh (Esc thoat); tu choi tren man hinh dang co Output toan man hinh.
class MultiviewWindow : public QWidget {
    Q_OBJECT
public:
    enum class Kind { Program, Preview, Scene };
    enum class Mark { None, Program, Preview };      // Program = vien do (dang On Air), Preview = vien xanh la
    struct Tile {
        Kind kind = Kind::Scene;
        int channel = 0;
        int scene = -1;                              // chi so Scene trong Channel (o PGM/PVW: Scene dang o do, -1 neu khong)
        QRect rect;
        Mark mark = Mark::None;
        bool onAir = false;                          // hien nhan ON AIR
        QString title;
        QImage image;
    };

    MultiviewWindow(Project& project, RenderEngine& engine, QWidget* parent);
    ~MultiviewWindow() override;                     // thoat ung dung khi dang mo: van nho vi tri cua so

    // Goi tu timer cua cua so chinh (~15 Hz) khi cua so nay dang hien.
    void refresh(int selectedChannel, const std::vector<std::vector<QString>>& sceneNames);
    const std::vector<Tile>& tiles() const { return tiles_; }
    bool toggleFullScreen();                         // false = bi tu choi (man hinh dang co Output toan man hinh)
    bool screenHasFullscreenOutput(const QScreen* screen) const;
    QString notice() const;

signals:
    void channelPicked(int channel);
    void scenePicked(int scene);                     // Scene cua Channel dang chon -> Preview

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void changeEvent(QEvent* e) override;

private:
    void relayout();
    void showNotice(const i18n::Text& text);
    void placeButton();
    void storeGeometry() const;                      // bo qua khi dang toan man hinh

    Project& project_;
    RenderEngine& engine_;
    QPushButton* fullBtn_ = nullptr;
    std::vector<Tile> tiles_;
    int channel_ = 0;
    int sceneCount_ = 0;
    QString notice_;
    qint64 noticeUntilMs_ = 0;
};

} // namespace lc
