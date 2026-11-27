#include "CountdownAlarm.h"
#include <algorithm>
#include <cmath>

namespace lc {

namespace {
// Khung hinh tre qua muc nay so voi moc thi bo moc do (khong keu don sau khi bi khung, hoac khi vua hien giua chung).
constexpr double kLate = 0.25;
}

void CountdownAlarm::update(int channel, const std::vector<OverlayFrame>& frames, std::vector<AlarmEvent>& out) {
    std::vector<int> seen;
    auto stop = [&](int id) { out.push_back({channel, id, true, ToneKind::Tick, 0}); };
    for (const OverlayFrame& f : frames) {
        const OverlayDef& d = f.def;
        if (d.type != OverlayType::Countdown) continue;
        const std::pair<int, int> key{channel, d.id};
        auto it = state_.find(key);
        const bool wanted = d.alert && f.on && (f.running || f.finished);
        if (!wanted) {                                      // tat nhac / an / tam dung / dat lai
            if (it != state_.end()) { stop(d.id); state_.erase(it); }
            continue;
        }
        seen.push_back(d.id);
        const float volume = float(std::clamp(d.alertVolume, 0, 100)) / 100.0f;
        if (f.finished) {
            // chuong chi khi dong ho VUA chay het trong luc dang hien (hien mot dong ho da het gio thi im)
            if (it != state_.end() && it->second.counting) {
                it->second.counting = false;
                out.push_back({channel, d.id, false, ToneKind::Bell, volume});
            }
            continue;
        }
        if (it != state_.end() && !it->second.counting) { stop(d.id); it->second.lastMark = -1; }      // chay lai sau khi het gio
        State& st = state_[key];
        st.counting = true;
        const double r = f.remaining;
        if (r > d.alertSeconds + 1e-6) { st.lastMark = -1; continue; }
        const bool fast = d.fastSeconds > 0 && r <= d.fastSeconds + 1e-6;
        const double mark = fast ? std::ceil(r * 2.0 - 1e-6) / 2.0 : std::ceil(r - 1e-6);    // moc vua di qua
        if (mark <= 0 || mark == st.lastMark || mark - r >= kLate) continue;
        st.lastMark = mark;
        out.push_back({channel, d.id, false, fast ? ToneKind::Fast : ToneKind::Tick, volume});
    }
    for (auto it = state_.begin(); it != state_.end();) {   // dong ho bi xoa
        if (it->first.first == channel && std::find(seen.begin(), seen.end(), it->first.second) == seen.end()) {
            stop(it->first.second);
            it = state_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace lc
