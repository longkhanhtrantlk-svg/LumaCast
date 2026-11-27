#include "OverlayCache.h"
#include <algorithm>
#include <chrono>

namespace lc {

namespace {
constexpr uint64_t kKeepFrames = 600;      // muc khong duoc hoi qua chung nay khung (vd. da xoa lop phu) thi giai phong
}

OverlayCache::~OverlayCache() { release(); }

void OverlayCache::release() {
    {
        std::lock_guard lk(m_);
        quit_ = true;
        jobs_.clear();
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard lk(m_);
        quit_ = false;
        results_.clear();
    }
    entries_.clear();
}

void OverlayCache::run() {
    std::unique_lock lk(m_);
    for (;;) {
        cv_.wait(lk, [this] { return quit_ || !jobs_.empty(); });
        if (quit_) return;
        Job job = std::move(jobs_.front());
        jobs_.pop_front();
        busy_ = true;
        lk.unlock();
        OverlayBitmap bmp = rasterizeOverlay(job.key);
        lk.lock();
        ++rasters_;
        results_.push_back({job.where, std::move(job.key), std::move(bmp)});
        busy_ = false;
        if (jobs_.empty()) idle_.notify_all();
    }
}

void OverlayCache::submit(const Key& where, const OverlayRasterKey& key) {
    std::lock_guard lk(m_);
    if (!worker_.joinable()) worker_ = std::thread([this] { run(); });      // chi tao khi project co lop phu
    auto it = std::find_if(jobs_.begin(), jobs_.end(), [&](const Job& j) { return j.where == where; });
    if (it != jobs_.end()) it->key = key;                                   // viec chua chay da cu: thay bang ban moi
    else jobs_.push_back({where, key});
    cv_.notify_one();
}

void OverlayCache::beginFrame(GL& gl) {
    ++frame_;
    std::vector<Result> done;
    {
        std::lock_guard lk(m_);
        done.swap(results_);
    }
    for (Result& r : done) {
        auto it = entries_.find(r.where);
        if (it == entries_.end()) continue;
        Entry& e = it->second;
        e.tiles.resize(r.bitmap.tiles.size());
        for (size_t i = 0; i < r.bitmap.tiles.size(); ++i) {
            if (!e.tiles[i]) e.tiles[i] = std::make_unique<GlTexture>();
            const QImage& img = r.bitmap.tiles[i];
            e.tiles[i]->upload(gl, img.constBits(), img.width(), img.height());
        }
        e.size = r.bitmap.size;
        e.ready = true;
    }
    for (auto it = entries_.begin(); it != entries_.end();)
        it = frame_ - it->second.lastUsed > kKeepFrames ? entries_.erase(it) : std::next(it);
}

const OverlayCache::Entry* OverlayCache::request(const void* owner, int id, QSize target, const OverlayRasterKey& key) {
    const Key where{owner, id, target.width(), target.height()};
    Entry& e = entries_[where];
    e.lastUsed = frame_;
    if (!e.requested || !(e.wanted == key)) {
        e.wanted = key;
        e.requested = true;
        submit(where, key);
    }
    if (e.ready) return &e;
    for (const auto& [k, other] : entries_)
        if (std::get<0>(k) == owner && std::get<1>(k) == id && other.ready) return &other;
    return nullptr;
}

uint64_t OverlayCache::rasterCount() const {
    std::lock_guard lk(m_);
    return rasters_;
}

bool OverlayCache::waitIdle(int timeoutMs) const {
    std::unique_lock lk(m_);
    return idle_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [this] { return jobs_.empty() && !busy_; });
}

} // namespace lc
