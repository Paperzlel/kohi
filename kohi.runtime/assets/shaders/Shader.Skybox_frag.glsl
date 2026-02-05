#version 450

const uint KMATERIAL_UBO_MAX_VIEWS = 16;
#define KMATERIAL_MAX_WATER_PLANES 4
// One view for regular camera, plus one reflection view per water plane.
#define KMATERIAL_MAX_VIEWS (KMATERIAL_MAX_WATER_PLANES + 1)

// =========================================================
// Inputs
// =========================================================

layout(set = 0, binding = 0) uniform global_ubo_data {
    mat4 views[KMATERIAL_UBO_MAX_VIEWS];
    mat4 projection;
	vec4 fog_colour;
} global_ubo;

layout(set = 0, binding = 1) uniform textureCube cube_texture;
layout(set = 0, binding = 2) uniform sampler cube_sampler;

layout(push_constant) uniform immediate_data {
    uint view_index;
} immediate;

// Data Transfer Object from vertex shader.
layout(location = 0) in dto {
	vec4 frag_pos;
	vec3 tex_coord;
} in_dto;

// =========================================================
// Outputs
// =========================================================
layout(location = 0) out vec4 out_colour;

void main() {
    out_colour = texture(samplerCube(cube_texture, cube_sampler), in_dto.tex_coord);

	float min_fog_y = 0.0;
	float max_fog_y = 0.02;
	float fog_factor = smoothstep(max_fog_y, min_fog_y, in_dto.frag_pos.y);

	vec3 final_colour = mix(out_colour.rgb, global_ubo.fog_colour.rgb, fog_factor * global_ubo.fog_colour.a);

	out_colour = vec4(final_colour, 1.0);

} 
