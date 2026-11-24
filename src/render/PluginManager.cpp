#include "PluginManager.h"
#include "core/AppSettings.h"
#include "core/Features.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>

namespace lc {

PluginManager::PluginManager(EffectRegistry& registry, QStringList dirs, QObject* parent)
    : QObject(parent), registry_(registry), dirs_(std::move(dirs)) {
    debounce_.setSingleShot(true);
    debounce_.setInterval(300);
    connect(&debounce_, &QTimer::timeout, this, [this] { rescan(); });
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this] { debounce_.start(); });
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this] { debounce_.start(); });
}

QStringList PluginManager::defaultDirs() {
    return {QCoreApplication::applicationDirPath() + "/plugins",
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/plugins"};
}

void PluginManager::watch(bool on) {
    watching_ = on;
    if (on) rewatch();
    else {
        if (!watcher_.directories().isEmpty()) watcher_.removePaths(watcher_.directories());
        if (!watcher_.files().isEmpty()) watcher_.removePaths(watcher_.files());
    }
}

void PluginManager::rewatch() {
    if (!watching_) return;
    QStringList paths;
    for (const QString& d : dirs_) {
        if (!QDir(d).exists()) QDir().mkpath(d);
        paths << d;
        for (const QFileInfo& sub : QDir(d).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            paths << sub.absoluteFilePath();
            for (const QFileInfo& f : QDir(sub.absoluteFilePath()).entryInfoList(QDir::Files)) paths << f.absoluteFilePath();
        }
    }
    const QStringList cur = watcher_.directories() + watcher_.files();
    QStringList add;
    for (const QString& p : paths) if (!cur.contains(p)) add << p;
    if (!add.isEmpty()) watcher_.addPaths(add);
}

void PluginManager::setEnabled(const QString& id, bool on) {
    QSettings st = appSettings();
    QStringList off = st.value("disabledPlugins").toStringList();
    off.removeAll(id);
    if (!on) off << id;
    st.setValue("disabledPlugins", off);
    rescan();
}

void PluginManager::rescan() {
    ++scanCount_;
    const QStringList disabled = appSettings().value("disabledPlugins").toStringList();

    std::vector<EffectDef> defs = builtinEffects();
    std::vector<PluginInfo> infos;
    for (const auto& d : defs) {
        PluginInfo i;
        i.id = d.id; i.name = d.name; i.version = d.version; i.author = d.author; i.description = d.description;
        i.kind = d.kind; i.builtin = true;
        infos.push_back(i);
    }

    std::map<QString, std::pair<EffectDef, PluginInfo>> found;      // id -> (def, info); thu muc sau ghi de
    std::vector<PluginInfo> failed;
    const bool pluginsAllowed = Entitlements::current().plugins;          // goi khong co plug-in: chi Effect/Transition dung san
    if (!pluginsAllowed && !pluginsNoted_) { pluginsNoted_ = true; qInfo().noquote() << "Plug-in không có trong gói giấy phép hiện tại: bỏ qua thư mục plug-in"; }
    for (const QString& dir : pluginsAllowed ? dirs_ : QStringList()) {
        for (const QFileInfo& sub : QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString folder = sub.absoluteFilePath();
            const QString manifestPath = folder + "/plugin.json";
            if (!QFileInfo::exists(manifestPath)) continue;
            PluginInfo info;
            info.dir = folder;
            info.name = sub.fileName();
            info.id = sub.fileName();
            auto fail = [&](const QString& msg) {
                info.ok = false;
                info.error = msg;
                if (lastErrors_.value(folder) != msg)                       // chi canh bao khi loi moi/doi, khong lap lai moi lan quet
                    qWarning().noquote() << QString("Plug-in '%1': %2").arg(folder, msg);
                lastErrors_[folder] = msg;
                failed.push_back(info);
            };
            QFile mf(manifestPath);
            if (!mf.open(QIODevice::ReadOnly)) { fail("khong doc duoc plugin.json"); continue; }
            const QByteArray manifestBytes = mf.readAll();
            QJsonParseError pe;
            const QJsonDocument doc = QJsonDocument::fromJson(manifestBytes, &pe);
            if (!doc.isObject()) { fail("plugin.json khong phai JSON hop le: " + pe.errorString()); continue; }
            const QJsonObject m = doc.object();
            const QString entry = m["entry"].toString();
            // chi cho phep file shader NAM TRONG thu muc plug-in (chan "../..")
            if (entry.isEmpty() || QFileInfo(entry).fileName() != entry) { fail("entry phai la ten file nam trong thu muc plug-in"); continue; }
            QFile sf(folder + "/" + entry);
            if (!sf.open(QIODevice::ReadOnly)) { fail("khong doc duoc file shader '" + entry + "'"); continue; }
            const QByteArray shaderBytes = sf.readAll();

            EffectDef def;
            QString err;
            if (!parseManifest(m, QString::fromUtf8(shaderBytes), def, &err)) { fail(err); continue; }
            info.id = def.id; info.name = def.name; info.version = def.version; info.author = def.author;
            info.description = def.description; info.kind = def.kind;
            if (def.id.startsWith("builtin.")) { fail("id bat dau bang 'builtin.' la danh rieng cho Effect dung san"); continue; }
            def.dir = folder;
            QCryptographicHash h(QCryptographicHash::Sha1);
            h.addData(manifestBytes);
            h.addData(shaderBytes);
            const QByteArray hash = h.result();
            auto it = revisions_.find(def.id);
            if (it == revisions_.end() || it->second.first != hash) revisions_[def.id] = {hash, ++revCounter_};
            def.revision = revisions_[def.id].second;       // noi dung khong doi -> giu revision (khong bien dich lai)
            info.enabled = !disabled.contains(def.id);
            found[def.id] = {def, info};                    // thu muc sau ghi de
        }
    }
    for (auto& [id, pr] : found) {
        infos.push_back(pr.second);
        if (pr.second.enabled) defs.push_back(pr.first);
    }
    for (const auto& f : failed) infos.push_back(f);
    registry_.setDefs(std::move(defs), std::move(infos));
    rewatch();
    emit changed();
}

} // namespace lc
