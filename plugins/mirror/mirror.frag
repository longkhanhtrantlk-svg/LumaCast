vec4 effect(vec2 uv) {
    vec2 p = uv;
    if (mode == 0 || mode == 2) p.x = p.x > 0.5 ? 1.0 - p.x : p.x;
    if (mode == 1 || mode == 2) p.y = p.y > 0.5 ? 1.0 - p.y : p.y;
    return sampleInput(p);
}
