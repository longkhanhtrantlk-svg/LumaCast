#pragma once
#include <QColor>
#include <QDir>
#include <QJsonArray>
#include <QRectF>
#include <QString>
#include <map>
#include <mutex>
#include <vector>

namespace lc {

// Lop phu (SRC-08): tieu de, chu chay, dong ho dem nguoc, logo. Thuoc ve MOT Channel, ve de len Program (sau transition, truoc FTB)
// nen giu nguyen khi doi Scene. Preview khong co lop phu.
enum class OverlayType { Title, Ticker, Countdown, Logo };
enum class OverlayAlign { Left, Center, Right };

inline const char* overlayTypeId(OverlayType t) {
    switch (t) {
    case OverlayType::Title: return "title";
    case OverlayType::Ticker: return "ticker";
    case OverlayType::Countdown: return "countdown";
    case OverlayType::Logo: return "logo";
    }
    return "title";
}

inline constexpr int kMaxOverlays = 32;                    // moi Channel
inline constexpr int kMaxOverlayText = 2000;               // ky tu
inline constexpr int kMaxCountdownSeconds = 100 * 3600 - 1;

// Dinh nghia (SUA duoc: co Hoan tac, bi Khoa van hanh chan, luu trong project). Moi kich thuoc diem anh tinh theo canvas cua Channel.
struct OverlayDef {
    int id = 0;                          // duy nhat trong Channel
    OverlayType type = OverlayType::Title;
    QString name;
    QRectF rect{0.05, 0.75, 0.9, 0.18};  // chuan hoa theo canvas, goc tren-trai
    float opacity = 1.0f;
    float fade = 0.3f;                   // giay hien/an mo dan; 0 = cat ngay
    QString text;                        // tieu de / chu chay; dem nguoc: chu dung truoc dong ho (tuy chon)
    QString font;                        // ho font; rong = font mac dinh cua he thong
    int fontSize = 72;                   // px
    bool bold = false;
    bool italic = false;
    QColor color{Qt::white};
    QColor outlineColor{Qt::black};
    float outlineWidth = 0.0f;           // px; 0 = khong vien
    QColor background{0, 0, 0, 0};       // alpha 0 = khong nen
    OverlayAlign align = OverlayAlign::Center;
    double speed = 150.0;                // chu chay: px/giay (sang trai)
    int seconds = 300;                   // dem nguoc: thoi luong
    QString image;                       // logo: duong dan tuyet doi
    // Tieng nhac cua dem nguoc (tu tong hop, di qua bus tieng cua Channel). Mac dinh TAT.
    bool alert = false;
    int alertSeconds = 10;               // con bay nhieu giay thi bat dau nhac, moi giay mot tieng
    int fastSeconds = 5;                 // con bay nhieu giay thi nhac cao hon va gap hon (2 tieng/giay); 0 = khong
    int alertVolume = 80;                // %

    bool operator==(const OverlayDef&) const = default;
};

inline constexpr int kMaxAlertSeconds = 3600;

// Mac dinh theo loai cho nut "Them" (chua co id).
OverlayDef defaultOverlay(OverlayType type);
// "mm:ss", hoac "h:mm:ss" khi `withHours`.
QString formatCountdown(int seconds, bool withHours);
// Chuoi hien thi: tieu de/chu chay = text; dem nguoc = [text + " "] + dong ho con `secondsLeft`.
QString overlayDisplayText(const OverlayDef& d, int secondsLeft);

// Trang thai cua mot lop phu tai mot thoi diem (cho render va UI).
struct OverlayFrame {
    OverlayDef def;
    bool on = false;
    float level = 0.0f;             // 0 = an han, 1 = hien han (da tinh mo dan)
    double onSeconds = 0.0;         // da hien bao lau (chu chay cuon theo day, khong theo so khung)
    int secondsLeft = 0;            // dem nguoc, lam tron len
    bool running = false;           // dem nguoc dang chay
    double remaining = 0.0;         // dem nguoc: so giay con lai (khong lam tron)
    bool finished = false;          // dem nguoc da chay het (00:00), chua Dat lai
    bool alerting = false;          // dang nhac (co bat tieng nhac, dang hien, dang chay trong khoang nhac hoac da het gio): so doi do
};

// Dinh nghia dung de VE: dang nhac thi chu doi sang do.
inline const QColor kAlertColor{255, 59, 48};
OverlayDef overlayShownDef(const OverlayFrame& f);

// Cac lop phu cua mot Channel: dinh nghia + trang thai LIVE (bat/tat, dong ho). An toan giua cac thread (UI goi truc tiep, render doc moi khung);
// moi thao tac chi giu mutex trong thoi gian rat ngan. `now` = RenderEngine::now() (giay).
// Quy tac: KHONG gi tu len hinh ngoai lenh setOn() - them moi, hay xoa roi Hoan tac, deu o trang thai TAT.
class OverlayStack {
public:
    std::vector<OverlayDef> defs() const;
    // Thay toan bo danh sach (thu tu = thu tu ve, phan tu cuoi nam tren cung). Id bi go mat trang thai; doi thoi luong thi dong ho ve dau.
    void setDefs(std::vector<OverlayDef> defs);
    bool empty() const;
    int nextId() const;

    void setOn(int id, bool on, double now);
    bool isOn(int id) const;
    void countdownStart(int id, double now);
    void countdownPause(int id, double now);
    void countdownReset(int id);

    // Moi lop phu theo thu tu ve (ca cai dang tat: de dung san hinh chu truoc khi bat).
    std::vector<OverlayFrame> frames(double now) const;
    bool frame(int id, double now, OverlayFrame& out) const;

    // Chi dung khi nap project: dat trang thai bat ma khong mo dan.
    void restoreOn(int id);

private:
    struct Live {
        bool on = false;
        bool fading = false;            // false: muc = on ? 1 : 0
        double switchedAt = 0;
        float levelAtSwitch = 0;
        double onSince = -1;            // -1 = chua ro (vua mo project): lay lan hoi dau tien
        bool running = false;
        double startedAt = 0;
        double accumulated = 0;         // giay da dem truoc lan chay hien tai
    };
    static float level(const Live& l, const OverlayDef& d, double now);
    OverlayFrame frameOf(const OverlayDef& d, double now) const;       // da khoa mutex
    const OverlayDef* find(int id) const;

    mutable std::mutex m_;
    std::vector<OverlayDef> defs_;
    mutable std::map<int, Live> live_;
};

// channels[].overlays trong file project (duong dan logo tuong doi theo `base`).
QJsonArray overlaysToJson(const OverlayStack& s, const QDir& base);
void overlaysFromJson(OverlayStack& s, const QJsonArray& a, const QDir& base);

} // namespace lc
