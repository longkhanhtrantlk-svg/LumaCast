#include "PlaylistPanel.h"
#include "DragList.h"
#include "Text.h"
#include "Theme.h"
#include "Ui.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace lc {

namespace {
// Mot lan sua playlist: luu ca cau hinh truoc/sau. Sua lien tiep cung mot o so (mergeKey) duoc gop thanh mot buoc.
class PlaylistCmd : public QUndoCommand {
public:
    PlaylistCmd(PlaylistPanel* panel, std::shared_ptr<Channel> ch, PlaylistData before, PlaylistData after, const QString& text, QString key)
        : QUndoCommand(text), panel_(panel), ch_(std::move(ch)), before_(std::move(before)), after_(std::move(after)), key_(std::move(key)) {}
    int id() const override { return key_.isEmpty() ? -1 : 7000101; }
    bool mergeWith(const QUndoCommand* other) override {
        auto* o = static_cast<const PlaylistCmd*>(other);
        if (o->ch_ != ch_ || o->key_ != key_) return false;
        after_ = o->after_;
        return true;
    }
    void redo() override { panel_->apply(ch_, after_); }
    void undo() override { panel_->apply(ch_, before_); }
private:
    PlaylistPanel* panel_;
    std::shared_ptr<Channel> ch_;
    PlaylistData before_, after_;
    QString key_;
};

QString clockText(double seconds) {
    const int s = int(std::ceil(std::max(0.0, seconds) - 0.05));
    return QString("%1:%2").arg(s / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}

QDoubleSpinBox* secondsSpin(const char* name, double lo, double hi, double step) {
    auto* s = new QDoubleSpinBox;
    s->setObjectName(name);
    s->setRange(lo, hi);
    s->setDecimals(1);
    s->setSingleStep(step);
    s->setSuffix(" s");
    s->setKeyboardTracking(false);          // go so xong moi ap dung
    return s;
}
} // namespace

PlaylistPanel::PlaylistPanel(Project& project, RenderEngine& engine, QUndoStack* undo, QWidget* parent)
    : QWidget(parent), project_(project), engine_(engine), undo_(undo) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(6, 4, 6, 4);

