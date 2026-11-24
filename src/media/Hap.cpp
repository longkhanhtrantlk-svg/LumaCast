#include "Hap.h"
#include <algorithm>
#include <cstring>

namespace lc {

bool snappyDecompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out, size_t maxOut) {
    size_t i = 0;
    uint64_t len = 0;
    for (int shift = 0; shift < 35; shift += 7) {                // do dai giai nen: varint
        if (i >= n) return false;
        const uint8_t b = in[i++];
        len |= uint64_t(b & 0x7F) << shift;
        if (!(b & 0x80)) break;
        if (shift == 28) return false;
    }
    if (len > maxOut) return false;
    out.resize(size_t(len));
    size_t o = 0;
    while (i < n) {
        const uint8_t tag = in[i++];
        const int type = tag & 3;
        if (type == 0) {                                          // literal
            size_t l = tag >> 2;
            if (l >= 60) {
                const int extra = int(l) - 59;
                if (i + size_t(extra) > n) return false;
                l = 0;
                for (int k = 0; k < extra; ++k) l |= size_t(in[i + size_t(k)]) << (8 * k);
                i += size_t(extra);
            }
            ++l;
            if (i + l > n || o + l > out.size()) return false;
            std::memcpy(out.data() + o, in + i, l);
            i += l; o += l;
        } else {                                                  // copy
            size_t l, off;
            if (type == 1) {
                if (i >= n) return false;
                l = 4 + ((tag >> 2) & 7);
                off = (size_t(tag >> 5) << 8) | in[i++];
            } else if (type == 2) {
                if (i + 2 > n) return false;
                l = 1 + (tag >> 2);
                off = size_t(in[i]) | (size_t(in[i + 1]) << 8);
                i += 2;
            } else {
                if (i + 4 > n) return false;
                l = 1 + (tag >> 2);
                off = size_t(in[i]) | (size_t(in[i + 1]) << 8) | (size_t(in[i + 2]) << 16) | (size_t(in[i + 3]) << 24);
                i += 4;
            }
            if (off == 0 || off > o || o + l > out.size()) return false;
            for (size_t k = 0; k < l; ++k, ++o) out[o] = out[o - off];     // co the chong lap -> chep tung byte
        }
    }
    return o == out.size();
}

namespace {

struct Section {
    size_t size = 0;      // do dai noi dung
    uint8_t type = 0;
    size_t hdr = 0;       // do dai header (4 hoac 8)
};

bool readSection(const uint8_t* p, size_t avail, Section& s) {
    if (avail < 4) return false;
    s.size = size_t(p[0]) | (size_t(p[1]) << 8) | (size_t(p[2]) << 16);
    s.type = p[3];
    s.hdr = 4;
    if (s.size == 0) {
        if (avail < 8) return false;
        s.size = size_t(p[4]) | (size_t(p[5]) << 8) | (size_t(p[6]) << 16) | (size_t(p[7]) << 24);
        s.hdr = 8;
    }
    return s.hdr + s.size <= avail;
}

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

} // namespace

bool hapParseFrame(const uint8_t* pkt, size_t n, int w, int h, HapFrame& out, QString* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (w <= 0 || h <= 0 || w % 4 || h % 4) return fail("kich thuoc khong chia het cho 4");
    Section top;
    if (!readSection(pkt, n, top)) return fail("header HAP hong");
    HapFormat fmt;
    switch (top.type & 0x0F) {
    case 0x0B: fmt = HapFormat::Dxt1; break;
    case 0x0E: fmt = HapFormat::Dxt5; break;
    case 0x0F: fmt = HapFormat::YCoCgDxt5; break;
    default: return fail("dinh dang HAP chua ho tro (Alpha-only / hai texture)");
    }
    const size_t need = hapTextureBytes(fmt, w, h);
    const uint8_t* body = pkt + top.hdr;
    const size_t bodyN = top.size;
    std::vector<uint8_t> tex;
    switch (top.type & 0xF0) {
    case 0xA0:
        if (bodyN != need) return fail("kich thuoc texture khong khop");
        tex.assign(body, body + bodyN);
        break;
    case 0xB0:
        if (!snappyDecompress(body, bodyN, tex, need) || tex.size() != need) return fail("snappy hong");
        break;
    case 0xC0: {
        Section di;
        if (!readSection(body, bodyN, di) || di.type != 0x01) return fail("thieu decode instructions");
        const uint8_t* ip = body + di.hdr;
        size_t left = di.size;
        std::vector<uint8_t> comp;
        std::vector<uint32_t> sizes, offsets;
        while (left > 0) {
            Section s;
            if (!readSection(ip, left, s)) return fail("decode instructions hong");
            const uint8_t* c = ip + s.hdr;
            if (s.type == 0x02) comp.assign(c, c + s.size);
            else if (s.type == 0x03) for (size_t k = 0; k + 4 <= s.size; k += 4) sizes.push_back(le32(c + k));
            else if (s.type == 0x04) for (size_t k = 0; k + 4 <= s.size; k += 4) offsets.push_back(le32(c + k));
            ip += s.hdr + s.size;
            left -= s.hdr + s.size;
        }
        if (comp.empty() || comp.size() != sizes.size() || (!offsets.empty() && offsets.size() != comp.size())) return fail("bang chunk khong khop");
        tex.assign(need, 0);
        size_t src = di.hdr + di.size, dst = 0;
        for (size_t k = 0; k < comp.size(); ++k) {
            if (src + sizes[k] > bodyN) return fail("chunk vuot goi tin");
            const uint8_t* cd = body + src;
            std::vector<uint8_t> dec;
            const uint8_t* chunk = cd;
            size_t chunkN = sizes[k];
            if (comp[k] == 0x0B) {
                if (!snappyDecompress(cd, sizes[k], dec, need)) return fail("snappy chunk hong");
                chunk = dec.data(); chunkN = dec.size();
            } else if (comp[k] != 0x0A) {
                return fail("nen chunk khong biet");
            }
            const size_t at = offsets.empty() ? dst : offsets[k];
            if (at + chunkN > need) return fail("chunk vuot texture");
            std::memcpy(tex.data() + at, chunk, chunkN);
            dst = at + chunkN;
            src += sizes[k];
        }
        if (offsets.empty() && dst != need) return fail("chunk khong du texture");
        break;
    }
    default: return fail("nen tang hai khong biet");
    }
    out.format = fmt;
    out.w = w; out.h = h;
    out.data = std::move(tex);
    return true;
}

