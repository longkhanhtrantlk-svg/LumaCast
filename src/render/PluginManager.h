#pragma once
#include "render/EffectRegistry.h"
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <map>

namespace lc {

// Quet thu muc plug-in shader: moi plug-in la mot thu muc co plugin.json + file shader (xem docs/PLUGIN-SDK.md).
// Nap vao EffectRegistry (cung noi chua Effect/Transition dung san). Theo doi thay doi file de nap lai nong.
// Plug-in loi (manifest sai, thieu file, trung id...) KHONG lam hong gi: duoc liet ke voi thong bao loi.
class PluginManager : public QObject {
    Q_OBJECT
public:
    // dirs: thu muc goc chua cac thu muc plug-in; thu muc SAU ghi de plug-in cung id o thu muc TRUOC.
    PluginManager(EffectRegistry& registry, QStringList dirs, QObject* parent = nullptr);
    static QStringList defaultDirs();           // <thu muc exe>/plugins, %AppData%/LumaCast/plugins

    void rescan();                              // doc lai het (dong bo); phat `changed()`
    void setEnabled(const QString& id, bool on);
    void watch(bool on);                        // bat/tat tu dong nap lai khi file doi
    QStringList dirs() const { return dirs_; }
    int scanCount() const { return scanCount_; }

signals:
    void changed();

private:
    void rewatch();

    EffectRegistry& registry_;
    QStringList dirs_;
    QFileSystemWatcher watcher_;
    QTimer debounce_;
    std::map<QString, std::pair<QByteArray, int>> revisions_;    // id -> (hash noi dung, revision)
    QHash<QString, QString> lastErrors_;                           // thu muc -> loi da canh bao gan nhat
    int revCounter_ = 1;
    int scanCount_ = 0;
    bool pluginsNoted_ = false;
    bool watching_ = false;
};

} // namespace lc
