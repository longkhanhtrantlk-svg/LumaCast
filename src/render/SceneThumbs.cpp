#include "SceneThumbs.h"
#include <algorithm>
#include <cmath>

namespace lc {

static QSize bounded(QSize s, int side) {
    if (s.width() <= side && s.height() <= side) return s.expandedTo(QSize(1, 1));
    return s.scaled(side, side, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
}

void SceneThumbRing::release(GL& gl) {
    for (Slot& s : slots_)
        if (s.readback) s.readback->release(gl);
    slots_.clear();
    images_.clear();
    channel_ = -1;
    next_ = 0;
    wait_ = skipped_ = 0;
}

bool SceneThumbRing::tick(Compositor& comp, TargetPool& pool, int channel, const std::vector<std::shared_ptr<Scene>>* scenes,
                          double fps, bool busyFrame, bool overBudget, double maxHz) {
    bool changed = false;
    if (channel < 0 || !scenes) {
        changed = channel_ >= 0;
        if (changed) release(comp.gl());
        return changed;
    }
    // Doi kenh hoac danh sach Scene (nhap media, Hoan tac...): bo anh cu de khong hien nham o.
    bool same = channel == channel_ && scenes->size() == slots_.size();
    for (size_t i = 0; same && i < slots_.size(); ++i) same = slots_[i].scene.lock() == (*scenes)[i];
    if (!same) {
        release(comp.gl());
        channel_ = channel;
        slots_.resize(scenes->size());
        images_.resize(scenes->size());
        for (size_t i = 0; i < slots_.size(); ++i) slots_[i].scene = (*scenes)[i];
        changed = true;
    }
    if (slots_.empty()) return changed;

    if (wait_ > 0) --wait_;
    if (wait_ > 0 || busyFrame) return changed;
    if (overBudget && skipped_ < kMaxSkip) { ++skipped_; return changed; }
    skipped_ = 0;
    wait_ = std::max(1, int(std::lround(fps / (maxHz * double(slots_.size())))));

    const size_t i = next_ % slots_.size();
    next_ = i + 1;
    const Scene* scene = (*scenes)[i].get();
    if (!scene) return changed;

    const QSize big = bounded(scene->size, kRenderSize);
    const QSize small = bounded(big, kThumbSize);
    RenderTarget* src = pool.acquire(big.width(), big.height());
    comp.renderScene(*scene, *src);
    RenderTarget* dst = pool.acquire(small.width(), small.height());
    GL& gl = comp.gl();
    gl.glBindFramebuffer(GL_FRAMEBUFFER, dst->fbo);
    OutputConfig cfg;
    cfg.scale = ScaleMode::Stretch;
    comp.drawToCurrent(src->tex, src->w, src->h, cfg, small.width(), small.height());
    if (!slots_[i].readback) slots_[i].readback = std::make_unique<AsyncReadback>();
    const QImage img = slots_[i].readback->submit(gl, small.width(), small.height());   // anh cua LAN TRUOC cung o
    if (!img.isNull()) { images_[i] = img; changed = true; }
    return changed;
}

} // namespace lc
