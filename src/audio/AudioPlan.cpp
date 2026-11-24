#include "AudioPlan.h"
#include <algorithm>

namespace lc {

std::shared_ptr<AudioPlan> buildAudioPlan(const Project& p, double now) {
    auto plan = std::make_shared<AudioPlan>();
    plan->channels = int(std::min<size_t>(p.channels.size(), kMaxAudioChannels));
    std::vector<float> w;
    std::shared_ptr<Source> stinger;
    auto add = [&](const std::shared_ptr<Source>& src, int ch, float g) {
        for (AudioPlanEntry& e : plan->entries)
            if (e.src == src && e.channel == ch) { e.gain += g; return; }       // cung nguon xuat hien nhieu lan: cong he so
        plan->entries.push_back({src, ch, g});
    };
    for (int ci = 0; ci < plan->channels; ++ci) {
        const Channel& c = *p.channels[size_t(ci)];
        c.audioWeights(now, w, stinger);
        const float chGain = c.mute.load() ? 0.0f : std::clamp(c.volume.load(), 0.0f, 2.0f) * (1.0f - std::clamp(c.ftbAmount.load(), 0.0f, 1.0f));
        for (size_t si = 0; si < c.scenes.size(); ++si)
            for (const Layer& l : c.scenes[si]->layers) {
                if (!l.source) continue;
                add(l.source, ci, l.visible ? w[si] * std::clamp(l.volume, 0.0f, 2.0f) * chGain : 0.0f);
            }
        if (stinger) add(stinger, ci, chGain);
    }
    return plan;
}

} // namespace lc
