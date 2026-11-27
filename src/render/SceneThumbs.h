#pragma once
#include "core/Scene.h"
#include "render/Compositor.h"
#include "render/GlHelpers.h"
#include <QImage>
#include <memory>
#include <vector>

namespace lc {

// Anh thu nho cac Scene cua MOT Channel cho Multiview (UI-06). Chi dung tren render thread.
// Moi lan tick() ve toi da MOT Scene (xoay vong) o do phan giai nho va doc bat dong bo qua PBO, nen khong cho GPU va
// chi them mot phan nho cong viec moi khung. channel < 0 (Multiview dong) = khong ton gi.
class SceneThumbRing {
public:
    static constexpr int kRenderSize = 640;     // canh dai nhat khi ve Scene
    static constexpr int kThumbSize = 320;      // canh dai nhat cua anh doc ve
    static constexpr double kMaxHz = 7.5;       // moi Scene cap nhat nhieu nhat chung nay lan/giay
    static constexpr int kMaxSkip = 30;         // khung bi het ngan sach lien tiep toi da truoc khi van ve 1 Scene

    // busyFrame: khung nay da lam anh monitor -> nhuong. overBudget: khung nay da dung qua nua thoi gian -> nhuong (tru khi cho qua lau).
    // Tra true neu images() thay doi (ke ca vua xoa sach).
    bool tick(Compositor& comp, TargetPool& pool, int channel, const std::vector<std::shared_ptr<Scene>>* scenes,
              double fps, bool busyFrame, bool overBudget);
    int channel() const { return channel_; }
    const std::vector<QImage>& images() const { return images_; }     // cung thu tu voi Channel::scenes; rong = chua ve xong
    void release(GL& gl);                                             // goi truoc khi huy context

private:
    struct Slot {
        std::weak_ptr<Scene> scene;
        std::unique_ptr<AsyncReadback> readback;
    };
    std::vector<Slot> slots_;
    std::vector<QImage> images_;
    int channel_ = -1;
    size_t next_ = 0;
    int wait_ = 0, skipped_ = 0;
};

} // namespace lc