    auto* top = new QHBoxLayout;
    prev_ = ui::iconButton(Glyph::Prev, "plPrev", text::plPrev());
    play_ = ui::iconButton(Glyph::Play, "plPlay", text::plPlay());
    pause_ = ui::iconButton(Glyph::Pause, "plPause", text::plPause());
    stop_ = ui::iconButton(Glyph::Stop, "plStop", text::plStop());
    next_ = ui::iconButton(Glyph::Next, "plNext", text::plNext());
    for (auto* b : {prev_, play_, pause_, stop_, next_}) top->addWidget(b);
    status_ = new QLabel;
    status_->setObjectName("plStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setMinimumWidth(60);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    top->addWidget(status_, 1);
    v->addLayout(top);

    auto* items_list = new DragList;
    items_list->onReorder = [this](const std::vector<int>& rows, int before) { reorder(rows, before); };
    list_ = items_list;
    list_->setObjectName("playlistList");
    list_->setToolTip(text::plListTip());
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setTextElideMode(Qt::ElideMiddle);
    list_->setMinimumHeight(60);
    v->addWidget(list_, 1);

    editBox_ = new QWidget;
    auto* ev = new QVBoxLayout(editBox_);
    ev->setContentsMargins(0, 0, 0, 0);
    auto* items = new QHBoxLayout;
    addScene_ = ui::iconButton(Glyph::AddScene, "plAddScene", text::plAddScene());
    addFile_ = ui::iconButton(Glyph::AddFile, "plAddFile", text::plAddFile());
    remove_ = ui::iconButton(Glyph::Remove, "plRemove", text::plRemove());
    ui::setWhy(remove_, text::plNoSelectionWhy());
    itemSeconds_ = secondsSpin("plItemSeconds", 0, kPlaylistMaxSeconds, 1);
    itemSeconds_->setSpecialValueText("Tự động");
    itemSeconds_->setToolTip("Thời lượng riêng của mục đang chọn. Tự động: video chuyển khi phát hết, còn lại dùng thời lượng ảnh");
    for (QWidget* w : std::initializer_list<QWidget*>{addScene_, addFile_, remove_}) items->addWidget(w);
    items->addSpacing(12);
    items->addWidget(new QLabel("Thời lượng mục"));
    items->addWidget(itemSeconds_);
    items->addStretch();
    ev->addLayout(items);

    auto* opts = new QHBoxLayout;
    imageSeconds_ = secondsSpin("plImageSeconds", kPlaylistMinSeconds, kPlaylistMaxSeconds, 1);
    imageSeconds_->setToolTip("Thời gian giữ mỗi ảnh/Scene không phải video (tính sau khi chuyển vào xong)");
    loop_ = new QCheckBox("Lặp lại");
    loop_->setObjectName("plLoop");
    trans_ = new QComboBox;
    trans_->setObjectName("plTransition");
    transSeconds_ = secondsSpin("plTransSeconds", 0.1, 10, 0.1);
    opts->addWidget(new QLabel("Thời lượng ảnh"));
    opts->addWidget(imageSeconds_);
    opts->addWidget(loop_);
    opts->addSpacing(12);
    opts->addWidget(new QLabel("Hiệu ứng"));
    opts->addWidget(trans_);
    opts->addWidget(transSeconds_);
    opts->addStretch();
    ev->addLayout(opts);
    v->addWidget(editBox_);

    // Enter trong o so khong duoc kich hoat phim tat CUT cua cua so chinh
    for (QDoubleSpinBox* s : {itemSeconds_, imageSeconds_, transSeconds_}) s->installEventFilter(this);

    connect(play_, &QPushButton::clicked, this, [this] { control([](Channel& c, double now) { c.playlist.play(c, now); }); });
    connect(pause_, &QPushButton::clicked, this, [this] { control([](Channel& c, double now) { c.playlist.pause(now); }); });
    connect(stop_, &QPushButton::clicked, this, [this] { control([](Channel& c, double) { c.playlist.stop(); }); });
    connect(prev_, &QPushButton::clicked, this, [this] { control([](Channel& c, double now) { c.playlist.prev(c, now); }); });
    connect(next_, &QPushButton::clicked, this, [this] { control([](Channel& c, double now) { c.playlist.next(c, now); }); });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        const int id = it->data(Qt::UserRole).toInt();
        control([id](Channel& c, double now) { c.playlist.play(c, now, id); });
    });
    connect(list_, &QListWidget::itemSelectionChanged, this, [this] { refresh(); });

    connect(addScene_, &QPushButton::clicked, this, [this] {
        auto ch = channel();
        QMenu menu(this);
        menu.setObjectName("plSceneMenu");
        for (size_t i = 0; i < ch->scenes.size(); ++i) {
            auto sc = ch->scenes[i];
            menu.addAction(QString("%1  %2").arg(i + 1, 2, 10, QChar('0')).arg(sc->name), this, [this, sc] { addScenes({sc}); });
        }
        if (ch->scenes.size() > 1) {
            menu.addSeparator();
            menu.addAction("Tất cả Scene", this, [this, ch] { addScenes(ch->scenes); });
        }
        menu.exec(addScene_->mapToGlobal(QPoint(0, addScene_->height())));
    });
    connect(addFile_, &QPushButton::clicked, this, [this] { addFiles(); });
    connect(remove_, &QPushButton::clicked, this, [this] { removeSelected(); });

    connect(itemSeconds_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        PlaylistData d = channel()->playlist.data;
        const double s = value <= 0 ? 0 : std::max(value, kPlaylistMinSeconds);
        QString key = "item";
        bool changed = false;
        for (int r : selectedRows()) {
            key += QString(":%1").arg(d.items[size_t(r)].id);
            changed = changed || d.items[size_t(r)].seconds != s;
            d.items[size_t(r)].seconds = s;
        }
        if (changed) edit(d, "Thời lượng mục playlist", key);
    });
    connect(imageSeconds_, &QDoubleSpinBox::valueChanged, this, [this](double s) {
        PlaylistData d = channel()->playlist.data;
        if (d.imageSeconds == s) return;
        d.imageSeconds = s;
        edit(d, "Thời lượng ảnh playlist", "image");
    });
    connect(loop_, &QCheckBox::toggled, this, [this](bool on) {
        PlaylistData d = channel()->playlist.data;
        if (d.loop == on) return;
        d.loop = on;
        edit(d, "Lặp lại playlist");
    });
    connect(trans_, &QComboBox::currentIndexChanged, this, [this] {
        PlaylistData d = channel()->playlist.data;
        const QString key = trans_->currentData().toString();
        if (key.startsWith("b:")) {
            d.transition = TransitionType(key.mid(2).toInt());
            d.pluginId.clear();
        } else if (key.startsWith("p:")) {
            d.transition = TransitionType::Plugin;
            d.pluginId = key.mid(2);
        } else {
            return;
        }
        edit(d, "Hiệu ứng playlist");
    });
    connect(transSeconds_, &QDoubleSpinBox::valueChanged, this, [this](double s) {
        PlaylistData d = channel()->playlist.data;
        if (std::abs(d.transitionSeconds - s) < 1e-9) return;
        d.transitionSeconds = s;
        edit(d, "Thời gian chuyển playlist", "trans");
    });

    connect(&timer_, &QTimer::timeout, this, [this] { queryItemInfo(); tick(); });
    timer_.start(200);
    refreshTransitions();
    refresh();
}

