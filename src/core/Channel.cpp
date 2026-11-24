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
    manual_ = false;
}

void Channel::setManual(const TransitionSpec& spec, float t) {
    if (active_ && !manual_) return;                          // dang chay transition tu dong
    if (!sceneAt(preview) || !sceneAt(program)) return;
    if (t <= 0.0f) { active_ = false; manual_ = false; return; }   // keo ve 0 = huy
    if (t >= 1.0f) { finish(); return; }                           // keo het = hoan tat
    spec_ = spec;
    if (spec_.type == TransitionType::Cut || spec_.type == TransitionType::Stinger) spec_.type = TransitionType::Fade;   // T-bar khong ho tro Stinger
    if (!active_) enterScene(sceneAt(preview), -1);        // vua bat dau keo T-bar
    manualT_ = t;
    manual_ = true;
    active_ = true;
}

void Channel::cut(double now) {
    if (sceneAt(preview)) { enterScene(sceneAt(preview), now); finish(); }
}

void Channel::startTransition(const TransitionSpec& spec, double now) {
    if (active_ || !sceneAt(preview)) return;
    if (spec.type == TransitionType::Cut || spec.duration <= 0 || !sceneAt(program)) { cut(now); return; }
    spec_ = spec;
    if (spec_.type == TransitionType::Stinger) {
        if (!spec_.stinger) spec_.type = TransitionType::Fade;           // thieu video stinger -> Fade
        else spec_.stinger->restart();
    }
    start_ = now;
    active_ = true;
    enterScene(sceneAt(preview), now);            // Scene moi bat dau chay hoat hoa ngay khi bat dau chuyen
}

RenderTarget* Channel::render(Compositor& comp, TargetPool& pool, double now) {
    RenderTarget* out = renderProgram(comp, pool, now);
    float a = ftbAmount;
    if (ftbLast_ >= 0) {
        const float step = float((now - ftbLast_) / std::max(0.05, ftbDuration_));
        a = ftbTarget ? std::min(1.0f, a + step) : std::max(0.0f, a - step);
    } else if (ftbTarget) {
        a = 1.0f;
    }
    ftbLast_ = now;
    ftbAmount = a;
    if (a > 0.0f) comp.fadeToBlack(*out, a);
    return out;
}

RenderTarget* Channel::renderPreview(Compositor& comp, TargetPool& pool) {
    Scene* s = sceneAt(preview);
    if (!s) return nullptr;
    RenderTarget* t = pool.acquire(s->size.width(), s->size.height());
    comp.renderScene(*s, *t);
    return t;
}

RenderTarget* Channel::renderProgram(Compositor& comp, TargetPool& pool, double now) {
    if (active_) {
        double t = manual_ ? double(manualT_) : (now - start_) / spec_.duration;
        if (!manual_ && t >= 1.0) finish();
        else if (spec_.type == TransitionType::Stinger) {
            // Cat canh cung (khong pha tron) o cutPoint, video stinger (co alpha) chay de len tren
            Scene* s = t < spec_.cutPoint ? sceneAt(program) : sceneAt(preview);
            RenderTarget* tg = pool.acquire(s->size.width(), s->size.height());
            comp.renderScene(*s, *tg);
            comp.drawOverlay(*spec_.stinger, *tg);
            return tg;
        } else {
            Scene* a = sceneAt(program);
            Scene* b = sceneAt(preview);
            RenderTarget* ta = pool.acquire(a->size.width(), a->size.height());
            comp.renderScene(*a, *ta);
            RenderTarget* tb = pool.acquire(b->size.width(), b->size.height());
            comp.renderScene(*b, *tb);
            RenderTarget* dst = pool.acquire(size.width(), size.height());
            const float e = manual_ ? float(t) : float(applyEasing(spec_.easing, t));   // T-bar: truc tiep; tu dong: theo easing
            comp.renderTransition(*ta, *tb, spec_, e, *dst);
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
