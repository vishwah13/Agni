#version 450

layout (location = 0) in vec3 inColor;

layout (location = 0) out vec4 outFragColor;

void main()
{
	// Solid magenta color indicates fallback shader is active
	// This is an obvious error color so developers know the real shader failed to load
	outFragColor = vec4(1.0, 0.0, 1.0, 1.0);
}