std::shared_ptr<Channel> PlaylistPanel::channel() const { return project_.channels[size_t(channel_)]; }

bool PlaylistPanel::eventFilter(QObject* obj, QEvent* ev) {
    if (ev->type() == QEvent::ShortcutOverride) {
        const int key = static_cast<QKeyEvent*>(ev)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) { ev->accept(); return true; }
    }
    return QWidget::eventFilter(obj, ev);
}

void PlaylistPanel::setChannel(int index) {
    if (index < 0 || index >= int(project_.channels.size())) return;
    channel_ = index;
    list_->clearSelection();
    refresh();
}

void PlaylistPanel::setLocked(bool on) {
    locked_ = on;
    editBox_->setEnabled(!on);
    static_cast<DragList*>(list_)->setReorderEnabled(!on);
}

void PlaylistPanel::refreshTransitions() {
    {
        QSignalBlocker b(trans_);
        trans_->clear();
        for (auto t : {TransitionType::Fade, TransitionType::Cut, TransitionType::DipToBlack, TransitionType::WipeLeft,
                       TransitionType::WipeRight, TransitionType::WipeUp, TransitionType::WipeDown})
            trans_->addItem(transitionName(t), "b:" + QString::number(int(t)));
        for (const auto& d : engine_.registry().all(EffectDef::Transition)) trans_->addItem(d->name, "p:" + d->id);
    }
    refresh();
}

void PlaylistPanel::queryItemInfo() {
    auto ch = channel();
    engine_.post([ch, box = info_](double) {
        std::map<int, ItemInfo> out;
        for (const PlaylistItem& it : ch->playlist.data.items) {
            ItemInfo i;
            i.clip = sceneClipSeconds(*it.scene);
            i.missing = std::find(ch->scenes.begin(), ch->scenes.end(), it.scene) == ch->scenes.end();
            for (const Layer& l : it.scene->layers)
                if (l.visible && l.source && !l.source->error().isEmpty()) i.broken = true;
            out[it.id] = i;
        }
        std::lock_guard lk(box->m);
        box->items = std::move(out);
    });
}

