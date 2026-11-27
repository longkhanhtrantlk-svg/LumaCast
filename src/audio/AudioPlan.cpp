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
        plan->channelGain.push_back(chGain);
        for (size_t si = 0; si < c.scenes.size(); ++si) {
            const float sceneGain = c.scenes[si]->audio.gain();          // tat tieng / am luong rieng cua Scene
            for (const Layer& l : c.scenes[si]->layers) {
                if (!l.source) continue;
                add(l.source, ci, l.visible ? w[si] * std::clamp(l.volume, 0.0f, 2.0f) * sceneGain * chGain : 0.0f);
            }
        }
        if (stinger) {
            float own = 1.0f;                                            // theo Scene chi chua dung video stinger (Scene tao luc nhap)
            for (const auto& s : p.scenes)
                if (s->layers.size() == 1 && s->layers[0].source == stinger) { own = s->audio.gain(); break; }
            add(stinger, ci, chGain * own);
        }
    }
    return plan;
}

} // namespace lc
