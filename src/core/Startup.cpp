#include "Startup.h"
#include "core/I18n.h"
#include <QDir>
#include <QFile>

namespace lc {

QString autostartCommand(const QString& exePath) { return "\"" + QDir::toNativeSeparators(exePath) + "\""; }

bool setAutostart(QSettings& runKey, bool on, const QString& exePath) {
    if (on) {
        if (exePath.isEmpty() || exePath.contains('"')) return false;
        runKey.setValue(kRunValueName, autostartCommand(exePath));
    } else {
        runKey.remove(kRunValueName);
    }
    runKey.sync();
    return runKey.status() == QSettings::NoError;
}

AutostartState autostartState(QSettings& runKey, const QString& exePath) {
    runKey.sync();
    if (!runKey.contains(kRunValueName)) return AutostartState::Off;
    const QString cur = runKey.value(kRunValueName).toString().trimmed();
    return cur.compare(autostartCommand(exePath), Qt::CaseInsensitive) == 0 ? AutostartState::On : AutostartState::OnOtherPath;
}

bool autostartSupported() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

std::unique_ptr<QSettings> openRunKey() {
#ifdef _WIN32
    return std::make_unique<QSettings>(kRunKeyPath, QSettings::NativeFormat);
#else
    return nullptr;
#endif
}

bool reopenLastEnabled(QSettings& st) { return st.value(kKeyReopenLast, true).toBool(); }
void setReopenLast(QSettings& st, bool on) { st.setValue(kKeyReopenLast, on); }
QString lastProjectPath(QSettings& st) { return st.value(kKeyLastProject).toString(); }
void rememberProject(QSettings& st, const QString& path) {
    if (!path.isEmpty()) st.setValue(kKeyLastProject, QFileInfo(path).absoluteFilePath());
}

StartupDecision decideStartupProject(QSettings& st, bool hasExplicitConfig) {
    StartupDecision d;
    if (hasExplicitConfig || !reopenLastEnabled(st)) return d;
    const QString last = lastProjectPath(st);
    if (last.isEmpty()) return d;
    if (!QFileInfo::exists(last) && !QFileInfo::exists(last + ".autosave")) {
        const i18n::Text notice = LC_BOTH(QString(LC_TR("Không tìm thấy project gần nhất: %1 — chạy với project trống.")).arg(QDir::toNativeSeparators(last)));
        d.notice = notice.shown;
        d.noticeLog = notice.source;
        return d;
    }
    d.path = last;
    return d;
}

QString pickProjectToLoad(const QString& path, const std::function<bool(const QFileInfo&)>& askRestore) {
    const QFileInfo main(path), autosave(path + ".autosave");
    if (autosave.exists() && (!main.exists() || autosave.lastModified() > main.lastModified()) && askRestore && askRestore(autosave))
        return autosave.filePath();
    return path;
}

} // namespace lc