QString PlaylistPanel::itemText(int row, const PlaylistData& d, bool current) const {
    const PlaylistItem& it = d.items[size_t(row)];
    ItemInfo info;
    {
        std::lock_guard lk(info_->m);
        if (auto k = info_->items.find(it.id); k != info_->items.end()) info = k->second;
    }
    QString time;
    if (it.seconds > 0) time = QString("%1 s").arg(it.seconds, 0, 'g', 4);
    else if (info.clip > 0) time = QString("video, hết thì chuyển (%1)").arg(clockText(info.clip));
    else time = QString("%1 s (tự động)").arg(d.imageSeconds, 0, 'g', 4);
    QString text = QString("%1 %2  %3  —  %4").arg(current ? "▶" : "  ").arg(row + 1, 2, 10, QChar('0')).arg(it.scene->name, time);
    if (info.missing) text += "   ⚠ Scene đã bị gỡ (sẽ bỏ qua)";
    if (info.broken) text += "   ⚠ lỗi media (sẽ bỏ qua)";
    return text;
}

std::vector<int> PlaylistPanel::selectedRows() const {
    std::vector<int> rows;
    const int n = int(channel()->playlist.data.items.size());
    for (int i = 0; i < list_->count() && i < n; ++i)
        if (list_->item(i)->isSelected()) rows.push_back(i);
    return rows;
}

// Dong bo toan bo widget voi du lieu (giu lua chon theo id muc). Chi UI thread sua playlist.data (qua postAndWait) nen doc truc tiep duoc.
void PlaylistPanel::refresh() {
    auto ch = channel();
    const PlaylistData& d = ch->playlist.data;
    const int current = ch->playlist.currentId();
    {
        QSignalBlocker bl(list_);
        std::vector<int> keep;
        for (auto* it : list_->selectedItems()) keep.push_back(it->data(Qt::UserRole).toInt());
        while (list_->count() > int(d.items.size())) delete list_->takeItem(list_->count() - 1);
        while (list_->count() < int(d.items.size())) list_->addItem(QString());
        for (int i = 0; i < int(d.items.size()); ++i) {
            QListWidgetItem* it = list_->item(i);
            const int id = d.items[size_t(i)].id;
            it->setData(Qt::UserRole, id);
            it->setText(itemText(i, d, id == current));
            it->setForeground(id == current ? QColor(colors::kProgram) : QColor(0xe6, 0xe8, 0xee));
            it->setSelected(std::find(keep.begin(), keep.end(), id) != keep.end());
        }
    }
    shownCurrent_ = current;

    const auto rows = selectedRows();
    {
        QSignalBlocker b1(itemSeconds_), b2(imageSeconds_), b3(loop_), b4(trans_), b5(transSeconds_);
        if (!itemSeconds_->hasFocus() || rows.empty()) itemSeconds_->setValue(rows.empty() ? 0 : d.items[size_t(rows[0])].seconds);
        imageSeconds_->setValue(d.imageSeconds);
        loop_->setChecked(d.loop);
        const QString key = d.transition == TransitionType::Plugin ? "p:" + d.pluginId : "b:" + QString::number(int(d.transition));
        trans_->setCurrentIndex(trans_->findData(key));            // -1: plug-in khong co tren may nay (giu nguyen trong project)
        transSeconds_->setValue(d.transitionSeconds);
    }
    itemSeconds_->setEnabled(!rows.empty());
    remove_->setEnabled(!rows.empty());
    transSeconds_->setEnabled(d.transition != TransitionType::Cut);
    queryItemInfo();
    tick();
}

