#pragma once
#include <QString>
#include <QStringList>

namespace lc {

// Co tinh nang theo goi (SPEC muc 5), doc tu MOT FILE GIAY PHEP tach khoi loi (license.json). Mac dinh khong co file = "unrestricted": KHONG khoa gi.
// Day la co tinh nang, KHONG phai co che chong sao chep: file khong ky so/khong ma hoa. Neu can xac thuc giay phep that su (chu ky, gan may),
// thay phan nap file bang bo kiem tra rieng - phan con lai cua ung dung chi hoi Entitlements::allows()/maxOutputs().
enum class Feature { Plugins, Ndi, Camera, ExternalControl, Timeline };

struct Entitlements {
    QString edition = "unrestricted";   // "unrestricted" (khong co file) | "basic" | "commercial" | ten tuy y (chi dung cac co ghi ro)
    QString licensee;
    int maxOutputs = 0;                 // 0 = khong gioi han
    bool plugins = true, ndi = true, camera = true, externalControl = true, timeline = true;
    QString sourcePath;                 // file da nap (rong = mac dinh)
    QString error;                      // file co nhung khong doc duoc -> khong khoa gi, hien trong Gioi thieu

    bool allows(Feature f) const;
    bool restricted() const { return maxOutputs > 0 || !plugins || !ndi || !camera || !externalControl || !timeline; }
    // Tom tat 1 dong cho Gioi thieu/log: "Goi: Co ban (khoa: plug-in, NDI...) - Cap cho: ..."
    QString summary() const;
    static QString featureName(Feature f);

    // preset: "basic" (<= 2 Output, khong plug-in/NDI/camera/dieu khien ngoai/timeline - SPEC muc 5) | "commercial" (mo het). Khac -> mo het.
    static Entitlements preset(const QString& edition);
    // Doc license.json. File sai -> tra ve unrestricted + error (khong lam hong buoi dien).
    static Entitlements load(const QString& path);
    static QStringList searchPaths();   // $LUMACAST_LICENSE, <thu muc exe>/license.json, <AppData>/license.json (dau tien co that duoc dung)

    // Giay phep dang co hieu luc cua tien trinh: nap lan dau tu searchPaths(); setCurrent() de kiem thu/ghi de.
    static const Entitlements& current();
    static void setCurrent(const Entitlements& e);
};

} // namespace lc
