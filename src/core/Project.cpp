#include "Project.h"
#include "media/FFmpegSource.h"
#include "media/StaticSources.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace lc {

// ---------- nguon ----------

std::shared_ptr<Source> createSource(const SourceDef& d, QString* err) {
    auto fail = [&](const QString& m) -> std::shared_ptr<Source> { if (err) *err = m; return nullptr; };
    if (d.type == "video") {
        if (!QFileInfo::exists(d.path)) return fail("Khong tim thay video: " + d.path);
        return std::make_shared<FFmpegSource>(d.path);
    }
    if (d.type == "image") {
        auto s = ImageSource::fromFile(d.path);
        return s ? std::shared_ptr<Source>(s) : fail("Khong doc duoc anh: " + d.path);
    }
    if (d.type == "color")
        return std::make_shared<ColorSource>(d.color, d.label.isEmpty() ? d.id : d.label);
    if (d.type == "testpattern")
        return std::make_shared<ImageSource>(makeTestPattern(d.size.width(), d.size.height(), d.label),
                                             d.label.isEmpty() ? "test" : d.label);
    return fail("Loai nguon khong biet: " + d.type);
}

std::shared_ptr<Source> Project::addSource(SourceDef def, QString* err) {
    if (def.id.isEmpty()) def.id = QString("src%1").arg(sources.size() + 1);
    if (!def.source) def.source = createSource(def, err);
    if (!def.source) return nullptr;
    sources.push_back(def);
    return def.source;
}

const SourceDef* Project::findSource(const Source* s) const {
    for (const auto& d : sources) if (d.source.get() == s) return &d;
    return nullptr;
}

// ---------- wall ----------

std::vector<OutputConfig> makeWall(int channel, int cols, int rows, QSize cell, int firstScreen, bool fullscreen) {
    std::vector<OutputConfig> v;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            OutputConfig o;
            o.name = QString("Wall %1,%2").arg(r + 1).arg(c + 1);
            o.channel = channel;
            o.size = cell;
            o.fullscreen = fullscreen;
            o.screenIndex = firstScreen + int(v.size());
            o.scale = ScaleMode::Stretch;
            o.crop = QRectF(double(c) / cols, double(r) / rows, 1.0 / cols, 1.0 / rows);
            v.push_back(o);
        }
    return v;
}

// ---------- JSON ----------

// Tham so Effect <-> JSON (khong can biet dinh nghia, de van giu du lieu khi plug-in bi thieu)
static QJsonValue paramToJson(const QVariant& v) {
    switch (v.typeId()) {
    case QMetaType::Bool: return v.toBool();
    case QMetaType::QColor: return v.value<QColor>().name(QColor::HexArgb);
    case QMetaType::QPointF: return QJsonArray{v.toPointF().x(), v.toPointF().y()};
    case QMetaType::Int: return v.toInt();
    default: return v.toDouble();
    }
}
static QVariant paramFromJson(const QJsonValue& j) {
    if (j.isBool()) return j.toBool();
    if (j.isString()) { const QColor c(j.toString()); return c.isValid() ? QVariant::fromValue(c) : QVariant(j.toString()); }
    if (j.isArray() && j.toArray().size() == 2) return QPointF(j.toArray()[0].toDouble(), j.toArray()[1].toDouble());
    return j.toDouble();
}

static const char* scaleStr(ScaleMode m) {
    switch (m) {
    case ScaleMode::Fit: return "fit";
    case ScaleMode::Fill: return "fill";
    case ScaleMode::Stretch: return "stretch";
    case ScaleMode::Pixel: return "pixel";
    }
    return "fit";
}
static ScaleMode parseScale(const QString& s) {
    return s == "fill" ? ScaleMode::Fill : s == "stretch" ? ScaleMode::Stretch
         : s == "pixel" ? ScaleMode::Pixel : ScaleMode::Fit;
}
static const char* fitStr(LayerFit f) {
    return f == LayerFit::Stretch ? "stretch" : f == LayerFit::Fill ? "fill" : "fit";
}
static LayerFit parseFit(const QString& s) {
    return s == "stretch" ? LayerFit::Stretch : s == "fill" ? LayerFit::Fill : LayerFit::Fit;
}

