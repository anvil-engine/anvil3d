#version 450
// render::Draw3D: (texture, or texture/texture2 blended by vertex weight) * lightmap * colorScale, optional alpha test.

layout(set = 0, binding = 0) uniform sampler2D tex;
layout(set = 1, binding = 0) uniform sampler2D lightmap;
layout(set = 2, binding = 0) uniform sampler2D tex2;

layout(push_constant) uniform Push {
  layout(offset = 64) vec4 params; // x: color scale, y: alpha reference (0 = no test), z: 1 = blend tex2
} pc;

layout(location = 0) in vec2 uv;
layout(location = 1) in vec2 lightmapUV;
layout(location = 2) in float blend;
layout(location = 0) out vec4 outColor;

void main() {
  vec4 base = texture(tex, uv);
  if (pc.params.z > 0.0) base = mix(base, texture(tex2, uv), clamp(blend, 0.0, 1.0));
  if (base.a < pc.params.y) discard;
  outColor = vec4(base.rgb * texture(lightmap, lightmapUV).rgb * pc.params.x, base.a);
}
