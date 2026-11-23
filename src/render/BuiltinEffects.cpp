// Effect/Transition dung san. Cung dinh dang voi plug-in cua nguoi dung (xem docs/PLUGIN-SDK.md).
#include "EffectDef.h"

namespace lc {

static ParamDef pf(const char* name, const char* label, double mn, double mx, double def) {
    ParamDef p; p.name = name; p.label = label; p.type = ParamDef::Float; p.min = mn; p.max = mx; p.def = def; return p;
}
static ParamDef pc(const char* name, const char* label, QStringList choices, int def = 0) {
    ParamDef p; p.name = name; p.label = label; p.type = ParamDef::Choice; p.choices = choices; p.def = def; return p;
}

static EffectDef make(const char* id, const char* name, const char* desc, EffectDef::Kind kind, QVector<ParamDef> params, const char* glsl) {
    EffectDef d;
    d.id = id; d.name = name; d.description = desc; d.kind = kind; d.params = params; d.glsl = glsl;
    d.builtin = true; d.author = "LumaCast"; d.revision = 1;
    return d;
}

std::vector<EffectDef> builtinEffects() {
    std::vector<EffectDef> v;

    v.push_back(make("builtin.blur", "Làm mờ", "Làm mờ đĩa (32 mẫu xoắn ốc).", EffectDef::Effect,
        {pf("radius", "Bán kính (px)", 0, 64, 8)}, R"(
vec4 effect(vec2 uv) {
    if (radius < 0.5) return sampleInput(uv);
    vec2 px = radius / uSize;
    vec4 sum = vec4(0.0);
    const int N = 32;
    for (int i = 0; i < N; ++i) {
        float fi = float(i);
        float rr = sqrt((fi + 0.5) / float(N));
        float a = fi * 2.399963;
        sum += sampleInput(uv + vec2(cos(a), sin(a)) * rr * px);
    }
    return sum / float(N);
})"));

    v.push_back(make("builtin.glow", "Phát sáng (glow)", "Cộng thêm vầng sáng từ vùng sáng của hình.", EffectDef::Effect,
        {pf("intensity", "Cường độ", 0, 4, 1), pf("radius", "Bán kính (px)", 0, 64, 12), pf("threshold", "Ngưỡng sáng", 0, 1, 0.6)}, R"(
vec4 effect(vec2 uv) {
    vec4 base = sampleInput(uv);
    vec2 px = radius / uSize;
    vec3 acc = vec3(0.0);
    const int N = 32;
    for (int i = 0; i < N; ++i) {
        float fi = float(i);
        float rr = sqrt((fi + 0.5) / float(N));
        float a = fi * 2.399963;
        vec3 c = sampleInput(uv + vec2(cos(a), sin(a)) * rr * px).rgb;
        float l = max(c.r, max(c.g, c.b));
        acc += c * smoothstep(threshold, 1.0, l);
    }
    return vec4(base.rgb + acc / float(N) * intensity, base.a);
})"));

    v.push_back(make("builtin.colorgrade", "Chỉnh màu", "Độ sáng, tương phản, bão hoà, xoay sắc độ, gamma.", EffectDef::Effect,
        {pf("brightness", "Độ sáng", -1, 1, 0), pf("contrast", "Tương phản", 0, 3, 1), pf("saturation", "Bão hoà", 0, 3, 1),
         pf("hue", "Sắc độ (độ)", -180, 180, 0), pf("gamma", "Gamma", 0.2, 4, 1)}, R"(
vec3 hueRotate(vec3 c, float deg) {
    float a = radians(deg), cs = cos(a), sn = sin(a);
    vec3 r0 = vec3(0.213 + cs * 0.787 - sn * 0.213, 0.715 - cs * 0.715 - sn * 0.715, 0.072 - cs * 0.072 + sn * 0.928);
    vec3 r1 = vec3(0.213 - cs * 0.213 + sn * 0.143, 0.715 + cs * 0.285 + sn * 0.140, 0.072 - cs * 0.072 - sn * 0.283);
    vec3 r2 = vec3(0.213 - cs * 0.213 - sn * 0.787, 0.715 - cs * 0.715 + sn * 0.715, 0.072 + cs * 0.928 + sn * 0.072);
    return vec3(dot(r0, c), dot(r1, c), dot(r2, c));
}
vec4 effect(vec2 uv) {
    vec4 s = sampleInput(uv);
    vec3 c = s.rgb;
    c = hueRotate(c, hue);
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, saturation);
    c = (c - 0.5) * contrast + 0.5 + brightness;
    c = pow(clamp(c, 0.0, 1.0), vec3(1.0 / gamma));
    return vec4(c, s.a);
})"));

    v.push_back(make("builtin.rgbsplit", "Tách kênh RGB", "Lệch kênh đỏ và xanh dương theo một hướng (kiểu glitch).", EffectDef::Effect,
        {pf("amount", "Độ lệch (px)", 0, 50, 6), pf("angle", "Góc (độ)", 0, 360, 0)}, R"(
vec4 effect(vec2 uv) {
    float a = radians(angle);
    vec2 o = vec2(cos(a), sin(a)) * amount / uSize;
    vec4 g = sampleInput(uv);
    return vec4(sampleInput(uv + o).r, g.g, sampleInput(uv - o).b, g.a);
})"));

    v.push_back(make("builtin.pixelate", "Vỡ hạt (pixelate)", "Gộp điểm ảnh thành ô vuông.", EffectDef::Effect,
        {pf("size", "Cỡ ô (px)", 1, 200, 16)}, R"(
vec4 effect(vec2 uv) {
    vec2 cell = vec2(max(size, 1.0)) / uSize;
    return sampleInput((floor(uv / cell) + 0.5) * cell);
})"));

    v.push_back(make("builtin.noise", "Nhiễu hạt (film grain)", "Thêm nhiễu hạt thay đổi theo thời gian.", EffectDef::Effect,
        {pf("amount", "Cường độ", 0, 1, 0.2), pf("speed", "Tốc độ", 0, 10, 1)}, R"(
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
vec4 effect(vec2 uv) {
    vec4 s = sampleInput(uv);
    float h = hash(uv * uSize + floor(uTime * speed * 24.0));
    return vec4(clamp(s.rgb + (h - 0.5) * amount, 0.0, 1.0), s.a);
})"));

    // ---- Transition ----
    const char* dirCode = R"(
vec2 dirVec() {
    if (direction == 0) return vec2(-1.0, 0.0);
    if (direction == 1) return vec2(1.0, 0.0);
    if (direction == 2) return vec2(0.0, 1.0);
    return vec2(0.0, -1.0);
}
bool inside(vec2 p) { return p.x >= 0.0 && p.x <= 1.0 && p.y >= 0.0 && p.y <= 1.0; }
)";
    const QStringList dirs = {"Trái", "Phải", "Lên", "Xuống"};

    v.push_back(make("builtin.push", "Đẩy (push)", "Hình mới đẩy hình cũ ra khỏi khung.", EffectDef::Transition,
        {pc("direction", "Hướng", dirs)}, (QByteArray(dirCode) + R"(
vec4 transition(vec2 uv) {
    vec2 d = dirVec();
    vec2 a = uv - d * uProgress;
    if (inside(a)) return sampleFrom(a);
    return sampleTo(uv - d * (uProgress - 1.0));
})").constData()));

    v.push_back(make("builtin.slide", "Trượt đè (slide)", "Hình mới trượt đè lên hình cũ đứng yên.", EffectDef::Transition,
        {pc("direction", "Hướng", dirs)}, (QByteArray(dirCode) + R"(
vec4 transition(vec2 uv) {
    vec2 d = dirVec();
    vec2 b = uv - d * (uProgress - 1.0);
    if (inside(b)) return sampleTo(b);
    return sampleFrom(uv);
})").constData()));

    v.push_back(make("builtin.zoom", "Phóng to (zoom)", "Hình cũ phóng to rồi hoà vào hình mới.", EffectDef::Transition,
        {pf("strength", "Độ phóng", 0, 3, 1)}, R"(
vec4 transition(vec2 uv) {
    float s = 1.0 + strength * uProgress;
    vec2 a = (uv - 0.5) / s + 0.5;
    return mix(sampleFrom(a), sampleTo(uv), smoothstep(0.0, 1.0, uProgress));
})"));

    v.push_back(make("builtin.luma", "Quét theo mẫu (luma wipe)", "Hình mới hiện dần theo một mẫu độ sáng.", EffectDef::Transition,
        {pc("pattern", "Mẫu", {"Xuyên tâm", "Ngang", "Chéo", "Bàn cờ", "Cánh cửa"}), pf("softness", "Độ mềm", 0.01, 0.5, 0.1)}, R"(
float lumaMask(vec2 uv) {
    if (pattern == 0) return clamp(length(uv - 0.5) * 1.41421, 0.0, 1.0);
    if (pattern == 1) return uv.x;
    if (pattern == 2) return (uv.x + uv.y) * 0.5;
    if (pattern == 3) return mod(floor(uv.x * 8.0) + floor(uv.y * 8.0), 2.0) * 0.5 + uv.x * 0.5;
    return fract(uv.x * 10.0);
}
vec4 transition(vec2 uv) {
    float m = lumaMask(uv);
    float t = smoothstep(m, m + softness, uProgress * (1.0 + softness));
    return mix(sampleFrom(uv), sampleTo(uv), t);
})"));

    return v;
}

} // namespace lc
