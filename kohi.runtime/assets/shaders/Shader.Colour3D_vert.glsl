#version 450

layout(location = 0) in vec4 in_position; // NOTE: w is ignored.
layout(location = 1) in vec4 in_colour;

layout(set = 0, binding = 0) uniform global_uniform_data {
    mat4 projection;
	mat4 view;
} global_ubo;

layout(push_constant) uniform immediate_data {
	
	// Only guaranteed a total of 128 bytes.
	mat4 model; // 64 bytes
} immediate;


// Data Transfer Object
layout(location = 1) out struct dto {
	vec4 colour;
} out_dto;

void main() {
	out_dto.colour = in_colour;
	gl_Position = global_ubo.projection * global_ubo.view * immediate.model * vec4(in_position.xyz, 1.0);
	gl_PointSize = 10.0;
}
