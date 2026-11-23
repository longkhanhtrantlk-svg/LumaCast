#include "EffectDef.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>

namespace lc {

static const QStringList kReserved = {"uInput", "uFrom", "uTo", "uProgress", "uSize", "uTime", "vUv", "fragColor", "main",
                                      "effect", "transition", "sampleInput", "sampleFrom", "sampleTo"};

static bool validIdentifier(const QString& s) {
    static const QRegularExpression re("^[A-Za-z_][A-Za-z0-9_]*$");
    return re.match(s).hasMatch() && !s.startsWith("gl_") && !kReserved.contains(s);
}

QVariant sanitizeParam(const ParamDef& p, const QVariant& v) {
    switch (p.type) {
    case ParamDef::Float: return std::clamp(v.isValid() ? v.toDouble() : p.def.toDouble(), p.min, p.max);
    case ParamDef::Int: return int(std::clamp(v.isValid() ? v.toDouble() : p.def.toDouble(), p.min, p.max));
    case ParamDef::Choice: return std::clamp(v.isValid() ? v.toInt() : p.def.toInt(), 0, std::max(0, int(p.choices.size()) - 1));
    case ParamDef::Bool: return v.isValid() ? v.toBool() : p.def.toBool();
    case ParamDef::Color: {
        if (v.canConvert<QColor>() && v.value<QColor>().isValid()) return v.value<QColor>();
        if (v.typeId() == QMetaType::QString && QColor(v.toString()).isValid()) return QColor(v.toString());
        return p.def;
    }
    case ParamDef::Point: {
        if (v.canConvert<QPointF>() && v.typeId() == QMetaType::QPointF) return v.toPointF();
        return p.def;
    }
    }
    return p.def;
}

QVariantMap defaultParams(const EffectDef& d) {
    QVariantMap m;
    for (const auto& p : d.params) m[p.name] = p.def;
    return m;
}

static QColor colorFromJson(const QJsonValue& v, const QColor& fallback) {
    if (v.isString()) { QColor c(v.toString()); return c.isValid() ? c : fallback; }
    if (v.isArray() && v.toArray().size() >= 3) {
        const auto a = v.toArray();
        return QColor::fromRgbF(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()),
                                a.size() > 3 ? float(a[3].toDouble(1)) : 1.0f);
    }
    return fallback;
}

bool parseManifest(const QJsonObject& m, const QString& shader, EffectDef& out, QString* err) {
    auto fail = [&](const QString& e) { if (err) *err = e; return false; };
    EffectDef d;
    d.id = m["id"].toString();
    static const QRegularExpression idRe("^[a-z0-9][a-z0-9._-]*$");
    if (!idRe.match(d.id).hasMatch()) return fail("id khong hop le (chi chu thuong, so, '.', '_', '-'): '" + d.id + "'");
    d.name = m["name"].toString(d.id);
    d.description = m["description"].toString();
    d.author = m["author"].toString();
    d.version = m["version"].toString("1.0.0");
    const int api = m["apiVersion"].toInt(0);
    if (api != kPluginApiVersion) return fail(QString("apiVersion %1 khong ho tro (can %2)").arg(api).arg(kPluginApiVersion));
    const QString type = m["type"].toString();
    if (type == "effect") d.kind = EffectDef::Effect;
    else if (type == "transition") d.kind = EffectDef::Transition;
    else return fail("type phai la 'effect' hoac 'transition'");
    if (shader.trimmed().isEmpty()) return fail("file shader rong");
    d.glsl = shader;

    const QJsonArray params = m["params"].toArray();
    for (int i = 0; i < params.size(); ++i) {
        const QJsonObject o = params[i].toObject();
        ParamDef p;
        p.name = o["name"].toString();
        const QString where = QString("params[%1] '%2': ").arg(i).arg(p.name);
        if (!validIdentifier(p.name)) return fail(where + "ten khong hop le hoac trung ten du phong");
        for (const auto& e : d.params) if (e.name == p.name) return fail(where + "trung ten tham so");
        p.label = o["label"].toString(p.name);
        const QString t = o["type"].toString("float");
        const QJsonValue def = o["default"];
        if (t == "float" || t == "int") {
            p.type = t == "float" ? ParamDef::Float : ParamDef::Int;
            p.min = o["min"].toDouble(0);
            p.max = o["max"].toDouble(1);
            if (p.min > p.max) return fail(where + "min > max");
            const double dv = std::clamp(def.toDouble(p.min), p.min, p.max);
            p.def = p.type == ParamDef::Int ? QVariant(int(dv)) : QVariant(dv);
        } else if (t == "bool") {
            p.type = ParamDef::Bool;
            p.def = def.toBool(false);
        } else if (t == "color") {
            p.type = ParamDef::Color;
            p.def = colorFromJson(def, QColor(Qt::white));
        } else if (t == "point") {
            p.type = ParamDef::Point;
            const QJsonArray a = def.toArray();
            p.def = a.size() >= 2 ? QPointF(a[0].toDouble(), a[1].toDouble()) : QPointF(0.5, 0.5);
        } else if (t == "choice") {
            p.type = ParamDef::Choice;
            for (const auto& c : o["choices"].toArray()) p.choices << c.toString();
            if (p.choices.size() < 2) return fail(where + "choice can it nhat 2 lua chon");
            int idx = def.isString() ? p.choices.indexOf(def.toString()) : def.toInt(0);
            p.def = std::clamp(idx, 0, int(p.choices.size()) - 1);
        } else {
            return fail(where + "type khong biet: " + t);
        }
        d.params.push_back(p);
    }
    out = std::move(d);
    return true;
}

QString buildFragmentSource(const EffectDef& d) {
    QString s = "#version 330 core\n"
                "in vec2 vUv;\nout vec4 fragColor;\n"
                "uniform vec2 uSize;\nuniform float uTime;\n";
    if (d.kind == EffectDef::Effect) {
        s += "uniform sampler2D uInput;\n"
             "vec4 sampleInput(vec2 uv) { return texture(uInput, uv); }\n";
    } else {
        s += "uniform sampler2D uFrom;\nuniform sampler2D uTo;\nuniform float uProgress;\n"
             "vec4 sampleFrom(vec2 uv) { return texture(uFrom, uv); }\n"
             "vec4 sampleTo(vec2 uv) { return texture(uTo, uv); }\n";
    }
    for (const auto& p : d.params) {
        switch (p.type) {
        case ParamDef::Float: s += "uniform float " + p.name + ";\n"; break;
        case ParamDef::Int: case ParamDef::Choice: s += "uniform int " + p.name + ";\n"; break;
        case ParamDef::Bool: s += "uniform bool " + p.name + ";\n"; break;
        case ParamDef::Color: s += "uniform vec4 " + p.name + ";\n"; break;
        case ParamDef::Point: s += "uniform vec2 " + p.name + ";\n"; break;
        }
    }
    s += "#line 1\n" + d.glsl + "\n";
    s += d.kind == EffectDef::Effect ? "void main() { fragColor = effect(vUv); }\n"
                                     : "void main() { fragColor = transition(vUv); }\n";
    return s;
}

} // namespace lc
