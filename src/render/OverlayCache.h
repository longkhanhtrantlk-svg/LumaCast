#pragma once
#include "render/GlHelpers.h"
#include "render/OverlayRaster.h"
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>

namespace lc {

// Kho hinh lop phu cua render thread. Chi dung lai anh khi OverlayRasterKey doi; viec dung (QPainter, doc file logo) chay tren
// MOT thread phu nen render thread khong bi chan - trong luc cho, anh cu van duoc ve. Render thread chi upload texture khi co anh moi.
class OverlayCache {
public:
    struct Entry {
        std::vector<std::unique_ptr<GlTexture>> tiles;
        QSize size;                          // kich thuoc noi dung dang co tren GPU (rong = khong co gi de ve)
        OverlayRasterKey wanted;             // lan yeu cau gan nhat
        bool requested = false;
        bool ready = false;                  // da nhan it nhat mot ket qua
        uint64_t lastUsed = 0;
    };

    OverlayCache() = default;
    ~OverlayCache();

    // Render thread (co context GL). beginFrame() mot lan moi khung: nhan anh da dung xong va upload.
    void beginFrame(GL& gl);
    // Yeu cau anh cho lop phu (owner, id) tren dich co kich thuoc `target`; tra muc hien co de ve (nullptr neu chua co gi).
    // Chua co anh dung kich thuoc nay thi tam dung anh kich thuoc khac cua cung lop phu (se duoc keo gian).
    const Entry* request(const void* owner, int id, QSize target, const OverlayRasterKey& key);
    void release();                          // dung thread phu + giai phong texture (context phai con hien hanh)

    // Kiem thu / do dac.
    uint64_t rasterCount() const;            // so anh da dung
    bool waitIdle(int timeoutMs) const;      // cho den khi thread phu het viec (ket qua duoc nhan o beginFrame ke tiep)

private:
    using Key = std::tuple<const void*, int, int, int>;     // owner, id, rong, cao dich
    struct Job { Key where; OverlayRasterKey key; };
    struct Result { Key where; OverlayRasterKey key; OverlayBitmap bitmap; };
    void run();
    void submit(const Key& where, const OverlayRasterKey& key);

    std::map<Key, Entry> entries_;
    uint64_t frame_ = 0;

    mutable std::mutex m_;
    mutable std::condition_variable cv_, idle_;
    std::deque<Job> jobs_;
    std::vector<Result> results_;
    std::thread worker_;
    bool quit_ = false, busy_ = false;
    uint64_t rasters_ = 0;
};

} // namespace lc
