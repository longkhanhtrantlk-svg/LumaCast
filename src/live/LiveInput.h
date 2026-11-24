#pragma once
#include <QString>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace lc {

// Mot khung hinh nhan duoc tu nguon truc tiep, da doi sang RGBA8 (hang dau = tren cung).
struct LiveVideoFrame {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
};

// Mot khoi am thanh nhan duoc: float32 xen ke, `channels` kenh, tan so `rate` bat ky (LiveSource doi sang 48 kHz stereo).
struct LiveAudioBlock {
    int rate = 48000;
    int channels = 2;
    std::vector<float> data;
};

// Mot KET NOI da mo toi mot nguon truc tiep (receiver NDI, camera...). Mat ket noi = bo doi tuong nay va mo cai moi.
class LiveInput {
public:
    enum class Event { None, Video, Audio, Lost };
    virtual ~LiveInput() = default;
    // Cho toi da timeoutMs roi tra ve su kien. Video/Audio: du lieu duoc dien vao v/a. Lost: ket noi het dung duoc (rut thiet bi, nguon tat...).
    // KHONG duoc treo qua timeoutMs (de LiveSource dung/ket thuc duoc).
    virtual Event capture(int timeoutMs, LiveVideoFrame& v, LiveAudioBlock& a) = 0;
    virtual QString info() const { return {}; }     // mo ta ngan ve ket noi (do phan giai, ten thiet bi...)
};

struct LiveOpenResult {
    std::unique_ptr<LiveInput> input;
    QString error;           // ly do that bai (tieng Viet)
    bool unavailable = false;   // true = khong the lam duoc tren may nay (chua cai NDI Runtime, nen tang chua ho tro) -> khong phai "mat tin hieu"
};
// Mo mot ket noi moi. Goi tu thread cua LiveSource (co the cham/chan vai tram ms).
using LiveInputFactory = std::function<LiveOpenResult()>;

} // namespace lc
