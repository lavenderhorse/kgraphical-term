#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_fg;
layout(location = 2) in vec4 v_bg;
layout(location = 3) in float v_hasGlyph;

layout(location = 0) out vec4 fragColor;

// Premultiplied RGBA glyph atlas. Mono glyphs are white, so only alpha matters.
layout(binding = 1) uniform sampler2D atlas;

void main()
{
    vec4 t = texture(atlas, v_uv) * v_hasGlyph;

    // Monochrome glyph: tint with the cell foreground.
    vec3 mono = mix(v_bg.rgb, v_fg.rgb, t.a);
    // Colour glyph (emoji): composite the premultiplied texel over the background.
    vec3 colour = v_bg.rgb * (1.0 - t.a) + t.rgb;

    float isColour = 1.0 - v_fg.a;
    fragColor = vec4(mix(mono, colour, isColour), 1.0);
}
