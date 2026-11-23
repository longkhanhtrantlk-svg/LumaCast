#include "Channel.h"
#include <algorithm>

namespace lc {

void Channel::setPreview(int idx) {
    if (active_ || idx < 0 || idx >= int(scenes.size())) return;
    preview = idx;
}

void Channel::finish() {
    const int p = program, v = preview;
    program = v;
    preview = p;
    active_ = false;
}

void Channel::cut() {
    if (sceneAt(preview)) finish();
}

void Channel::startTransition(TransitionType type, double seconds, double now) {
    if (active_ || !sceneAt(preview)) return;
    if (type == TransitionType::Cut || seconds <= 0 || !sceneAt(program)) { finish(); return; }
    type_ = type; duration_ = seconds; start_ = now; active_ = true;
}

RenderTarget* Channel::render(Compositor& comp, TargetPool& pool, double now) {
    if (active_) {
        double t = (now - start_) / duration_;
        if (t >= 1.0) finish();
        else {
            Scene* a = sceneAt(program);
            Scene* b = sceneAt(preview);
            RenderTarget* ta = pool.acquire(a->size.width(), a->size.height());
            comp.renderScene(*a, *ta);
            RenderTarget* tb = pool.acquire(b->size.width(), b->size.height());
            comp.renderScene(*b, *tb);
            RenderTarget* dst = pool.acquire(size.width(), size.height());
            const float e = float(t * t * (3 - 2 * t));          // ease in-out
            comp.renderTransition(*ta, *tb, type_, e, *dst);
            return dst;
        }
    }
    Scene* p = sceneAt(program);
    if (!p) {                                                     // chua co Program -> den
        RenderTarget* dst = pool.acquire(size.width(), size.height());
        comp.clear(*dst, Qt::black);
        return dst;
    }
    RenderTarget* t = pool.acquire(p->size.width(), p->size.height());
    comp.renderScene(*p, *t);
    return t;
}

} // namespace lc
