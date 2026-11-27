#pragma once
#include "core/Project.h"
#include "render/Engine.h"
#include <QTimer>
#include <QWidget>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QUndoStack;

namespace lc {

// Khung "Playlist": danh sach muc (Scene) cua kenh dang chon, tu chay len Program.
// Sua danh sach/cai dat di qua QUndoStack (hoan tac duoc, bi khoa khi Khoa van hanh) va KHONG doi Preview/Program.
// Chay/Tam dung/Dung/Truoc/Sau la dieu khien live (van chay khi khoa).
class PlaylistPanel : public QWidget {
public:
    PlaylistPanel(Project& project, RenderEngine& engine, QUndoStack* undo, QWidget* parent = nullptr);

    // Nhap mot file media (tao Scene moi) va tra ve Scene do; nullptr neu khong nhap duoc. Do MainWindow cung cap.
    std::function<std::shared_ptr<Scene>(const QString&)> importFile;
    std::function<void(const QString&)> showMessage;
    QString mediaFilter;                                   // bo loc hop thoai "Them file..."

    void setChannel(int index);
    void setLocked(bool on);
    void refresh();                                        // dung lai danh sach (sau khi project doi)
    void refreshTransitions();                             // danh sach hieu ung (sau khi nap lai plug-in)
    void addScenes(const std::vector<std::shared_ptr<Scene>>& scenes);   // them vao cuoi danh sach (hoan tac duoc)
    void apply(const std::shared_ptr<Channel>& ch, const PlaylistData& data);   // dung boi lenh Hoan tac/Lam lai

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    std::shared_ptr<Channel> channel() const;
    void edit(const PlaylistData& after, const QString& text, const QString& mergeKey = {});
    void addFiles();
    void removeSelected();
    void moveSelected(int dir);
    std::vector<int> selectedRows() const;
    void control(std::function<void(Channel&, double)> fn);
    void tick();
    QString itemText(int row, const PlaylistData& d, bool current) const;
    void queryItemInfo();

    // Tinh trang media cua tung muc: render thread doc Layer cua Scene roi gui lai (timer UI khong cham du lieu render thread).
    struct ItemInfo { double clip = 0; bool missing = false; bool broken = false; };
    struct InfoBox { std::mutex m; std::map<int, ItemInfo> items; };
    std::shared_ptr<InfoBox> info_ = std::make_shared<InfoBox>();

    Project& project_;
    RenderEngine& engine_;
    QUndoStack* undo_;
    QTimer timer_;
    int channel_ = 0;
    bool locked_ = false;
    int shownCurrent_ = -1;
    QLabel* status_ = nullptr;
    QListWidget* list_ = nullptr;
    QPushButton *play_ = nullptr, *pause_ = nullptr, *stop_ = nullptr, *prev_ = nullptr, *next_ = nullptr;
    QPushButton *addScene_ = nullptr, *addFile_ = nullptr, *remove_ = nullptr, *up_ = nullptr, *down_ = nullptr;
    QDoubleSpinBox *itemSeconds_ = nullptr, *imageSeconds_ = nullptr, *transSeconds_ = nullptr;
    QCheckBox* loop_ = nullptr;
    QComboBox* trans_ = nullptr;
    QWidget* editBox_ = nullptr;
};

} // namespace lc
