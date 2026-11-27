#pragma once
#include "core/Overlay.h"
#include "core/Scene.h"
#include "core/Transition.h"
#include "core/Playlist.h"
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

    // Lop phu (SRC-08): ve de len Program, giu nguyen khi doi Scene. Tu an toan giua cac thread (khong can post()).
    OverlayStack overlays;

    bool inTransition() const { return active_; }
    // Them vao `out` cac Scene dang hien tren Program (ke ca Scene dang chuyen vao). Render thread.
    void liveScenes(std::vector<const Scene*>& out) const {
        if (const Scene* p = sceneAt(program)) out.push_back(p);
        if (active_) if (const Scene* in = sceneAt(incomingIndex())) out.push_back(in);
    }

    // Playlist (CHN-10). startAuto: chuyen tu Program sang mot Scene bat ky cua Channel KHONG dung toi Preview (xong: program = dich, preview giu nguyen).
    // Tra false neu dang chuyen canh hoac Scene khong thuoc Channel. Render thread.
    Playlist playlist;
    bool startAuto(const Scene* target, const TransitionSpec& spec, double now);
    bool autoTransition() const { return auto_.load() != nullptr; }
    bool isIncoming(const Scene* s) const { return s && auto_.load() == s; }     // dang duoc playlist chuyen vao
    // Scene dang o Program, hoac Scene playlist dang chuyen vao (nullptr neu khong co).
    const Scene* programScene() const { const Scene* a = auto_.load(); return a ? a : sceneAt(program); }
    bool showing(const Scene* s) const { return s && (!active_ || autoTransition()) && programScene() == s; }
    // Dem so lan NGUOI VAN HANH tac dong Program (CUT, TAKE, T-bar, ke ca qua dieu khien ngoai) - playlist dung de tu tam dung.
    uint32_t operatorOps() const { return ops_; }
    void noteOperatorOp() { ++ops_; }                // vd. tam dung/tua video dang o Program tu danh sach Scenes
    // Dau moi khung (render() tu goi): ket thuc transition tu dong va chay playlist. Khong can GL.
    void advance(double now);

    // T-bar: dieu khien tien do chuyen bang tay (0..1). t<=0 huy (khong doi scene), t>=1 hoan tat (Program <-> Preview).
    // Bi bo qua khi dang chay transition tu dong. Render thread (qua post).
    void setManual(const TransitionSpec& spec, float t);
    void setManual(TransitionType type, float t) { TransitionSpec s; s.type = type; setManual(s, t); }
    bool manualActive() const { return manual_; }
    void setPreview(int idx);
    void cut(double now = -1);                     // now: dung de phat lai Scene moi len Program (restartOnAir)
    void startTransition(const TransitionSpec& spec, double now);
    void startTransition(TransitionType type, double seconds, double now) {
        TransitionSpec s; s.type = type; s.duration = seconds; startTransition(s, now);
    }

    // Am thanh cua Channel (bus rieng). Doi qua RenderEngine::post(); mixer chi thay qua ke hoach tron do render thread dung moi khung.
    std::atomic<float> volume{1.0f};               // 0..2
    std::atomic<bool> mute{false};
    // Trong so am thanh cua tung Scene o thoi diem `now` (render thread, sau render()): Program/Preview chuyen theo DUNG tien do
    // transition (ca T-bar). w.size() == scenes.size(); tong = 1 (Fade/Wipe/Plugin: noi tuyen tinh theo easing; Dip: Program xuong 0 o
    // nua dau, Preview len o nua sau; Stinger: cat cung o cutPoint). stinger = nguon stinger dang chay (nullptr neu khong).
    void audioWeights(double now, std::vector<float>& w, std::shared_ptr<Source>& stinger) const;

    // Fade-To-Black: dua toan bo Program ve den (bam lan nua de tro lai). Render thread (qua post).
    std::atomic<bool> ftbTarget{false};
    std::atomic<float> ftbAmount{0.0f};      // 0 = binh thuong, 1 = den hoan toan (de UI hien thi)
    void toggleFtb(double seconds) { ftbDuration_ = seconds; ftbTarget = !ftbTarget; }

    // Render thread. Tra ve texture Program cua khung hinh nay (hop le den het khung).
    RenderTarget* render(Compositor& comp, TargetPool& pool, double now);
    // Texture cua scene dang o Preview (nullptr neu khong co) -- cho monitor.
    RenderTarget* renderPreview(Compositor& comp, TargetPool& pool);

private:
    void enterScene(Scene* s, double now) { if (s && s->restartOnAir) s->clock.restart(now); }
    Scene* sceneAt(int i) const { return i >= 0 && i < int(scenes.size()) ? scenes[i].get() : nullptr; }
    void finish();
    int indexOf(const Scene* s) const { for (size_t i = 0; i < scenes.size(); ++i) if (scenes[i].get() == s) return int(i); return -1; }
    int incomingIndex() const { const Scene* a = auto_.load(); return a ? indexOf(a) : preview.load(); }   // dich cua transition dang chay
    void abortAuto() { if (auto_.load()) { auto_ = nullptr; active_ = false; } }
    std::atomic<const Scene*> auto_{nullptr};        // dich cua transition do playlist khoi xuong
    std::atomic<uint32_t> ops_{0};
    RenderTarget* renderProgram(Compositor& comp, TargetPool& pool, double now);
    double ftbDuration_ = 1.0, ftbLast_ = -1.0;

    bool active_ = false;
    std::atomic<bool> manual_{false};
    float manualT_ = 0;
    TransitionSpec spec_;
    double start_ = 0;
};

} // namespace lc
