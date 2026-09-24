#version 330 core
// Draws a band `uRadius` pixels wide just outside every selected silhouette in the mask.
//
// Each pixel looks at the mask within a circle of that radius: a pixel that is itself selected is
// left alone (the outline sits outside the object, never over it), and one with a selected pixel
// nearby takes the outline colour. Alpha falls off over the last half pixel of the band, so its
// outer edge is smooth rather than stepped, and carries the strongest selection found - the
// active node's outline is full strength, the rest of a multi-selection dimmer.
uniform sampler2D uMask;
uniform int uRadius;
uniform vec3 uColor;
out vec4 fragColor;

void main() {
    ivec2 size = textureSize(uMask, 0);
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    if (texelFetch(uMask, clamp(pixel, ivec2(0), size - 1), 0).r > 0.0) {
        discard;
    }
    float strongest = 0.0;
    float nearest = 1.0e9;
    for (int dy = -uRadius; dy <= uRadius; ++dy) {
        for (int dx = -uRadius; dx <= uRadius; ++dx) {
            ivec2 probe = pixel + ivec2(dx, dy);
            if (any(lessThan(probe, ivec2(0))) || any(greaterThanEqual(probe, size))) {
                continue;
            }
            float weight = texelFetch(uMask, probe, 0).r;
            if (weight > 0.0) {
                strongest = max(strongest, weight);
                nearest = min(nearest, length(vec2(dx, dy)));
            }
        }
    }
    float coverage = clamp(float(uRadius) + 0.5 - nearest, 0.0, 1.0);
    if (strongest <= 0.0 || coverage <= 0.0) {
        discard;
    }
    fragColor = vec4(uColor * mix(0.7, 1.0, strongest), strongest * coverage);
}
