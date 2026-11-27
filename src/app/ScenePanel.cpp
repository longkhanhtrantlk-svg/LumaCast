#include "ScenePanel.h"
#include "Icons.h"
#include "Theme.h"
#include "live/LiveSource.h"
#include <QContextMenuEvent>
#include <QDateTime>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace lc {

// Anh thu nho 16:9 voi so thu tu, nhan PGM/PVW, cham trang thai nguon truc tiep va dau loi media.
class SceneThumbView : public QWidget {
public:
    explicit SceneThumbView(int number, QWidget* parent) : QWidget(parent), number_(number) {
        setFixedSize(SceneCard::kThumbW, SceneCard::kThumbH);
        setObjectName("scThumb");
    }
    void set(const QImage& img) { image_ = img; update(); }
    bool has() const { return !image_.isNull(); }
    void setMark(SceneCard::Mark m) { mark_ = m; update(); }
    void setLive(const QColor& c, bool hollow) {
        if (c == live_ && hollow == hollow_) return;
        live_ = c; hollow_ = hollow; update();
    }
    void setProblem(bool on) { if (on != problem_) { problem_ = on; update(); } }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        if (!image_.isNull()) {
            const QSize s = image_.size().scaled(size(), Qt::KeepAspectRatio);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(QRect(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s), image_);
        }
        QFont f = p.font();
        f.setBold(true);
        f.setPixelSize(12);
        p.setFont(f);
        auto badge = [&](const QString& text, const QColor& bg, bool right) {
            const int w = p.fontMetrics().horizontalAdvance(text) + 10;
            const QRect r(right ? width() - w - 2 : 2, 2, w, 17);
            p.fillRect(r, bg);
            p.setPen(Qt::white);
            p.drawText(r, Qt::AlignCenter, text);
        };
        badge(QString::number(number_), QColor(0, 0, 0, 190), false);
        if (mark_ == SceneCard::Mark::Program) badge("PGM", QColor(colors::kProgram), true);
        else if (mark_ == SceneCard::Mark::Preview) badge("PVW", QColor(colors::kPreview).darker(140), true);
        if (live_.isValid()) {
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(live_, 2));
            p.setBrush(hollow_ ? QBrush(Qt::NoBrush) : QBrush(live_));
            p.drawEllipse(QPointF(10, height() - 10), 4.5, 4.5);
        }
        if (problem_) {
            const QRect r(width() - 22, height() - 20, 20, 18);
            p.fillRect(r, QColor(0, 0, 0, 190));
            p.setPen(QColor(colors::kWarn));
            p.drawText(r, Qt::AlignCenter, "⚠");
        }
    }

private:
    int number_;
    QImage image_;
    SceneCard::Mark mark_ = SceneCard::Mark::None;
    QColor live_;
    bool hollow_ = false, problem_ = false;
};

namespace {

// Vung chua nut dieu khien: nuot moi su kien chuot (ke ca khe giua cac nut va nut dang bi mo) de bam truot
// khong thanh "chon Scene" hay "nhap doi = CUT".
class ControlStrip : public QWidget {
public:
    using QWidget::QWidget;
protected:
    void mousePressEvent(QMouseEvent* e) override { e->accept(); }
    void mouseReleaseEvent(QMouseEvent* e) override { e->accept(); }
    void mouseDoubleClickEvent(QMouseEvent* e) override { e->accept(); }
};

// Thanh keo: bam dau nhay toi do. Con lan chuot KHONG doi gia tri (de cuon danh sach khong thanh tua/doi tieng).
class CardSlider : public QSlider {
public:
    explicit CardSlider(QWidget* parent) : QSlider(Qt::Horizontal, parent) {
        setFocusPolicy(Qt::NoFocus);
        setFixedHeight(18);
    }
protected:
    void wheelEvent(QWheelEvent* e) override { e->ignore(); }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) { e->accept(); return; }
        setSliderDown(true);
        setFromX(e->position().x());
        e->accept();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (isSliderDown()) setFromX(e->position().x());
        e->accept();
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && isSliderDown()) setSliderDown(false);
        e->accept();
    }
private:
    void setFromX(double x) {
        const double f = std::clamp((x - 5.0) / std::max(1.0, width() - 10.0), 0.0, 1.0);
        setValue(minimum() + int(std::lround(f * (maximum() - minimum()))));
    }
};

