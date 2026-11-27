#pragma once
#include "audio/AudioMixer.h"
#include "core/Project.h"
#include "render/EffectRegistry.h"
#include "render/FrameStats.h"
#include "render/OutputSink.h"
#include <QImage>
#include <QOffscreenSurface>
#include <QThread>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>

namespace lc {

// Mot render thread duy nhat cho moi Output -> cac man LED ghep khong bi lech khung.
class RenderEngine : public QThread {
    Q_OBJECT
public:
    explicit RenderEngine(Project& project, QObject* parent = nullptr);
    ~RenderEngine() override;

    void startEngine();        // thread chinh: tao + hien cua so Output, roi chay render thread
    void stopEngine();
    // Gui lenh cho render thread (chay o dau khung hinh ke tiep). fn nhan thoi gian hien tai (giay).
    void post(std::function<void(double)> fn);
    // Nhu post() nhung cho (toi da timeoutMs) cho den khi render thread chay xong lenh. Dung cho thao tac hiem (Hoan tac/Lam lai nhap media)
    // can du lieu da doi truoc khi UI cap nhat tiep. Khong chay thread -> chay ngay. Tra false neu het thoi gian (lenh van se chay sau).
    bool postAndWait(std::function<void(double)> fn, int timeoutMs = 2000);
    double now() const;
    double fps() const { return fps_; }
    int outputCount() const { std::lock_guard lk(sinksMu_); return int(sinks_.size()); }
    OutputSink* output(int i) const { std::lock_guard lk(sinksMu_); return i >= 0 && i < int(sinks_.size()) ? sinks_[size_t(i)].get() : nullptr; }
    // Them Output NDI luc dang chay (M5b). Chi ho tro OutputKind::Ndi (cua so man hinh phai co luc khoi dong). Goi tu UI thread; cho render thread
    // xong roi moi tra ve, nen outputCount()/output(i) thay ngay Output moi. Tra chi so Output moi, hoac -1 neu loi.
    int addNdiOutput(const OutputConfig& cfg);
    // Doi ca danh sach Output luc dang chay (thiet lap man hinh). Goi tu UI thread. Cua so cu cung che do + cung man hinh duoc giu,
    // khong chop; con lai tao moi / an di. Den/Dung/Mau giu theo vi tri (dang BLACK ALL thi cua so moi cung den). `sameFrame`
    // (tuy chon) chay tren render thread trong CUNG khung hinh voi viec doi (kich thuoc canvas/Scene). false = render thread khong
    // phan hoi kip (thay doi van se ap dung sau).
    bool applyOutputs(const std::vector<OutputConfig>& cfgs, std::function<void()> sameFrame = {});
    QString error() const;
    QString gpuName() const;
    uint64_t droppedFrames() const { return dropped_; }
    // Thoi gian render moi khung (ms), cap nhat moi giay tren cua so ~300 khung gan nhat.
    double frameP50() const { return p50_; }
    double frameP99() const { return p99_; }
    double frameMax() const { return maxMs_; }
    // Thoi gian tung giai doan (upload / Scene / transition / monitor / Output), cap nhat moi giay. setProfileSync(true): glFinish cuoi moi giai doan de do
    // thoi gian GPU that (cham hon, chi dung khi do) - mac dinh chi do thoi gian CPU phat lenh.
    StageReport stages() const;
    void setProfileSync(bool on);
    // Them Output tuy y (benchmark, loai dich den moi) luc dang chay; sink do ben ngoai khoi tao. Tra chi so Output, -1 neu loi/het thoi gian.
    int addSink(std::unique_ptr<OutputSink> sink);
    // Ghi lai moi khung (ms) de tinh phan vi chinh xac tren ca doan do (benchmark). takeRecording() dung ghi, tra du lieu va xoa.
    struct FrameSample { float total = 0, upload = 0, scene = 0, transition = 0, monitor = 0, output = 0; };
    void startRecording();
    std::vector<FrameSample> takeRecording();
    // Watchdog: render thread khong bao nhip (heartbeat) qua nguong -> treo. Man hinh giu hinh cuoi (khong swap).
    bool stalled() const;
    void setStallThresholdMs(int ms) { stallMs_ = ms; }
    // Anh monitor (cap nhat ~15 lan/giay) cua Program/Preview cua mot Channel.
    struct MonitorFrame { QImage program, preview; };
    MonitorFrame monitor(int channel) const;
    // Anh thu nho tung Scene cua MOT Channel cho Multiview (UI-06). -1 = tat (mac dinh): render thread khong lam them gi.
    // Moi khung ve toi da mot Scene, SAU khi da dua hinh ra Output; anh theo thu tu Channel::scenes, rong = chua ve xong.
    struct SceneThumbFrame { int channel = -1; std::vector<QImage> images; std::vector<const Scene*> scenes; };
    void setSceneThumbChannel(int channel) { sceneThumbChannel_ = channel; }
    SceneThumbFrame sceneThumbs() const;              // cho Multiview; rong khi Multiview khong mo
    // Anh thu nho cho danh sach Scenes cua cua so chinh: cung mot bo ve voi Multiview (Multiview mo thi theo kenh va nhip cua no),
    // rieng danh sach thi cap nhat thua hon (SceneThumbRing::kListHz).
    void setListThumbChannel(int channel) { listThumbChannel_ = channel; }
    SceneThumbFrame listThumbs() const;
    // Render thread (goi trong post): chep cau hinh Output hien tai ve Project de luu file.
    void syncOutputsToProject();
    // Mixer am thanh (M5a). Engine dung ke hoach tron moi khung (render thread); thiet bi do ben ngoai mo bang audio().start(id).
    AudioMixer& audio() { return audio_; }
    // Kho Effect/Transition dung chung (UI nap plug-in vao day; render thread bien dich + dung).
    EffectRegistry& registry() { return *registry_; }

protected:
    void run() override;

private:
    void rewindVideoGoingOnAir();                   // render thread, dau moi khung: video VUA len Program phat lai tu dau
    void syncVideoLoop(const std::vector<const Scene*>& live);       // render thread: o "Lap" cua Scene -> bo giai ma video
    std::vector<const Source*> onAir_;              // nguon dang o Program o khung truoc
    Project& project_;
    // Sink khong bao gio bi huy khi dang chay (con tro Output ben vung cho den stopEngine): sink bi go chuyen sang graveyard_.
    std::vector<std::unique_ptr<OutputSink>> sinks_;      // sinksMu_ bao ve vector
    std::vector<std::unique_ptr<OutputSink>> retiring_;   // render thread: cho giai phong tai nguyen GL
    std::vector<std::unique_ptr<OutputSink>> graveyard_;  // da giai phong; huy luc stopEngine
    std::unique_ptr<OutputSink> makeSink(const OutputConfig& cfg, int index);
    int winX_ = 0, winY_ = 0, winRowH_ = 0;                // vi tri xep cua so thu nghiem (UI thread)
    void placeWindow(class OutputWindow& w, const OutputConfig& cfg);
    mutable std::mutex sinksMu_;
    std::unique_ptr<QOffscreenSurface> surface_;
    AudioMixer audio_;
    std::shared_ptr<AudioPlan> lastPlan_;
    EffectRegistry ownRegistry_;
    EffectRegistry* registry_ = &ownRegistry_;
    std::mutex mutex_;
    std::vector<std::function<void(double)>> commands_;
    std::atomic<double> fps_{0};
    std::atomic<bool> stop_{false};
    QString error_, gpu_;
    std::atomic<uint64_t> dropped_{0};
    std::atomic<double> p50_{0}, p99_{0}, maxMs_{0};
    std::mutex recMu_;
    std::vector<FrameSample> rec_;
    std::atomic<bool> recording_{false};
    mutable std::mutex stageMu_;
    StageReport stages_;
    std::atomic<int64_t> heartbeatNs_{0};
    std::atomic<int> stallMs_{2000};
    mutable std::mutex monitorMutex_;
    std::vector<MonitorFrame> monitors_;
    SceneThumbFrame sceneThumbs_;                      // monitorMutex_
    std::atomic<int> sceneThumbChannel_{-1};
    std::atomic<int> listThumbChannel_{-1};
    qint64 t0_ = 0;
};

} // namespace lc
