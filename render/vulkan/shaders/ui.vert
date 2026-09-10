#version 450
// render::2d vertex: pixel position -> clip space via push-constant scale/translate.

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor; // R8G8B8A8_UNORM

layout(push_constant) uniform Push {
  vec2 scale;
  vec2 translate;
} pc;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 color;

void main() {
  uv = inUV;
  color = inColor;
  gl_Position = vec4(inPos * pc.scale + pc.translate, 0.0, 1.0);
}