// Chi giu cho (bam/nhap doi/chu tro giup); moi thu nhin thay do SceneCard ve.
class CardDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter*, const QStyleOptionViewItem&, const QModelIndex&) const override {}
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const override {
        editor->setGeometry(option.rect);
    }
};

QToolButton* iconButton(Glyph g, const char* name, const QString& tip, QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setObjectName(name);
    b->setIcon(glyphIcon(g));
    b->setIconSize(QSize(14, 14));
    b->setFixedSize(22, 20);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);          // Space/Enter luon la TAKE/CUT
    b->setToolTip(tip);
    return b;
}

QString clockText(double seconds) {
    const int s = int(std::ceil(std::max(0.0, seconds) - 0.05));
    if (s >= 3600) return QString("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

} // namespace

// ---------------------------------------------------------------- SceneCard

SceneCard::SceneCard(std::shared_ptr<Scene> scene, int number, const QString& name, QWidget* parent)
    : QWidget(parent), scene_(std::move(scene)) {
    setObjectName("sceneCard");
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(6, 6, 6, 5);
    v->setSpacing(2);

    thumb_ = new SceneThumbView(number, this);
    v->addWidget(thumb_, 0, Qt::AlignHCenter);

    auto* nameRow = new QHBoxLayout;
    nameRow->setSpacing(0);
    name_ = new QLabel(this);
    name_->setObjectName("scName");
    name_->setTextFormat(Qt::PlainText);
    name_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    nameEdit_ = new QLineEdit(this);
    nameEdit_->setObjectName("scNameEdit");
    nameEdit_->setFixedHeight(20);
    nameEdit_->hide();
    nameEdit_->installEventFilter(this);
    name_->setProperty("full", name);
    auto* edits = new ControlStrip(this);
    auto* eh = new QHBoxLayout(edits);
    eh->setContentsMargins(0, 0, 0, 0);
    eh->setSpacing(0);
    renameB_ = iconButton(Glyph::Rename, "scRename", "Đổi tên Scene", edits);
    deleteB_ = iconButton(Glyph::Delete, "scDelete", "Xoá Scene (Hoàn tác được)", edits);
    eh->addWidget(renameB_);
    eh->addWidget(deleteB_);
    nameRow->addWidget(name_, 1);
    nameRow->addWidget(nameEdit_, 1);
    nameRow->addWidget(edits);
    v->addLayout(nameRow);

    auto* controls = new ControlStrip(this);
    controls->setObjectName("scControls");
    auto* cv = new QVBoxLayout(controls);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(1);

    auto* t = new QHBoxLayout;
    t->setSpacing(1);
    rewindB_ = iconButton(Glyph::Rewind, "scRewind", "Về đầu", controls);
    playB_ = iconButton(Glyph::Play, "scPlay", "Phát", controls);
    seek_ = new CardSlider(controls);
    seek_->setObjectName("scSeek");
    seek_->setRange(0, 1000);
    seek_->setToolTip("Kéo để tua. Khi đưa từ Preview lên Program, video vẫn phát lại từ đầu (tắt ở Timeline: \"Phát lại khi lên Program\")");
    time_ = new QLabel(controls);
    time_->setObjectName("scTime");
    time_->setFixedWidth(42);
    time_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    time_->setToolTip("Thời gian còn lại");
    t->addWidget(rewindB_);
    t->addWidget(playB_);
    t->addWidget(seek_, 1);
    t->addWidget(time_);
    cv->addLayout(t);

    auto* a = new QHBoxLayout;
    a->setSpacing(1);
    loopB_ = iconButton(Glyph::Loop, "scLoop", "Lặp lại", controls);
    loopB_->setCheckable(true);
    muteB_ = iconButton(Glyph::Speaker, "scMute", "Tắt tiếng Scene này (chỉ Scene ở PROGRAM mới ra loa)", controls);
    muteB_->setCheckable(true);
    volume_ = new CardSlider(controls);
    volume_->setObjectName("scVolume");
    volume_->setRange(0, 150);
    volume_->setToolTip("Âm lượng Scene (100% = nguyên gốc)");
    volumeText_ = new QLabel(controls);
    volumeText_->setFixedWidth(42);
    volumeText_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    a->addWidget(loopB_);
    a->addWidget(muteB_);
    a->addWidget(volume_, 1);
    a->addWidget(volumeText_);
    cv->addLayout(a);
    v->addWidget(controls);
    for (QWidget* w : std::initializer_list<QWidget*>{rewindB_, playB_, seek_, time_, loopB_, muteB_, volume_, volumeText_}) {
        QSizePolicy sp = w->sizePolicy();
        sp.setRetainSizeWhenHidden(true);
        w->setSizePolicy(sp);
    }

    {
        QSignalBlocker b1(muteB_), b2(volume_), b3(loopB_);
        muteB_->setChecked(scene_->audio.mute.load());
        volume_->setValue(int(std::lround(scene_->audio.volume.load() * 100)));
        loopB_->setChecked(scene_->loop);
    }
    volumeText_->setText(QString("%1%").arg(volume_->value()));
    syncMuteIcon();
    setFixedWidth(kWidth);
    name_->setText(name_->fontMetrics().elidedText(name, Qt::ElideMiddle, kWidth - 12 - 44));
    name_->setToolTip(name);
    syncEnabled();

    connect(renameB_, &QToolButton::clicked, this, [this] { startRename(); });
    connect(deleteB_, &QToolButton::clicked, this, [this] { if (onDelete) onDelete(); });
    connect(nameEdit_, &QLineEdit::editingFinished, this, [this] { finishRename(true); });
    connect(playB_, &QToolButton::clicked, this, [this] { if (onPlayPause) onPlayPause(); });
    connect(rewindB_, &QToolButton::clicked, this, [this] { if (onRewind) onRewind(); });
    connect(loopB_, &QToolButton::clicked, this, [this](bool on) { if (onLoop) onLoop(on); });
    connect(muteB_, &QToolButton::toggled, this, [this](bool on) {
        scene_->audio.mute = on;
        syncMuteIcon();
    });
    connect(volume_, &QSlider::valueChanged, this, [this](int pct) {
        scene_->audio.volume = float(pct) / 100.0f;
        volumeText_->setText(QString("%1%").arg(pct));
    });
    // Tua trong luc keo: toi da ~7 lan/giay (moi lan tua video phai giai ma lai tu keyframe), nha chuot thi tua dung cho.
    auto sendSeek = [this] {
        pendingSeek_ = false;
        lastSeekMs_ = QDateTime::currentMSecsSinceEpoch();
        const double seconds = media_.duration * seek_->value() / 1000.0;
        time_->setText("-" + clockText(media_.duration - seconds));
        if (onSeek) onSeek(seconds);
    };
    connect(seek_, &QSlider::valueChanged, this, [this, sendSeek] {
        if (QDateTime::currentMSecsSinceEpoch() - lastSeekMs_ >= 150) sendSeek();
        else pendingSeek_ = true;
    });
    connect(seek_, &QSlider::sliderReleased, this, [this, sendSeek] { if (pendingSeek_) sendSeek(); });
}

void SceneCard::setMark(Mark m) {
    if (m == mark_) return;
    mark_ = m;
    thumb_->setMark(m);
    update();
}

void SceneCard::setThumb(const QImage& img) { if (!img.isNull()) thumb_->set(img); }
bool SceneCard::hasThumb() const { return thumb_->has(); }

void SceneCard::setMedia(const SceneMediaState& st) {
    const bool first = !mediaKnown_;
    const bool changed = first || st.canTransport() != media_.canTransport() || st.audio != media_.audio || st.playing != media_.playing;
    const bool iconChanged = first || st.playing != media_.playing;
    media_ = st;
    mediaKnown_ = true;
    if (changed) syncEnabled();
    if (iconChanged) {
        playB_->setIcon(glyphIcon(st.playing ? Glyph::Pause : Glyph::Play));
        playB_->setToolTip(st.playing ? "Tạm dừng" : "Phát");
    }
    // Vua tua: gia tri doc ve co the con cu, dung de thanh nhay lui
    const bool settled = !seek_->isSliderDown() && QDateTime::currentMSecsSinceEpoch() - lastSeekMs_ > 400;
    if (settled) {
        QSignalBlocker b(seek_);
        seek_->setValue(st.duration > 0 ? int(std::lround(1000.0 * st.position / st.duration)) : 0);
        time_->setText(st.canTransport() && st.duration > 0 ? "-" + clockText(st.duration - st.position) : QString());
    }
    if (loopB_->isChecked() != st.loop) { QSignalBlocker b(loopB_); loopB_->setChecked(st.loop); }
    // am thanh co the doi tu noi khac (vd. hop dong cua so Scene khac): doc lai tru khi dang keo
    const bool mute = scene_->audio.mute.load();
    if (muteB_->isChecked() != mute) { QSignalBlocker b(muteB_); muteB_->setChecked(mute); syncMuteIcon(); }
    const int pct = int(std::lround(scene_->audio.volume.load() * 100));
    if (!volume_->isSliderDown() && volume_->value() != std::min(pct, volume_->maximum())) {
        QSignalBlocker b(volume_);
        volume_->setValue(pct);
        volumeText_->setText(QString("%1%").arg(volume_->value()));
    }
}

void SceneCard::setLive(const QColor& color, bool hollow, const QString& tip) {
    thumb_->setLive(color, hollow);
    if (thumb_->toolTip() != tip) thumb_->setToolTip(tip);
}

void SceneCard::setProblem(const QString& error) {
    thumb_->setProblem(!error.isEmpty());
    if (!error.isEmpty() && thumb_->toolTip() != error) thumb_->setToolTip(error);
}

void SceneCard::setLocked(bool on) {
    locked_ = on;
    if (on) finishRename(false);
    syncEnabled();
}

// Khoa van hanh: doi ten/xoa/lap la SUA; phat/tua/tieng la dieu khien live nen van dung duoc.
void SceneCard::syncEnabled() {
    renameB_->setEnabled(!locked_);
    deleteB_->setEnabled(!locked_);
    // Scene khong co gi de phat / khong co tieng: an han cac nut do (o van giu nguyen chieu cao), khoi tuong la dung duoc
    const bool transport = media_.canTransport();
    for (QWidget* w : std::initializer_list<QWidget*>{rewindB_, playB_, seek_, time_}) { w->setEnabled(transport); w->setVisible(transport); }
    loopB_->setEnabled(transport && !locked_);
    loopB_->setVisible(transport);
    for (QWidget* w : std::initializer_list<QWidget*>{muteB_, volume_, volumeText_}) { w->setEnabled(media_.audio); w->setVisible(media_.audio); }
}

void SceneCard::syncMuteIcon() {
    muteB_->setIcon(glyphIcon(muteB_->isChecked() ? Glyph::Muted : Glyph::Speaker));
}

void SceneCard::startRename() {
    if (locked_) return;
    nameEdit_->setText(name_->property("full").toString());
    name_->hide();
    nameEdit_->show();
    nameEdit_->setFocus();
    nameEdit_->selectAll();
}

bool SceneCard::renaming() const { return nameEdit_->isVisibleTo(this); }

void SceneCard::finishRename(bool commit) {
    if (!renaming()) return;
    const QString text = nameEdit_->text().trimmed();
    nameEdit_->hide();
    name_->show();
    nameEdit_->clearFocus();
    if (!commit || locked_ || text.isEmpty() || text == name_->property("full").toString()) return;
    // goi sau khi su kien nay xong: doi ten dung lai ca danh sach (huy o nay)
    QTimer::singleShot(0, this, [this, text] { if (onRename) onRename(text); });
}

bool SceneCard::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == nameEdit_) {
        if (ev->type() == QEvent::ShortcutOverride) {
            const int key = static_cast<QKeyEvent*>(ev)->key();
            if (key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter) { ev->accept(); return true; }
        }
        if (ev->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Escape) {
            finishRename(false);
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void SceneCard::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor border = mark_ == Mark::Program ? QColor(colors::kProgram)
                          : mark_ == Mark::Preview ? QColor(colors::kPreview) : QColor(0x34, 0x39, 0x4a);
    p.setPen(QPen(border, mark_ == Mark::None ? 1.0 : 3.0));
    p.setBrush(QColor(0x1d, 0x20, 0x2b));
    p.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 5, 5);
}

