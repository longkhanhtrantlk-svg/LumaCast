// Bu khe video wall (OUT-04): phan cua InspectorPanel cho khung "Video wall" o dau tab Output.
#include "InspectorPanel.h"
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QUndoCommand>
#include <QUndoStack>
#include <algorithm>

namespace lc {

namespace {

class WallCmd : public QUndoCommand {
public:
    WallCmd(InspectorPanel* p, int channel, InspectorPanel::WallState before, InspectorPanel::WallState after, int axis)
        : QUndoCommand("Đổi khe video wall"), panel_(p), channel_(channel), before_(std::move(before)), after_(std::move(after)),
          mergeId_(4000000 + channel * 2 + axis) {}
    int id() const override { return mergeId_; }
    void undo() override { panel_->applyWall(channel_, before_); }
    void redo() override { if (first_) { first_ = false; return; } panel_->applyWall(channel_, after_); }
    bool mergeWith(const QUndoCommand* other) override {
        if (other->id() != id()) return false;
        after_ = static_cast<const WallCmd*>(other)->after_;
        if (after_.canvas == before_.canvas) setObsolete(true);       // tra ve khe cu: khong de lai buoc Hoan tac rong
        return true;
    }
private:
    InspectorPanel* panel_;
    int channel_;
    InspectorPanel::WallState before_, after_;
    int mergeId_;
    bool first_ = true;
};

} // namespace

void InspectorPanel::addWallBoxes(QVBoxLayout* root) {
    for (const auto& c : project_.channels) canvasCache_.push_back(c->size);
    int at = 0;
    for (int ch = 0; ch < int(project_.channels.size()); ++ch) {
        const auto screens = std::count_if(outCache_.begin(), outCache_.end(), [ch](const OutputConfig& o) {
            return o.channel == ch && o.kind == OutputKind::Screen;
        });
        if (screens < 2) continue;
        WallW w;
        w.channel = ch;
        w.box = new QGroupBox;
        w.box->setObjectName("wallBox");
        auto* g = new QGridLayout(w.box);
        const char* prefix[2] = {"khe ngang ", "khe dọc "};
        const char* name[2] = {"wallGapX", "wallGapY"};
        for (int axis = 0; axis < 2; ++axis) {
            auto* s = new QSpinBox;
            s->setObjectName(name[axis]);
            s->setPrefix(prefix[axis]);
            s->setSuffix(" px");
            s->setKeyboardTracking(false);                // go "40" khong ap dung "4" truoc (moi lan ap dung la doi co canvas)
            s->setToolTip("Số điểm ảnh bị che sau viền/khe giữa hai màn kề nhau. Canvas và Scene rộng thêm phần này, mỗi Output vẫn 1:1.");
            g->addWidget(s, 0, axis);
            w.gap[axis] = s;
        }
        w.info = new QLabel;
        w.info->setObjectName("wallInfo");
        w.info->setWordWrap(true);
        g->addWidget(w.info, 1, 0, 1, 2);
        w.box->setVisible(false);
        root->insertWidget(at++, w.box);
        wallW_.push_back(w);
        for (int axis = 0; axis < 2; ++axis)
            connect(w.gap[axis], &QSpinBox::valueChanged, this, [this, ch, axis, x = w.gap[0], y = w.gap[1]] {
                setWallGap(ch, QSize(x->value(), y->value()), axis);
            });
    }
    syncWallBoxes();
}

void InspectorPanel::syncWallBoxes() {
    for (WallW& w : wallW_) {
        WallLayout wall;
        const QSize canvas = canvasCache_[size_t(w.channel)];
        if (!detectWall(outCache_, w.channel, canvas, wall)) {
            // Chua tung la wall: an. Da la wall roi bi sua crop lech luoi: giu khung, khoa o nhap va giai thich.
            for (QSpinBox* s : w.gap) s->setEnabled(false);
            w.info->setText("Vùng crop không còn khớp lưới wall nên không bù khe được. Hoàn tác (Ctrl+Z) hoặc sửa crop về đúng lưới.");
            continue;
        }
        w.box->setVisible(true);
        w.box->setTitle(QString("Video wall %1×%2, ô %3×%4 (kênh %5)").arg(wall.cols).arg(wall.rows)
                            .arg(wall.cell.width()).arg(wall.cell.height()).arg(w.channel + 1));
        const QSize max = wall.maxGap();
        const int value[2] = {wall.gap.width(), wall.gap.height()}, limit[2] = {max.width(), max.height()}, count[2] = {wall.cols, wall.rows};
        for (int axis = 0; axis < 2; ++axis) {
            QSignalBlocker b(w.gap[axis]);
            w.gap[axis]->setRange(0, std::max(limit[axis], value[axis]));
            w.gap[axis]->setValue(value[axis]);
            w.gap[axis]->setEnabled(count[axis] > 1);
        }
        QString info = QString("Canvas %1×%2").arg(canvas.width()).arg(canvas.height());
        for (const auto& c : wall.cells)
            if (outCache_[size_t(c.output)].size != wall.cell) { info += ". Lưu ý: có Output khác cỡ ô nên không đúng 1:1."; break; }
        w.info->setText(info);
    }
}

void InspectorPanel::setWallGap(int channel, QSize gap, int axis) {
    if (locked_ || channel < 0 || channel >= int(canvasCache_.size())) return;          // khoa van hanh
    const QSize oldCanvas = canvasCache_[size_t(channel)];
    WallLayout wall;
    if (!detectWall(outCache_, channel, oldCanvas, wall)) return;
    gap = gap.expandedTo(QSize(0, 0)).boundedTo(wall.maxGap());
    if (gap == wall.gap) return;

    WallState before{oldCanvas, {}, {}}, after{wallCanvas(wall.cols, wall.rows, wall.cell, gap), {}, {}};
    for (const auto& c : wall.cells) {
        before.crops.push_back({c.output, outCache_[size_t(c.output)].crop});
        after.crops.push_back({c.output, wallCrop(c.col, c.row, wall.cols, wall.rows, wall.cell, gap)});
    }
    // Scene ve dung co canvas thi doi theo (giu 1:1); Scene co do phan giai rieng giu nguyen.
    for (const auto& s : project_.channels[size_t(channel)]->scenes) {
        if (s->size != oldCanvas) continue;
        if (std::any_of(before.scenes.begin(), before.scenes.end(), [&](const auto& e) { return e.first == s; })) continue;
        before.scenes.push_back({s, oldCanvas});
        after.scenes.push_back({s, after.canvas});
    }
    applyWall(channel, after);
    undo_->push(new WallCmd(this, channel, std::move(before), std::move(after), axis));
}

void InspectorPanel::applyWall(int channel, const WallState& state) {
    if (channel < 0 || channel >= int(canvasCache_.size())) return;
    canvasCache_[size_t(channel)] = state.canvas;
    std::vector<std::pair<OutputSink*, OutputConfig>> configs;
    for (const auto& [i, crop] : state.crops) {               // cap nhat het ban sao truoc de khong co trang thai nua voi
        outCache_[size_t(i)].crop = crop;
        configs.push_back({engine_.output(i), outCache_[size_t(i)]});
    }
    // Canvas, Scene va crop phai doi trong CUNG mot khung hinh, khong thi man hinh nhay mot khung sai ti le.
    auto chan = project_.channels[size_t(channel)];
    engine_.postAndWait([chan, canvas = state.canvas, scenes = state.scenes, configs](double) {
        chan->size = canvas;
        for (const auto& [scene, size] : scenes) scene->size = size;
        for (const auto& [sink, cfg] : configs) sink->mutableConfig() = cfg;
    });
    for (const auto& e : state.crops) applyOutput(e.first, OutputConfig(outCache_[size_t(e.first)]));    // o nhap tung Output
    for (int i = 0; i < sceneCombo_->count() && i < int(project_.scenes.size()); ++i) {
        const auto& s = project_.scenes[size_t(i)];
        sceneCombo_->setItemText(i, QString("%1  (%2x%3)").arg(s->name).arg(s->size.width()).arg(s->size.height()));
    }
}

} // namespace lc
