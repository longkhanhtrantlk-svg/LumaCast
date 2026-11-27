#pragma once
#include "core/Animation.h"
#include "core/Types.h"
#include "media/Source.h"
#include <QColor>
#include <QVariantMap>
#include <memory>
#include <vector>

namespace lc {

// Mot Effect gan len Layer: id (dung san hoac plug-in) + gia tri tham so. Giu nguyen ca khi plug-in bi thieu (de khong mat du lieu project).
struct EffectInstance {
    QString effectId;
    bool enabled = true;
    QVariantMap params;
};

struct Layer {
    std::shared_ptr<Source> source;
    QRectF rect{0, 0, 1, 1};      // chuan hoa trong Scene, goc tren-trai
    float opacity = 1.0f;
    LayerFit fit = LayerFit::Fit;
    bool visible = true;
    std::vector<EffectInstance> effects;     // ap dung theo thu tu, truoc khi ve vao Scene
    float rotation = 0;                      // do, theo chieu kim dong ho, quanh tam layer
    QRectF crop{0, 0, 1, 1};                 // vung cua nguon duoc dung, chuan hoa, goc tren-trai, THEO HINH DA XOAY (video dien thoai)
    BlendMode blend = BlendMode::Normal;
    float volume = 1.0f;                     // am luong cua nguon trong Layer nay (0..2; 1 = nguyen goc). Chi co tac dung neu nguon co am thanh
    std::vector<Track> tracks;               // keyframe theo thoi gian Scene; thuoc tinh co track se bi track ghi de

    const Track* track(AnimProp p) const { for (const auto& t : tracks) if (t.prop == p) return &t; return nullptr; }
    Track* track(AnimProp p) { for (auto& t : tracks) if (t.prop == p) return &t; return nullptr; }
};

// Trang thai hieu dung cua layer tai thoi diem t (gia tri tinh + track).
struct LayerState {
    QRectF rect;
    float opacity = 1;
    float rotation = 0;
};
LayerState evalLayer(const Layer& l, double t);

// Am thanh rieng cua Scene (danh sach Scenes): nhan them vao am luong tung Layer. Nguon chi ra loa khi Scene o Program
// (hoac dang chuyen vao). Atomic: UI doi truc tiep, render thread doc khi dung ke hoach tron.
struct SceneAudio {
    std::atomic<float> volume{1.0f};   // 0..2 (1 = nguyen goc)
    std::atomic<bool> mute{false};
    SceneAudio() = default;
    SceneAudio(const SceneAudio& o) : volume(o.volume.load()), mute(o.mute.load()) {}
    SceneAudio& operator=(const SceneAudio& o) { volume = o.volume.load(); mute = o.mute.load(); return *this; }
    float gain() const { const float v = volume.load(); return mute.load() ? 0.0f : v < 0 ? 0.0f : v > 2 ? 2.0f : v; }
};

struct Scene {
    QString name;
    QSize size{1920, 1080};        // do phan giai thiet ke cua Scene
    QColor background{Qt::black};
    std::vector<Layer> layers;
    double duration = 10.0;                  // giay; thoi luong dong ho cho keyframe
    bool loop = true;
    bool restartOnAir = true;                // phat lai tu dau moi khi Scene len Program (hoac bat dau chuyen vao)
    SceneClock clock;
    SceneAudio audio;
};

// ---- Phat/tam dung/tua mot Scene (danh sach Scenes) ----
// Scene co video: tac dong len moi video trong Scene (video dung chung voi Scene khac cung doi theo - mot bo giai ma),
// kem dong ho keyframe neu Scene co keyframe. Scene chi co keyframe: dong ho keyframe. Goi tren render thread.
struct SceneMediaState {
    bool video = false;          // co nguon dieu khien phat duoc
    bool audio = false;          // co nguon co tieng
    bool keyframes = false;
    bool playing = false;
    bool loop = true;
    double position = 0;         // giay (theo video dai nhat; khong co video: dong ho keyframe)
    double duration = 0;         // 0 = khong co gi de tua
    bool canTransport() const { return video || keyframes; }
};
SceneMediaState sceneMediaState(const Scene& s);
void scenePlay(Scene& s, double now);
void scenePause(Scene& s);
void sceneSeek(Scene& s, double seconds);

} // namespace lc