// Chi chuot TRAI tren anh/ten di xuong danh sach; nut phai mo menu va khong doi Preview.
void SceneCard::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) e->ignore();
    else e->accept();
}

void SceneCard::contextMenuEvent(QContextMenuEvent* e) {
    auto* menu = new QMenu(this);                 // popup (khong exec): danh sach co the duoc dung lai trong luc menu dang mo
    menu->setObjectName("scMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction(glyphIcon(Glyph::Rename), "Đổi tên", this, [this] { startRename(); })->setEnabled(!locked_);
    menu->addAction(glyphIcon(Glyph::Delete), "Xoá Scene", this, [this] { if (onDelete) QTimer::singleShot(0, this, onDelete); })->setEnabled(!locked_);
    menu->popup(e->globalPos());
}

// ---------------------------------------------------------------- ScenePanel

ScenePanel::ScenePanel(Project& project, RenderEngine& engine, QWidget* parent) : QWidget(parent), project_(project), engine_(engine) {
    setObjectName("scenePanel");
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 3, 4, 3);
    v->setSpacing(3);

    auto* top = new QHBoxLayout;
    auto tool = [this](Glyph g, const char* name, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setObjectName(name);
        b->setIcon(glyphIcon(g));
        b->setIconSize(QSize(18, 18));
        b->setFixedSize(34, 26);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tip);
        return b;
    };
    importB_ = tool(Glyph::Import, "importBtn", "Nhập media (ảnh/video): mỗi file thành một Scene. Cũng có thể kéo thả file vào cửa sổ");
    ndiB_ = tool(Glyph::Ndi, "addNdiBtn", "Thêm nguồn NDI từ mạng (cần NDI Runtime)");
    camB_ = tool(Glyph::Camera, "addCamBtn", "Thêm webcam / card capture");
    for (QToolButton* b : {importB_, ndiB_, camB_}) top->addWidget(b);
    hint_ = new QLabel("Bấm = Preview · Nhấp đúp = CUT · Phím 1–9");
    hint_->setStyleSheet("color:#8a90a2");
    hint_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    top->addSpacing(8);
    top->addWidget(hint_, 1);
    v->addLayout(top);

    list_ = new QListWidget;
    list_->setObjectName("sceneList");
    list_->setViewMode(QListView::IconMode);
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setSpacing(3);
    list_->setItemDelegate(new CardDelegate(list_));
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->verticalScrollBar()->setSingleStep(24);
    list_->setDragDropMode(QAbstractItemView::NoDragDrop);
    list_->setMinimumHeight(60);
    v->addWidget(list_, 1);
}

