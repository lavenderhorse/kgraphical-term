#version 440

// One instance per terminal cell (or per decoration rectangle).
layout(location = 0) in vec2 corner;  // unit quad corner, 0..1
layout(location = 1) in vec4 rect;    // x, y, width, height in device pixels
layout(location = 2) in vec4 uv;      // u0, v0, u1, v1 in the glyph atlas (u1 == u0: no glyph)
layout(location = 3) in vec4 fg;      // rgb = foreground, a = 1 for mono glyph, 0 for colour glyph
layout(location = 4) in vec4 bg;      // rgb = background

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_fg;
layout(location = 2) out vec4 v_bg;
layout(location = 3) out float v_hasGlyph;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
};

out gl_PerVertex {
    vec4 gl_Position;
};

void main()
{
    vec2 pos = rect.xy + corner * rect.zw;
    gl_Position = mvp * vec4(pos, 0.0, 1.0);
    v_uv = mix(uv.xy, uv.zw, corner);
    v_fg = fg;
    v_bg = bg;
    v_hasGlyph = (uv.z > uv.x) ? 1.0 : 0.0;
}
