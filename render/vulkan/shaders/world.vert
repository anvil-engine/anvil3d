#version 450
// render::Draw3D vertex: world position -> clip space. API clip space is y-up; Vulkan's is y-down.

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec2 inLightmapUV;
layout(location = 3) in float inBlend;

layout(push_constant) uniform Push {
  mat4 viewProj;
} pc;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec2 lightmapUV;
layout(location = 2) out float blend;

void main() {
  uv = inUV;
  lightmapUV = inLightmapUV;
  blend = inBlend;
  gl_Position = pc.viewProj * vec4(inPos, 1.0);
  gl_Position.y = -gl_Position.y;
}