SceneCard* ScenePanel::card(int row) const {
    QListWidgetItem* it = list_->item(row);
    return it ? static_cast<SceneCard*>(list_->itemWidget(it)) : nullptr;
}

void ScenePanel::setScenes(int channel, const std::vector<std::shared_ptr<Scene>>& scenes, const std::vector<QString>& names) {
    const int scroll = channel == channel_ ? list_->verticalScrollBar()->value() : 0;
    channel_ = channel;
    scenes_ = scenes;
    names_ = names;
    list_->clear();
    for (size_t i = 0; i < scenes_.size(); ++i) {
        const std::shared_ptr<Scene> s = scenes_[i];
        const QString name = i < names_.size() ? names_[i] : QString();
        auto* it = new QListWidgetItem;
        auto* c = new SceneCard(s, int(i) + 1, name);
        c->setLocked(locked_);
        if (const auto k = thumbs_.constFind(s.get()); k != thumbs_.constEnd()) c->setThumb(*k);
        else
            for (auto l = s->layers.rbegin(); l != s->layers.rend() && !c->hasThumb(); ++l)      // tam thoi, truoc khi render thread ve
                if (l->visible && l->source) c->setThumb(l->source->snapshot(SceneCard::kThumbW));
        c->onPlayPause = [this, s] {
            transport(s, [](Scene& sc, double now) { sceneMediaState(sc).playing ? scenePause(sc) : scenePlay(sc, now); }, true);
        };
        c->onRewind = [this, s] { transport(s, [](Scene& sc, double) { sceneSeek(sc, 0); }, true); };
        c->onSeek = [this, s](double seconds) { transport(s, [seconds](Scene& sc, double) { sceneSeek(sc, seconds); }, true); };
        c->onLoop = [this, s](bool on) { if (setSceneLoop) setSceneLoop(s, on); };
        c->onRename = [this, s](const QString& n) { if (renameScene) renameScene(s, n); };
        c->onDelete = [this, s] { if (deleteScene) deleteScene(s); };
        it->setSizeHint(c->sizeHint());
        list_->addItem(it);
        list_->setItemWidget(it, c);
    }
    list_->doItemsLayout();
    list_->verticalScrollBar()->setValue(scroll);
    program_ = preview_ = -2;                    // setMarks() ke tiep ghi lai nhan
    query();
}

