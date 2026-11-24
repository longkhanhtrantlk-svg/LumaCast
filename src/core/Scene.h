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

struct Scene {
    QString name;
    QSize size{1920, 1080};        // do phan giai thiet ke cua Scene
    QColor background{Qt::black};
    std::vector<Layer> layers;
    double duration = 10.0;                  // giay; thoi luong dong ho cho keyframe
    bool loop = true;
    bool restartOnAir = true;                // phat lai tu dau moi khi Scene len Program (hoac bat dau chuyen vao)
    SceneClock clock;
};

} // namespace lc