void PlaylistPanel::tick() {
    for (const auto& c : project_.channels)
        for (const QString& m : c->playlist.takeNotices()) {
            if (showMessage) showMessage(m);
            status_->setToolTip(m);
        }

    auto ch = channel();
    const PlaylistData& d = ch->playlist.data;
    const Playlist::State state = ch->playlist.state();
    const int current = ch->playlist.currentId();
    if (current != shownCurrent_) {
        shownCurrent_ = current;
        refresh();
        return;
    }
    // trang thai loi cua media doi theo thoi gian (video bao loi sau khi mo)
    for (int i = 0; i < list_->count() && i < int(d.items.size()); ++i) {
        const QString text = itemText(i, d, d.items[size_t(i)].id == current);
        if (list_->item(i)->text() != text) list_->item(i)->setText(text);
    }

    QString text;
    if (state == Playlist::State::Stopped) {
        text = d.items.empty() ? "Chưa có mục nào" : "Đã dừng";
    } else {
        text = state == Playlist::State::Playing ? "▶ Đang chạy" : "⏸ Tạm dừng";
        for (size_t i = 0; i < d.items.size(); ++i)
            if (d.items[i].id == current)
                text += QString(" · Mục %1/%2: %3").arg(i + 1).arg(d.items.size()).arg(d.items[i].scene->name);
        const double left = ch->playlist.remaining();
        if (left >= 0) text += " · còn " + clockText(left);
    }
    status_->setText(text);
    ui::setWhy(play_, d.items.empty() ? text::plEmptyWhy() : text::plPlayingWhy());
    play_->setEnabled(!d.items.empty() && state != Playlist::State::Playing);
    ui::setWhy(pause_, text::plNotRunningWhy());
    pause_->setEnabled(state == Playlist::State::Playing);
    for (auto* b : {stop_, prev_, next_}) {
        ui::setWhy(b, text::plNotRunningWhy());
        b->setEnabled(state != Playlist::State::Stopped);
    }
}

void PlaylistPanel::control(std::function<void(Channel&, double)> fn) {
    auto ch = channel();
    engine_.post([ch, fn = std::move(fn)](double now) { fn(*ch, now); });
}

void PlaylistPanel::edit(const PlaylistData& after, const QString& text, const QString& mergeKey) {
    if (locked_) return;
    auto ch = channel();
    undo_->push(new PlaylistCmd(this, ch, ch->playlist.data, after, text, mergeKey));
}

void PlaylistPanel::apply(const std::shared_ptr<Channel>& ch, const PlaylistData& data) {
    engine_.postAndWait([ch, data](double) { ch->playlist.data = data; });
    refresh();
}

void PlaylistPanel::addScenes(const std::vector<std::shared_ptr<Scene>>& scenes) {
    if (locked_ || scenes.empty()) return;
    PlaylistData d = channel()->playlist.data;
    for (const auto& s : scenes) {
        PlaylistItem it;
        it.id = newPlaylistItemId();
        it.scene = s;
        d.items.push_back(it);
    }
    edit(d, scenes.size() == 1 ? "Thêm vào playlist: " + scenes[0]->name : QString("Thêm %1 mục vào playlist").arg(scenes.size()));
}

void PlaylistPanel::addFiles() {
    if (locked_ || !importFile) return;
    std::vector<std::shared_ptr<Scene>> added;
    for (const QString& f : QFileDialog::getOpenFileNames(this, "Thêm file vào playlist", QString(), mediaFilter))
        if (auto s = importFile(f)) added.push_back(s);
    addScenes(added);
}

void PlaylistPanel::removeSelected() {
    PlaylistData d = channel()->playlist.data;
    const auto rows = selectedRows();
    if (rows.empty()) return;
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) d.items.erase(d.items.begin() + *it);
    edit(d, "Bỏ mục khỏi playlist");
}

void PlaylistPanel::reorder(const std::vector<int>& rows, int before) {
    PlaylistData d = channel()->playlist.data;
    if (locked_ || rows.empty() || rows.back() >= int(d.items.size())) return;
    d.items = movedRows(d.items, rows, before);
    edit(d, text::plUndoReorder());
}

} // namespace lc
