#pragma once
#include <QColor>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace lc {

// Mot tham so cua Effect/Transition (tu manifest plug-in hoac dung san). Giao dien tham so duoc sinh tu day.
struct ParamDef {
    enum Type { Float, Int, Bool, Color, Point, Choice };
    QString name;            // dinh danh GLSL: cung la ten uniform
    QString label;           // nhan hien thi
    Type type = Float;
    double min = 0, max = 1;
    QVariant def;            // Float: double | Int/Choice: int | Bool: bool | Color: QColor | Point: QPointF
    QStringList choices;     // Choice
};

// Mot Effect (1 texture vao -> 1 ra) hoac Transition (2 texture + tien do -> 1 ra), do shader GLSL dinh nghia.
// Effect/Transition dung san va plug-in cua nguoi dung dung CHUNG mot dinh dang nay.
struct EffectDef {
    enum Kind { Effect, Transition };
    QString id, name, description, author, version = "1.0.0";
    Kind kind = Effect;
    QString glsl;                 // ma nguoi dung: dinh nghia  vec4 effect(vec2 uv)  hoac  vec4 transition(vec2 uv)
    QVector<ParamDef> params;
    bool builtin = false;
    QString dir;                  // thu muc plug-in (rong neu dung san)
    int revision = 0;             // tang khi plug-in duoc nap lai -> render thread bien dich lai

    const ParamDef* param(const QString& name) const {
        for (const auto& p : params) if (p.name == name) return &p;
        return nullptr;
    }
};

constexpr int kPluginApiVersion = 1;

// Doc manifest (plugin.json) + ma shader. false + thong bao ro rang neu sai (khong crash).
bool parseManifest(const QJsonObject& manifest, const QString& shaderSource, EffectDef& out, QString* err);

// Ghep header chuan (uniform, ham tien ich, tham so) + ma nguoi dung + main(). Dong loi GLSL se tro dung dong trong file plug-in (#line 1).
QString buildFragmentSource(const EffectDef& d);

// Gia tri mac dinh cua moi tham so; va ep gia tri ve dung kieu/khoang cua tham so.
QVariantMap defaultParams(const EffectDef& d);
QVariant sanitizeParam(const ParamDef& p, const QVariant& v);

// Cac Effect/Transition dung san (blur, glow, chinh mau, RGB split, pixelate, noise; push, slide, zoom, luma wipe).
std::vector<EffectDef> builtinEffects();

} // namespace lc
