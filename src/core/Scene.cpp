#include "Scene.h"
#include <algorithm>

namespace lc {

LayerState evalLayer(const Layer& l, double t) {
    LayerState s;
    double x = l.rect.x(), y = l.rect.y(), w = l.rect.width(), h = l.rect.height();
    double op = l.opacity, rot = l.rotation;
    for (const Track& tr : l.tracks) {
        if (tr.keys.empty()) continue;
        const double v = evalTrack(tr, t);
        switch (tr.prop) {
        case AnimProp::X: x = v; break;
        case AnimProp::Y: y = v; break;
        case AnimProp::W: w = v; break;
        case AnimProp::H: h = v; break;
        case AnimProp::Opacity: op = v; break;
        case AnimProp::Rotation: rot = v; break;
        }
    }
    s.rect = QRectF(x, y, w, h);
    s.opacity = float(std::clamp(op, 0.0, 1.0));
    s.rotation = float(rot);
    return s;
}

} // namespace lc
