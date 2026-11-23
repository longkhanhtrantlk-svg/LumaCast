#include "Project.h"
#include "media/FFmpegSource.h"
#include "media/StaticSources.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

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
        chans.append(o);
    }
    root["channels"] = chans;

    QJsonArray outs;
    for (const OutputConfig& c : p.outputs) {
        QJsonObject o;
        o["name"] = c.name;
        o["channel"] = c.channel;
        o["screen"] = c.screenIndex;
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
