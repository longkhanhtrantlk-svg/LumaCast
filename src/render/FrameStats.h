#pragma once
#include <algorithm>
#include <vector>

namespace lc {

// Vong dem cac mau thoi gian (ms) gan nhat de tinh phan vi (p50/p99/max). Chi dung tren mot thread.
class FrameStats {
public:
    explicit FrameStats(size_t capacity = 300) : buf_(capacity, 0.0) {}
    void add(double v) {
        buf_[head_] = v;
        head_ = (head_ + 1) % buf_.size();
        if (count_ < buf_.size()) ++count_;
    }
    size_t size() const { return count_; }
    void clear() { head_ = 0; count_ = 0; }
    double max() const {
        double m = 0;
        for (size_t i = 0; i < count_; ++i) m = std::max(m, buf_[i]);
        return m;
    }
    // p trong [0,1]; 0 neu chua co mau.
    double percentile(double p) const {
        if (!count_) return 0;
        std::vector<double> v(buf_.begin(), buf_.begin() + count_);
        const size_t k = std::min(count_ - 1, size_t(p * double(count_ - 1) + 0.5));
        std::nth_element(v.begin(), v.begin() + k, v.end());
        return v[k];
    }
private:
    std::vector<double> buf_;
    size_t head_ = 0, count_ = 0;
};

// Thoi gian tung giai doan cua khung render (ms): p50/p99/max tren ~300 khung gan nhat.
struct StageStat { double p50 = 0, p99 = 0, max = 0; };
struct StageReport {
    StageStat frame;        // ca khung (khong tinh thoi gian ngu cho nhip)
    StageStat upload;       // day khung nguon len GPU (PBO/doi mau YUV)
    StageStat scene;        // ve Scene (khong tinh upload)
    StageStat transition;   // pha tron Transition
    StageStat monitor;      // anh thu nho cho monitor UI (doc nguoc GPU->CPU, ~15 lan/giay)
    StageStat output;       // dua ra tat ca Output (ve len cua so/NDI + swap)
};

} // namespace lc
