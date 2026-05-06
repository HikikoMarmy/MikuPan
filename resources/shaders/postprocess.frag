#version 330 core

in vec2 vUV;
in vec4 uColor;

out vec4 FragColor;

uniform sampler2D uTexture;
uniform float uBrightness;
uniform float uGamma;

// Final scene-to-window pass. Samples the resolved scene texture, applies a
// linear brightness multiplier, then a gamma curve via pow(col, 1/gamma).
// max(col, 0) guards against negatives that NaN under non-integer pow;
// max(uGamma, 0.01) guards against div-by-zero if the slider lands on zero.
void main()
{
    vec4 col = texture(uTexture, vUV) * uColor;
    col.rgb *= uBrightness;
    col.rgb = pow(max(col.rgb, vec3(0.0)), vec3(1.0 / max(uGamma, 0.01)));
    FragColor = col;
}