bool saveProject(const Project& p, const QString& path, QString* err) {
    const QDir base = QFileInfo(path).absoluteDir();
    QJsonObject root;
    root["app"] = "LumaCast";
    root["version"] = 1;
    root["fps"] = p.fps;
    if (!p.audioDevice.isEmpty()) root["audio"] = QJsonObject{{"device", p.audioDevice}};     // tuy chon (M5a); khong tang version

    QJsonArray srcs;
    for (const SourceDef& d : p.sources) {
        QJsonObject o;
        o["id"] = d.id;
        o["type"] = d.type;
        if (d.type == "video" || d.type == "image") o["path"] = base.relativeFilePath(d.path);
        if (d.type == "color") { o["color"] = d.color.name(QColor::HexArgb); o["label"] = d.label; }
        if (d.type == "testpattern") { o["width"] = d.size.width(); o["height"] = d.size.height(); o["label"] = d.label; }
        srcs.append(o);
    }
    root["sources"] = srcs;

    QJsonArray scenes;
    for (const auto& s : p.scenes) {
        QJsonObject o;
        o["name"] = s->name;
        o["width"] = s->size.width();
        o["height"] = s->size.height();
        o["background"] = s->background.name(QColor::HexArgb);
        o["duration"] = s->duration;
        o["loop"] = s->loop;
        o["restartOnAir"] = s->restartOnAir;
        QJsonArray layers;
        for (const Layer& l : s->layers) {
            const SourceDef* d = p.findSource(l.source.get());
            if (!d) continue;                                  // layer rong/nguon la -> bo qua
            QJsonObject lo;
            lo["source"] = d->id;
            lo["x"] = l.rect.x(); lo["y"] = l.rect.y(); lo["w"] = l.rect.width(); lo["h"] = l.rect.height();
            lo["opacity"] = double(l.opacity);
            lo["fit"] = fitStr(l.fit);
            lo["visible"] = l.visible;
            if (l.rotation != 0.0f) lo["rotation"] = double(l.rotation);
            if (l.crop != QRectF(0, 0, 1, 1)) lo["crop"] = QJsonArray{l.crop.x(), l.crop.y(), l.crop.width(), l.crop.height()};
            if (l.blend != BlendMode::Normal) lo["blend"] = blendId(l.blend);
            if (std::abs(l.volume - 1.0f) > 1e-4f) lo["volume"] = double(l.volume);
            if (!l.tracks.empty()) {
                QJsonArray tracks;
                for (const Track& tr : l.tracks) {
                    if (tr.keys.empty()) continue;
                    QJsonObject to;
                    to["prop"] = animPropId(tr.prop);
                    QJsonArray keys;
                    for (const Keyframe& k : tr.keys) keys.append(QJsonObject{{"t", k.time}, {"v", k.value}, {"i", interpId(k.interp)}});
                    to["keys"] = keys;
                    tracks.append(to);
                }
                lo["tracks"] = tracks;
            }
            if (!l.effects.empty()) {
                QJsonArray fx;
                for (const EffectInstance& e : l.effects) {
                    QJsonObject eo;
                    eo["id"] = e.effectId;
                    eo["enabled"] = e.enabled;
                    QJsonObject po;
                    for (auto it = e.params.begin(); it != e.params.end(); ++it) po[it.key()] = paramToJson(it.value());
                    eo["params"] = po;
                    fx.append(eo);
                }
                lo["effects"] = fx;
            }
            layers.append(lo);
        }
        o["layers"] = layers;
        scenes.append(o);
    }
    root["scenes"] = scenes;

    QJsonArray chans;
    for (const auto& c : p.channels) {
        QJsonObject o;
        o["name"] = c->name;
        o["width"] = c->size.width();
        o["height"] = c->size.height();
        QJsonArray idx;
        for (const auto& s : c->scenes)
            for (size_t i = 0; i < p.scenes.size(); ++i) if (p.scenes[i] == s) idx.append(int(i));
        o["scenes"] = idx;
        o["program"] = c->program.load();
        o["preview"] = c->preview.load();
        if (std::abs(c->volume.load() - 1.0f) > 1e-4f) o["volume"] = double(c->volume.load());
        if (c->mute.load()) o["mute"] = true;
        chans.append(o);
    }
    root["channels"] = chans;

    QJsonArray outs;
    for (const OutputConfig& c : p.outputs) {
        QJsonObject o;
        o["name"] = c.name;
        o["channel"] = c.channel;
        o["screen"] = c.screenIndex;
        if (!c.screenName.isEmpty()) o["screenName"] = c.screenName;
        o["fullscreen"] = c.fullscreen;
        o["width"] = c.size.width();
        o["height"] = c.size.height();
        o["scale"] = scaleStr(c.scale);
        o["rotation"] = c.rotation;
        o["crop"] = QJsonArray{c.crop.x(), c.crop.y(), c.crop.width(), c.crop.height()};
        QJsonObject cal;
        cal["brightness"] = double(c.cal.brightness);
        cal["contrast"] = double(c.cal.contrast);
        cal["gamma"] = double(c.cal.gamma);
        cal["gain"] = QJsonArray{double(c.cal.gainR), double(c.cal.gainG), double(c.cal.gainB)};
        o["calibration"] = cal;
        outs.append(o);
    }
    root["outputs"] = outs;

    QSaveFile f(path);                                         // ghi nguyen tu: mat dien giua chung khong hong file
    if (!f.open(QIODevice::WriteOnly)) { if (err) *err = f.errorString(); return false; }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) { if (err) *err = f.errorString(); return false; }
    return true;
}