void ScenePanel::setMarks(int program, int preview) {
    if (program == program_ && preview == preview_) return;
    program_ = program;
    preview_ = preview;
    for (int i = 0; i < list_->count(); ++i) {
        const bool pg = i == program, pv = i == preview;
        if (SceneCard* c = card(i)) c->setMark(pg ? SceneCard::Mark::Program : pv ? SceneCard::Mark::Preview : SceneCard::Mark::None);
        list_->item(i)->setText(QString("%1  %2%3").arg(i + 1, 2, 10, QChar('0'))
                                    .arg(i < int(names_.size()) ? names_[size_t(i)] : QString(), pg ? "   ● PGM" : pv ? "   ○ PVW" : ""));
    }
}

void ScenePanel::query() {
    {
        std::lock_guard lk(box_->m);
        if (box_->pending) return;
        box_->pending = true;
    }
    engine_.post([scenes = scenes_, box = box_](double) {
        std::map<const Scene*, Info> out;
        for (const auto& s : scenes) {
            Info i;
            i.media = sceneMediaState(*s);
            for (const Layer& l : s->layers) {
                if (!l.source) continue;
                if (!i.live && dynamic_cast<LiveSource*>(l.source.get())) i.live = l.source;
                if (i.problem.isEmpty()) i.problem = l.source->error();
            }
            out[s.get()] = std::move(i);
        }
        std::lock_guard lk(box->m);
        box->info = std::move(out);
        box->pending = false;
    });
}

