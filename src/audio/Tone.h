#pragma once
#include "audio/AudioStream.h"

namespace lc {

// Tieng nhac tu tong hop (khong can file): "tit", "tit" cao (doan gap) va chuong dai luc het gio.
enum class ToneKind { Tick, Fast, Bell };

// Do dai (khung o kAudioRate) va mau thu `frame` (mono, bien do -1..1) cua mot tieng.
int toneFrames(ToneKind kind);
float toneSample(ToneKind kind, int frame);

} // namespace lc
