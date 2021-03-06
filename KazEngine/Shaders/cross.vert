#version 450

layout (location = 0) in vec3 inPos;

layout (set=0, binding=0) uniform Camera
{
	mat4 projection;
	mat4 view;
} camera;

layout (set=1, binding=0) buffer ID
{
	uint entity_id[];
};

struct Properties {
	mat4 model;
	uint animation_id;
	uint frame_id;
	// ivec2 padding;
};

layout (set=1, binding=1) buffer Entity
{
	Properties entity[];
};


void main() 
{
	gl_Position = camera.projection * camera.view * entity[entity_id[gl_InstanceIndex]].model * vec4(inPos, 1.0);
}