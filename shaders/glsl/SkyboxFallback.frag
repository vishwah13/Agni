#version 450

layout (location = 0) in vec3 inTexCoord;

layout (location = 0) out vec4 outFragColor;

void main()
{
    // Solid sky blue color indicates fallback shader is active
    // Better than crashing when skybox cubemap fails to load
    outFragColor = vec4(0.5, 0.7, 1.0, 1.0);
}
