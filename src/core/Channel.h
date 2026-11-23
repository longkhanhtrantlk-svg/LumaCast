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

    // Fade-To-Black: dua toan bo Program ve den (bam lan nua de tro lai). Render thread (qua post).
    std::atomic<bool> ftbTarget{false};
    std::atomic<float> ftbAmount{0.0f};      // 0 = binh thuong, 1 = den hoan toan (de UI hien thi)
    void toggleFtb(double seconds) { ftbDuration_ = seconds; ftbTarget = !ftbTarget; }

    // Render thread. Tra ve texture Program cua khung hinh nay (hop le den het khung).
    RenderTarget* render(Compositor& comp, TargetPool& pool, double now);
    // Texture cua scene dang o Preview (nullptr neu khong co) -- cho monitor.
    RenderTarget* renderPreview(Compositor& comp, TargetPool& pool);

private:
    Scene* sceneAt(int i) const { return i >= 0 && i < int(scenes.size()) ? scenes[i].get() : nullptr; }
    void finish();
    RenderTarget* renderProgram(Compositor& comp, TargetPool& pool, double now);
    double ftbDuration_ = 1.0, ftbLast_ = -1.0;

    bool active_ = false;
    TransitionType type_ = TransitionType::Fade;
    double start_ = 0, duration_ = 1;
};

} // namespace lc
