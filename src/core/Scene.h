#pragma once
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
};

struct Scene {
    QString name;
    QSize size{1920, 1080};        // do phan giai thiet ke cua Scene
    QColor background{Qt::black};
    std::vector<Layer> layers;
};

} // namespace lc
