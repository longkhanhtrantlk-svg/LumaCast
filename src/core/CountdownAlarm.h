#pragma once
#include "audio/Tone.h"
#include "core/Overlay.h"
#include <map>
#include <utility>
#include <vector>

namespace lc {

struct AlarmEvent {
    int channel = 0;
    int overlayId = 0;
    bool stop = false;              // im ngay moi tieng cua dong ho nay
    ToneKind kind = ToneKind::Tick;
    float volume = 0;               // 0..1
};

// Quyet dinh LUC NAO keu: tu trang thai cac dong ho dem nguoc o moi khung hinh. Khong dong toi am thanh that (de kiem thu).
// Chi keu khi dong ho dang chay VA dang hien tren Program; tam dung / dat lai / an / xoa => phat lenh im.
//   con <= alertSeconds: moi giay mot tieng; con <= fastSeconds: cao hon, 2 tieng/giay; 00:00: mot hoi chuong.
class CountdownAlarm {
public:
    // `frames` = OverlayStack::frames() cua Channel `channel`. Su kien moi duoc them vao `out`.
    void update(int channel, const std::vector<OverlayFrame>& frames, std::vector<AlarmEvent>& out);

private:
    struct State {
        bool counting = false;      // khung truoc dang chay va dang hien
        double lastMark = -1;       // moc da keu (giay con lai)
    };
    std::map<std::pair<int, int>, State> state_;      // (channel, overlay id)
};

} // namespace lc
