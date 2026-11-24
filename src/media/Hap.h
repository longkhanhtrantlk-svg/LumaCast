#pragma once
#include <QString>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lc {

// HAP (Vidvox) - tach khung HAP thanh du lieu DXT de day THANG len GPU (SRC-05); khong dung bo giai ma cua FFmpeg (no giai ma DXT -> RGBA tren CPU).
// Ho tro: Hap (DXT1), Hap Alpha (DXT5), Hap Q (YCoCg DXT5, doi mau bang shader); nen tang hai: none / snappy / complex (nhieu chunk).
// Hap Alpha-only, Hap Q Alpha (hai texture)... khong ho tro -> nguon tu ve bo giai ma FFmpeg.
enum class HapFormat : uint8_t { None, Dxt1, Dxt5, YCoCgDxt5 };

struct HapFrame {
    HapFormat format = HapFormat::None;
    int w = 0, h = 0;                     // kich thuoc anh (boi so cua 4)
    std::vector<uint8_t> data;            // khoi DXT lien tuc, (w/4)*(h/4) khoi
};

inline size_t hapTextureBytes(HapFormat f, int w, int h) {
    return size_t(w / 4) * size_t(h / 4) * (f == HapFormat::Dxt1 ? 8 : 16);
}

// Giai nen Snappy (dinh dang raw, khong framing). false neu hong/vuot kich thuoc.
bool snappyDecompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out, size_t maxOut);

// Phan tich mot goi tin HAP (anh w x h, boi so cua 4). false + err neu khong ho tro/hong.
bool hapParseFrame(const uint8_t* pkt, size_t n, int w, int h, HapFrame& out, QString* err = nullptr);

// Lay mot diem anh RGBA tu khung HAP (CPU; chi cho thumbnail UI va kiem thu). YCoCg duoc doi sang RGB.
void hapSamplePixel(const HapFrame& f, int x, int y, uint8_t rgba[4]);

} // namespace lc
