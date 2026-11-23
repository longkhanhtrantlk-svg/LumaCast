#pragma once
#include "core/Scene.h"
#include "render/Compositor.h"
#include <atomic>

namespace lc {

// Mot duong Program/Preview doc lap. Moi thay doi sau khi engine chay phai di qua RenderEngine::post().
// Sau khi chuyen xong, Program va Preview HOAN DOI (kieu switcher): bam Cut lien tuc se bat qua lai 2 scene.
class Channel {
public:
    QString name;
    QSize size{1920, 1080};                         // do phan giai Channel khi dang co transition
    std::vector<std::shared_ptr<Scene>> scenes;
    std::atomic<int> program{-1};
    std::atomic<int> preview{-1};

    bool inTransition() const { return active_; }
    void setPreview(int idx);
    void cut();
    void startTransition(TransitionType type, double seconds, double now);

    // Render thread. Tra ve texture Program cua khung hinh nay (hop le den het khung).
    RenderTarget* render(Compositor& comp, TargetPool& pool, double now);

private:
    Scene* sceneAt(int i) const { return i >= 0 && i < int(scenes.size()) ? scenes[i].get() : nullptr; }
    void finish();

    bool active_ = false;
    TransitionType type_ = TransitionType::Fade;
    double start_ = 0, duration_ = 1;
};

} // namespace lc
