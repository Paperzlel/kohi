#version 450

layout(location = 0) in vec4 in_position;
layout(location = 1) in vec4 in_normal;
layout(location = 4) in vec4 in_tangent;

#define MAX_CASCADES 4

layout(set = 0, binding = 0) uniform global_ubo_data {
    mat4 view_projections[MAX_CASCADES];
} global_ubo;

layout(push_constant) uniform immediate_data {
	// Only guaranteed a total of 128 bytes.
    uint cascade_index;
} immediate;

void main() {
    gl_Position = global_ubo.view_projections[immediate.cascade_index] * vec4(in_position.xyz, 1.0);
}
