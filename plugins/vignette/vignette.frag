vec4 effect(vec2 uv) {
    vec4 c = sampleInput(uv);
    float d = length((uv - 0.5) * vec2(uSize.x / uSize.y, 1.0));
    float v = smoothstep(radius, radius + 0.5, d) * strength;
    return vec4(mix(c.rgb, tint.rgb, v), c.a);
}