static void rgb565(uint16_t c, int& r, int& g, int& b) {
    r = ((c >> 11) & 31) * 255 / 31;
    g = ((c >> 5) & 63) * 255 / 63;
    b = (c & 31) * 255 / 31;
}

void hapSamplePixel(const HapFrame& f, int x, int y, uint8_t rgba[4]) {
    x = std::clamp(x, 0, f.w - 1); y = std::clamp(y, 0, f.h - 1);
    const bool dxt1 = f.format == HapFormat::Dxt1;
    const size_t bs = dxt1 ? 8 : 16;
    const uint8_t* blk = f.data.data() + (size_t(y / 4) * size_t(f.w / 4) + size_t(x / 4)) * bs;
    const uint8_t* cb = dxt1 ? blk : blk + 8;               // khoi mau (DXT5: sau 8 byte alpha)
    const uint16_t c0 = uint16_t(cb[0] | (cb[1] << 8)), c1 = uint16_t(cb[2] | (cb[3] << 8));
    int pal[4][3];
    rgb565(c0, pal[0][0], pal[0][1], pal[0][2]);
    rgb565(c1, pal[1][0], pal[1][1], pal[1][2]);
    if (c0 > c1 || !dxt1) {
        for (int k = 0; k < 3; ++k) { pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3; pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3; }
    } else {
        for (int k = 0; k < 3; ++k) { pal[2][k] = (pal[0][k] + pal[1][k]) / 2; pal[3][k] = 0; }
    }
    const uint32_t bits = le32(cb + 4);
    const int idx = (bits >> (2 * ((y & 3) * 4 + (x & 3)))) & 3;
    int r = pal[idx][0], g = pal[idx][1], b = pal[idx][2], a = 255;
    if (dxt1 && c0 <= c1 && idx == 3) a = 0;
    if (!dxt1) {
        const int a0 = blk[0], a1 = blk[1];
        uint64_t ab = 0;
        for (int k = 0; k < 6; ++k) ab |= uint64_t(blk[2 + k]) << (8 * k);
        const int ai = int((ab >> (3 * ((y & 3) * 4 + (x & 3)))) & 7);
        if (ai == 0) a = a0;
        else if (ai == 1) a = a1;
        else if (a0 > a1) a = ((8 - ai) * a0 + (ai - 1) * a1) / 7;
        else a = ai == 6 ? 0 : ai == 7 ? 255 : ((6 - ai) * a0 + (ai - 1) * a1) / 5;
    }
    if (f.format == HapFormat::YCoCgDxt5) {                  // (r,g,b,a) = (Co, Cg, scale, Y) - xem shader
        const double scale = (double(b) / 255.0) * (255.0 / 8.0) + 1.0;
        const double Co = (double(r) / 255.0 - 0.50196078431373) / scale, Cg = (double(g) / 255.0 - 0.50196078431373) / scale, Y = double(a) / 255.0;
        auto cl = [](double v) { return uint8_t(std::clamp(v, 0.0, 1.0) * 255.0 + 0.5); };
        rgba[0] = cl(Y + Co - Cg); rgba[1] = cl(Y + Cg); rgba[2] = cl(Y - Co - Cg); rgba[3] = 255;
        return;
    }
    rgba[0] = uint8_t(r); rgba[1] = uint8_t(g); rgba[2] = uint8_t(b); rgba[3] = uint8_t(a);
}

} // namespace lc
