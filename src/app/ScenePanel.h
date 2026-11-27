#pragma once
#include "render/Engine.h"
#include <QHash>
#include <QListWidget>
#include <QSlider>
#include <QWidget>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

class QLabel;
class QLineEdit;
class QToolButton;

namespace lc {

class SceneThumbView;

// Mot o trong danh sach Scenes: anh thu nho danh so, ten, doi ten/xoa, phat/tam dung/ve dau/tua/lap, tat tieng + am luong.
// Bam vao ANH hoac TEN di xuong danh sach (bam = Preview, nhap doi = CUT); vung nut dieu khien NUOT chuot, nen bam truot
// giua hai nut khong bao gio dua Scene len song.
class SceneCard : public QWidget {
    Q_OBJECT
public:
    enum class Mark { None, Program, Preview };
    static constexpr int kWidth = 184, kThumbW = 172, kThumbH = 97;

    SceneCard(std::shared_ptr<Scene> scene, int number, const QString& name, QWidget* parent = nullptr);

    const std::shared_ptr<Scene>& scene() const { return scene_; }
    void setMark(Mark m);
    Mark mark() const { return mark_; }
    void setMedia(const SceneMediaState& st);
    void setThumb(const QImage& img);
    bool hasThumb() const;
    // Nguon truc tiep: cham trang thai tren anh (mau rong = Scene khong co nguon truc tiep).
    void setLive(const QColor& color, bool hollow, const QString& tip);
    void setProblem(const QString& error);
    void setLocked(bool on);
    void startRename();
    bool renaming() const;

    std::function<void()> onPlayPause, onRewind, onDelete;
    std::function<void(double seconds)> onSeek;
    std::function<void(bool)> onLoop;
    std::function<void(const QString&)> onRename;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    void finishRename(bool commit);
    void syncEnabled();
    void syncMuteIcon();

    std::shared_ptr<Scene> scene_;
    Mark mark_ = Mark::None;
    SceneMediaState media_;
    bool locked_ = false;
    bool mediaKnown_ = false;
    bool pendingSeek_ = false;
    qint64 lastSeekMs_ = 0;
    SceneThumbView* thumb_ = nullptr;
    QLabel* name_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QToolButton *renameB_ = nullptr, *deleteB_ = nullptr, *rewindB_ = nullptr, *playB_ = nullptr, *loopB_ = nullptr, *muteB_ = nullptr;
    QSlider *seek_ = nullptr, *volume_ = nullptr;
    QLabel *time_ = nullptr, *volumeText_ = nullptr;
};

// Khung "Scenes" (thay cho Media Pool + cot Scene): nut nhap media / NDI / camera va mot luoi o Scene cua kenh dang chon.
// Lua chon o (Preview/CUT) do cua so chinh xu ly qua tin hieu cua list(); khung nay chi goi nguoc lai cho cac viec SUA
// (doi ten, xoa, lap) va tu gui lenh phat/tieng cho render thread.
class ScenePanel : public QWidget {
    Q_OBJECT
public:
    ScenePanel(Project& project, RenderEngine& engine, QWidget* parent = nullptr);

    QListWidget* list() const { return list_; }
    QToolButton* importButton() const { return importB_; }
    QToolButton* ndiButton() const { return ndiB_; }
    QToolButton* cameraButton() const { return camB_; }
    SceneCard* card(int row) const;

    void setScenes(int channel, const std::vector<std::shared_ptr<Scene>>& scenes, const std::vector<QString>& names);
    void setMarks(int program, int preview);
    void refresh();                              // timer cua cua so chinh (~15 Hz)
    void setLocked(bool on);
    void reveal(const Scene* scene);             // cuon toi o cua Scene

    std::function<void(const std::shared_ptr<Scene>&, const QString&)> renameScene;
    std::function<void(const std::shared_ptr<Scene>&)> deleteScene;
    std::function<void(const std::shared_ptr<Scene>&, bool)> setSceneLoop;

private:
    struct Info {
        SceneMediaState media;
        std::shared_ptr<Source> live;            // nguon truc tiep dau tien cua Scene (neu co)
        QString problem;
    };
    struct Box { std::mutex m; std::map<const Scene*, Info> info; bool pending = false; };
    void query();
    void transport(const std::shared_ptr<Scene>& s, std::function<void(Scene&, double)> op, bool intervenes);

    Project& project_;
    RenderEngine& engine_;
    QListWidget* list_ = nullptr;
    QToolButton *importB_ = nullptr, *ndiB_ = nullptr, *camB_ = nullptr;
    QLabel* hint_ = nullptr;
    std::vector<std::shared_ptr<Scene>> scenes_;
    std::vector<QString> names_;
    QHash<const Scene*, QImage> thumbs_;         // giu anh cu khi dung lai danh sach
    std::shared_ptr<Box> box_ = std::make_shared<Box>();
    int channel_ = 0;
    int program_ = -1, preview_ = -1;
    bool locked_ = false;
};

} // namespace lc
