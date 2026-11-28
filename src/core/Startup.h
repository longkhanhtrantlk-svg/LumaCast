#pragma once
#include <QFileInfo>
#include <QSettings>
#include <QString>
#include <functional>
#include <memory>

namespace lc {

// ---- OPS-01 (a): khoi dong cung Windows ----
// Logic tach khoi registry de test duoc tren Linux: nhan mot QSettings bat ky (that su la registry tren Windows, la file .ini khi test).
constexpr const char* kRunKeyPath = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const char* kRunValueName = "LumaCast";

enum class AutostartState { Off, On, OnOtherPath };      // OnOtherPath: da bat nhung tro toi exe khac/cu (vd. da di chuyen thu muc)

QString autostartCommand(const QString& exePath);        // "\"C:\\duong dan\\lumacast.exe\"" (dau ngoac kep, dau gach nguoc kieu Windows)
bool setAutostart(QSettings& runKey, bool on, const QString& exePath);   // false neu duong dan khong hop le hoac ghi loi
AutostartState autostartState(QSettings& runKey, const QString& exePath);
bool autostartSupported();                               // chi Windows
std::unique_ptr<QSettings> openRunKey();                 // registry HKCU\...\Run tren Windows; nullptr o noi khac

// ---- OPS-01 (b): nho va mo lai project gan nhat ----
constexpr const char* kKeyLastProject = "startup/lastProject";
constexpr const char* kKeyReopenLast = "startup/reopenLast";

bool reopenLastEnabled(QSettings& st);                   // mac dinh BAT
void setReopenLast(QSettings& st, bool on);
QString lastProjectPath(QSettings& st);
void rememberProject(QSettings& st, const QString& path);

struct StartupDecision {
    QString path;                                        // project can mo (rong = chay binh thuong)
    QString notice;                                      // thong bao cho nguoi dung (rong = khong co)
    QString noticeLog;                                   // cung noi dung bang tieng Viet, de ghi log
};
// hasExplicitConfig: nguoi dung da chi dinh cau hinh qua dong lenh (-o/-m/--wall/--save) -> khong tu mo.
StartupDecision decideStartupProject(QSettings& st, bool hasExplicitConfig);

// Chon file de nap: neu ban .autosave moi hon (hoac file chinh mat) thi hoi `askRestore`; dong y thi nap ban tu luu.
// Day la co che khoi phuc co san (PRJ-03), dung chung cho --project va tu mo lai.
QString pickProjectToLoad(const QString& path, const std::function<bool(const QFileInfo& autosave)>& askRestore);

} // namespace lc
