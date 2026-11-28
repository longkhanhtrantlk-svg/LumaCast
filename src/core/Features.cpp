#include "Features.h"
#include "core/I18n.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <mutex>

namespace lc {

bool Entitlements::allows(Feature f) const {
    switch (f) {
    case Feature::Plugins: return plugins;
    case Feature::Ndi: return ndi;
    case Feature::Camera: return camera;
    case Feature::ExternalControl: return externalControl;
    case Feature::Timeline: return timeline;
    }
    return true;
}

QString Entitlements::featureName(Feature f) {
    switch (f) {
    case Feature::Plugins: return "plug-in";
    case Feature::Ndi: return "NDI";
    case Feature::Camera: return "camera/capture";
    case Feature::ExternalControl: return LC_TR("điều khiển ngoài (HTTP/OSC)");
    case Feature::Timeline: return "timeline/keyframe";
    }
    return "?";
}

QString Entitlements::summary() const {
    QString s;
    if (!restricted()) {
        s = sourcePath.isEmpty() ? LC_TR("Không giới hạn tính năng (không có file giấy phép)") : QString(LC_TR("Gói %1 — mở đủ tính năng")).arg(edition);
    } else {
        QStringList off;
        for (Feature f : {Feature::Plugins, Feature::Ndi, Feature::Camera, Feature::ExternalControl, Feature::Timeline})
            if (!allows(f)) off << featureName(f);
        s = QString(LC_TR("Gói %1")).arg(edition);
        if (maxOutputs > 0) s += QString(LC_TR(" — tối đa %1 Output")).arg(maxOutputs);
        if (!off.isEmpty()) s += LC_TR(" — không có: ") + off.join(", ");
    }
    if (!licensee.isEmpty()) s += QString(LC_TR(" — cấp cho: %1")).arg(licensee);
    if (!error.isEmpty()) s += QString(LC_TR(" [lỗi file giấy phép: %1 → không khoá gì]")).arg(error);
    return s;
}

Entitlements Entitlements::preset(const QString& edition) {
    Entitlements e;
    e.edition = edition;
    if (edition == "basic") {
        e.maxOutputs = 2;
        e.plugins = e.ndi = e.camera = e.externalControl = e.timeline = false;
    }
    return e;
}

Entitlements Entitlements::load(const QString& path) {
    Entitlements e;
    e.sourcePath = path;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { e.error = LC_TR("không đọc được file"); return e; }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject()) { e.error = LC_TR("không phải JSON hợp lệ: ") + pe.errorString(); return e; }
    const QJsonObject o = doc.object();
    if (o.contains("version") && o["version"].toInt() != 1) { e.error = LC_TR("phiên bản file giấy phép không biết"); return e; }
    const QString ed = o["edition"].toString().trimmed().toLower();
    if (ed.isEmpty()) { e.error = LC_TR("thiếu 'edition'"); return e; }
    Entitlements r = preset(ed);
    r.sourcePath = path;
    r.licensee = o["licensee"].toString();
    if (o.contains("maxOutputs")) {
        const int m = o["maxOutputs"].toInt(-1);
        if (m < 0) { e.error = LC_TR("'maxOutputs' phải là số nguyên ≥ 0"); return e; }
        r.maxOutputs = m;
    }
    const QJsonObject fe = o["features"].toObject();
    auto flag = [&](const char* k, bool& v) { if (fe.contains(k) && fe[k].isBool()) v = fe[k].toBool(); };
    flag("plugins", r.plugins); flag("ndi", r.ndi); flag("camera", r.camera);
    flag("externalControl", r.externalControl); flag("timeline", r.timeline);
    return r;
}

QStringList Entitlements::searchPaths() {
    QStringList p;
    const QString env = qEnvironmentVariable("LUMACAST_LICENSE");
    if (!env.isEmpty()) p << env;
    if (QCoreApplication::instance()) p << QCoreApplication::applicationDirPath() + "/license.json";
    p << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/license.json";
    return p;
}

static std::mutex gMu;
static Entitlements gCur;
static bool gLoaded = false;

const Entitlements& Entitlements::current() {
    std::lock_guard lk(gMu);
    if (!gLoaded) {
        for (const QString& p : searchPaths()) {
            if (!QFileInfo::exists(p)) continue;
            gCur = load(p);
            i18n::SourceScope logLanguage;
            qInfo().noquote() << "Giấy phép:" << gCur.summary() << "(" << p << ")";
            break;
        }
        gLoaded = true;
    }
    return gCur;
}

void Entitlements::setCurrent(const Entitlements& e) {
    std::lock_guard lk(gMu);
    gCur = e;
    gLoaded = true;
}

} // namespace lc
