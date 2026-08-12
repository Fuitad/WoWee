#version 450

// Full-screen colour overlay (underwater tint) with a waterline.
// Uses postprocess.vert.glsl as vertex shader (fullscreen triangle, no vertex input).

layout(push_constant) uniform Push {
    vec4 color;   // rgb = tint colour, a = opacity
    vec4 params;  // x = waterline height in NDC (-1 bottom .. 1 top), y = softness,
                  // z = ripple amplitude, w = time
} push;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    // A camera crossing the surface used to switch between above and below in one
    // step. Sweeping a boundary across the view instead reads as the waterline
    // rising past the lens, and rippling it keeps the edge from looking like a
    // straight cut. Fully submerged pushes the line off the top, so the whole
    // screen tints exactly as before.
    const float ndcY = vUV.y * 2.0 - 1.0;
    const float ripple = sin(vUV.x * 34.0 + push.params.w * 2.1) * 0.55
                       + sin(vUV.x * 71.0 - push.params.w * 3.3) * 0.45;
    const float line = push.params.x + ripple * push.params.z;
    // Below the line is underwater. The softness spans the meniscus, where the
    // surface is edge-on and neither side reads cleanly.
    // TexCoord.y = 0 is the top of the framebuffer, so larger ndcY is lower on
    // screen - which is the submerged side.
    const float submerged = smoothstep(line - push.params.y, line + push.params.y, ndcY);

    outColor = vec4(push.color.rgb, push.color.a * submerged);
}
