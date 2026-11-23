vec4 transition(vec2 uv) {
    vec2 d = uv - 0.5;
    float a = fract(atan(d.x, d.y) / 6.2831853);     // 0..1 quanh tâm, bắt đầu từ đỉnh, chạy theo chiều kim đồng hồ
    if (!clockwise) a = 1.0 - a;
    float t = smoothstep(a, a + softness, uProgress * (1.0 + softness));
    return mix(sampleFrom(uv), sampleTo(uv), t);
}
