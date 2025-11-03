#version 450

layout (location = 0) in vec3 inTexCoord;

layout (location = 0) out vec4 outFragColor;

// Cubemap sampler for skybox
layout(set = 1, binding = 0) uniform samplerCube skyboxTexture;

void main()
{
    // Sample the cubemap using the direction vector
    outFragColor = texture(skyboxTexture, inTexCoord);
}
