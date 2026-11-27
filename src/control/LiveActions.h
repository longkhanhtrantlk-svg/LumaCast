#pragma once
#include <QString>
#include <QStringList>

namespace lc {

// Danh sach lenh LIVE gan duoc phim tat (UI-07) va MIDI (CTL-01). Ma lenh (chuoi) la khoa luu trong QSettings:
//   lock, take, cut, ftb, blackAll, preview.1 .. preview.9, output.N.black | output.N.freeze | output.N.pattern (N tu 1).
struct LiveAction {
    enum Kind { None, Lock, Take, Cut, Ftb, BlackAll, Preview, OutputBlack, OutputFreeze, OutputPattern };
    Kind kind = None;
    int index = 0;                                      // Preview: 1..9; Output*: so Output (tu 1)

    static constexpr int kPreviewCount = 9;
    static constexpr int kMaxOutputIndex = 64;          // chan gia tri vo ly doc tu cau hinh

    bool valid() const { return kind != None; }
    bool operator==(const LiveAction& o) const { return kind == o.kind && index == o.index; }
    QString id() const;
    static LiveAction fromId(const QString& id);        // ma la -> kind = None
    QString label(const QString& outputName = {}) const;   // tieng Viet, cho giao dien va log
    // Lenh duoc phep goi tu ngoai (MIDI): dung bang tap lenh live cua HTTP/OSC. Khoa van hanh KHONG co o day (khong khoa/mo khoa tu ngoai).
    bool externalAllowed() const { return kind != None && kind != Lock; }
    // Thu tu hien thi: khoa, TAKE, CUT, FTB, BLACK ALL, Preview 1-9, roi tung Output (Den, Dung, Mau).
    int order() const;
    static QStringList allIds(int outputCount);
};

} // namespace lc
