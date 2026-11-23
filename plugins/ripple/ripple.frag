vec4 effect(vec2 uv) {
    vec2 d = uv - center;
    float r = length(d);
    float t = animate ? uTime * speed : 0.0;
    vec2 offset = normalize(d + 1e-5) * sin(r * frequency - t) * amplitude;
    return sampleInput(uv + offset);
}