bool loadProject(Project& out, const QString& path, QString* err) {
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return fail("Khong mo duoc file: " + f.errorString());
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull() || !doc.isObject()) return fail("JSON loi: " + pe.errorString());
    const QJsonObject root = doc.object();
    if (root["app"].toString() != "LumaCast") return fail("Khong phai file project LumaCast");
    if (root["version"].toInt(0) < 1 || root["version"].toInt(0) > 1)
        return fail(QString("Phien ban project %1 khong ho tro").arg(root["version"].toInt(0)));

    const QDir base = QFileInfo(path).absoluteDir();
    Project p;
    p.fps = root["fps"].toDouble(60.0);
    p.audioDevice = root["audio"].toObject()["device"].toString();
    if (p.fps < 1 || p.fps > 240) return fail("fps khong hop le");
    p.filePath = path;

    for (const QJsonValue& v : root["sources"].toArray()) {
        const QJsonObject o = v.toObject();
        SourceDef d;
        d.id = o["id"].toString();
        d.type = o["type"].toString();
        d.label = o["label"].toString();
        if (d.id.isEmpty()) return fail("Nguon thieu id");
        for (const auto& e : p.sources) if (e.id == d.id) return fail("Trung id nguon: " + d.id);
        if (o.contains("path")) d.path = QDir::cleanPath(base.absoluteFilePath(o["path"].toString()));
        if (o.contains("color")) d.color = QColor(o["color"].toString());
        d.size = QSize(o["width"].toInt(1920), o["height"].toInt(1080));
        QString e;
        if (!p.addSource(d, &e)) return fail(QString("Nguon '%1': %2").arg(d.id, e));
    }

    for (const QJsonValue& v : root["scenes"].toArray()) {
        const QJsonObject o = v.toObject();
        auto s = std::make_shared<Scene>();
        s->name = o["name"].toString("Scene");
        s->size = QSize(o["width"].toInt(1920), o["height"].toInt(1080));
        if (s->size.width() < 1 || s->size.height() < 1) return fail("Kich thuoc Scene khong hop le: " + s->name);
        s->background = QColor(o["background"].toString("#ff000000"));
        {
            const double dur = o["duration"].toDouble(10.0);
            s->duration = std::isfinite(dur) && dur >= 0.1 ? dur : 10.0;
            s->loop = o["loop"].toBool(true);
            s->restartOnAir = o["restartOnAir"].toBool(true);
        }
        for (const QJsonValue& lv : o["layers"].toArray()) {
            const QJsonObject lo = lv.toObject();
            const QString sid = lo["source"].toString();
            std::shared_ptr<Source> src;
            for (const auto& d : p.sources) if (d.id == sid) src = d.source;
            if (!src) return fail(QString("Scene '%1': layer tham chieu nguon khong ton tai '%2'").arg(s->name, sid));
            Layer l;
            l.source = src;
            l.rect = QRectF(lo["x"].toDouble(0), lo["y"].toDouble(0), lo["w"].toDouble(1), lo["h"].toDouble(1));
            l.opacity = float(lo["opacity"].toDouble(1));
            l.fit = parseFit(lo["fit"].toString());
            l.visible = lo["visible"].toBool(true);
            {
                const double rot = lo["rotation"].toDouble(0);
                l.rotation = std::isfinite(rot) ? float(rot) : 0.0f;
                const QJsonArray cr = lo["crop"].toArray();
                if (cr.size() == 4) {
                    const QRectF c(cr[0].toDouble(), cr[1].toDouble(), cr[2].toDouble(), cr[3].toDouble());
                    if (c.width() > 0 && c.height() > 0) l.crop = c;
                }
                const double vol = lo["volume"].toDouble(1.0);
                l.volume = std::isfinite(vol) ? float(std::clamp(vol, 0.0, 2.0)) : 1.0f;
                const QString bl = lo["blend"].toString();
                l.blend = bl == "add" ? BlendMode::Add : bl == "multiply" ? BlendMode::Multiply : bl == "screen" ? BlendMode::Screen : BlendMode::Normal;
                for (const QJsonValue& tv : lo["tracks"].toArray()) {
                    const QJsonObject to = tv.toObject();
                    Track tr;
                    if (!animPropFromId(to["prop"].toString(), tr.prop) || l.track(tr.prop)) continue;        // prop la hoac trung: bo qua
                    for (const QJsonValue& kv : to["keys"].toArray()) {
                        const QJsonObject ko = kv.toObject();
                        const double t = ko["t"].toDouble(0), v = ko["v"].toDouble(0);
                        if (!std::isfinite(t) || !std::isfinite(v)) continue;
                        Keyframe k;
                        k.time = std::max(0.0, t);
                        k.value = v;
                        if (!interpFromId(ko["i"].toString("linear"), k.interp)) k.interp = Interp::Linear;
                        tr.keys.push_back(k);
                    }
                    sortKeys(tr);
                    if (!tr.keys.empty()) l.tracks.push_back(tr);
                }
            }
            for (const QJsonValue& ev : lo["effects"].toArray()) {
                const QJsonObject eo = ev.toObject();
                EffectInstance e;
                e.effectId = eo["id"].toString();
                if (e.effectId.isEmpty()) continue;
                e.enabled = eo["enabled"].toBool(true);
                const QJsonObject po = eo["params"].toObject();
                for (auto it = po.begin(); it != po.end(); ++it) e.params[it.key()] = paramFromJson(it.value());
                l.effects.push_back(e);
            }
            s->layers.push_back(l);
        }
        p.scenes.push_back(s);
    }

    for (const QJsonValue& v : root["channels"].toArray()) {
        const QJsonObject o = v.toObject();
        auto c = std::make_shared<Channel>();
        c->name = o["name"].toString("Channel");
        c->size = QSize(o["width"].toInt(1920), o["height"].toInt(1080));
        for (const QJsonValue& iv : o["scenes"].toArray()) {
            const int i = iv.toInt(-1);
            if (i < 0 || i >= int(p.scenes.size())) return fail(QString("Channel '%1': chi so scene sai: %2").arg(c->name).arg(i));
            c->scenes.push_back(p.scenes[i]);
        }
        const int n = int(c->scenes.size());
        {
            const double vol = o["volume"].toDouble(1.0);
            c->volume = std::isfinite(vol) ? float(std::clamp(vol, 0.0, 2.0)) : 1.0f;
            c->mute = o["mute"].toBool(false);
        }
        const int pg = o["program"].toInt(0), pv = o["preview"].toInt(1);
        c->program = (pg >= 0 && pg < n) ? pg : (n > 0 ? 0 : -1);
        c->preview = (pv >= 0 && pv < n && pv != c->program) ? pv : (n > 1 ? (c->program == 0 ? 1 : 0) : -1);
        p.channels.push_back(c);
    }
    if (p.channels.empty()) return fail("Project khong co Channel nao");

    for (const QJsonValue& v : root["outputs"].toArray()) {
        const QJsonObject o = v.toObject();
        OutputConfig c;
        c.name = o["name"].toString(QString("Output %1").arg(p.outputs.size() + 1));
        c.channel = o["channel"].toInt(0);
        if (c.channel < 0 || c.channel >= int(p.channels.size()))
            return fail(QString("Output '%1': kenh %2 khong ton tai").arg(c.name).arg(c.channel + 1));
        c.screenIndex = o["screen"].toInt(-1);
        c.screenName = o["screenName"].toString();
        c.fullscreen = o["fullscreen"].toBool(false);
        c.size = QSize(o["width"].toInt(1920), o["height"].toInt(1080));
        c.scale = parseScale(o["scale"].toString());
        c.rotation = ((o["rotation"].toInt(0) % 360) + 360) % 360 / 90 * 90;
        const QJsonArray cr = o["crop"].toArray();
        if (cr.size() == 4) c.crop = QRectF(cr[0].toDouble(), cr[1].toDouble(), cr[2].toDouble(), cr[3].toDouble());
        if (c.crop.width() <= 0 || c.crop.height() <= 0) return fail("Output '" + c.name + "': crop khong hop le");
        const QJsonObject cal = o["calibration"].toObject();
        c.cal.brightness = float(cal["brightness"].toDouble(1));
        c.cal.contrast = float(cal["contrast"].toDouble(1));
        c.cal.gamma = float(cal["gamma"].toDouble(1));
        const QJsonArray g = cal["gain"].toArray();
        if (g.size() == 3) { c.cal.gainR = float(g[0].toDouble(1)); c.cal.gainG = float(g[1].toDouble(1)); c.cal.gainB = float(g[2].toDouble(1)); }
        p.outputs.push_back(c);
    }
    if (p.outputs.empty()) return fail("Project khong co Output nao");

    out = std::move(p);
    return true;
}

} // namespace lc