void ScenePanel::refresh() {
    const bool seen = isVisible() && !visibleRegion().isEmpty() && !window()->isMinimized();
    engine_.setListThumbChannel(seen ? channel_ : -1);       // khong ai xem: render thread khong ve anh thu nho
    std::map<const Scene*, Info> info;
    {
        std::lock_guard lk(box_->m);
        info = box_->info;
    }
    query();
    const RenderEngine::SceneThumbFrame frame = engine_.listThumbs();
    if (frame.channel == channel_)
        for (size_t i = 0; i < frame.images.size() && i < frame.scenes.size(); ++i)
            if (!frame.images[i].isNull() && frame.scenes[i]) thumbs_[frame.scenes[i]] = frame.images[i];
    for (int row = 0; row < list_->count() && row < int(scenes_.size()); ++row) {
        SceneCard* c = card(row);
        if (!c) continue;
        const Scene* s = scenes_[size_t(row)].get();
        if (const auto k = thumbs_.constFind(s); k != thumbs_.constEnd()) c->setThumb(*k);
        const auto found = info.find(s);
        if (found == info.end()) continue;
        const Info& i = found->second;
        c->setMedia(i.media);
        c->setProblem(i.problem);
        QListWidgetItem* it = list_->item(row);
        if (auto* ls = dynamic_cast<LiveSource*>(i.live.get())) {
            QColor col(colors::kPreview);
            bool hollow = false;
            switch (ls->state()) {
            case LiveState::Live: break;
            case LiveState::Connecting: col = QColor(colors::kWarn); break;
            case LiveState::Lost: col = QColor(colors::kProgram); break;
            case LiveState::Unavailable: col = QColor(0x8a, 0x90, 0xa2); hollow = true; break;
            }
            const QString tip = QString("%1: %2").arg(ls->kind() == "ndi" ? "NDI" : "Camera", ls->statusText());
            c->setLive(col, hollow, tip);
            it->setToolTip(tip);
        } else {
            c->setLive(QColor(), false, i.problem);
            it->setToolTip(i.problem);
        }
    }
}

// Lenh phat/tua: chay tren render thread. Tac dong vao Scene dang len song = nguoi van hanh can thiep -> playlist tam dung.
void ScenePanel::transport(const std::shared_ptr<Scene>& s, std::function<void(Scene&, double)> op, bool intervenes) {
    engine_.post([s, op = std::move(op), channels = project_.channels, intervenes](double now) {
        op(*s, now);
        if (!intervenes) return;
        std::vector<const Scene*> live;
        for (const auto& c : channels) {
            live.clear();
            c->liveScenes(live);
            if (std::find(live.begin(), live.end(), s.get()) != live.end()) c->noteOperatorOp();
        }
    });
}

void ScenePanel::setLocked(bool on) {
    locked_ = on;
    for (QToolButton* b : {importB_, ndiB_, camB_}) b->setEnabled(!on);
    for (int i = 0; i < list_->count(); ++i)
        if (SceneCard* c = card(i)) c->setLocked(on);
}

void ScenePanel::reveal(const Scene* scene) {
    for (int i = 0; i < list_->count() && i < int(scenes_.size()); ++i)
        if (scenes_[size_t(i)].get() == scene) {
            list_->scrollToItem(list_->item(i));
            return;
        }
}

} // namespace lc
