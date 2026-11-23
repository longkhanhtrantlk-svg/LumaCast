#pragma once
#include "core/Types.h"
#include "media/Source.h"
#include <QColor>
#include <memory>
#include <vector>

namespace lc {

struct Layer {
    std::shared_ptr<Source> source;
    QRectF rect{0, 0, 1, 1};      // chuan hoa trong Scene, goc tren-trai
    float opacity = 1.0f;
    LayerFit fit = LayerFit::Fit;
    bool visible = true;
};

struct Scene {
    QString name;
    QSize size{1920, 1080};        // do phan giai thiet ke cua Scene
    QColor background{Qt::black};
    std::vector<Layer> layers;
};

} // namespace lc
