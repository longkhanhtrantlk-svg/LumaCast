#pragma once
#include "audio/AudioMixer.h"
#include "core/Project.h"

namespace lc {

// Dung ke hoach tron tu trang thai hien tai cua Project (RENDER THREAD, sau khi cac Channel da render trong khung nay).
// Moi Channel: he so nguon = trong so Scene (theo transition) x am luong Layer x am luong Channel x (khong mute) x (1 - Fade-To-Black).
// MOI nguon cua MOI Scene cua Channel deu co mat (he so 0 neu khong len song) de mixer rut deu va dong ho am thanh cua no chay dung.
std::shared_ptr<AudioPlan> buildAudioPlan(const Project& p, double now);

} // namespace lc
