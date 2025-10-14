#version 450

//shader input
layout (location = 0) in vec3 inColor;

//push constants block
layout( push_constant ) uniform constants
{
 vec3 color;
} PushConstants;

//output write
layout (location = 0) out vec4 outFragColor;

void main() 
{
	//return red
	outFragColor = vec4(inColor * PushConstants.color,1.0f);
}