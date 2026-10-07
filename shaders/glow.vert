//-----------------------------------------------------------------------------
// Glow halo: a quad always facing the camera (billboard), centered on a light
//-----------------------------------------------------------------------------
#version 330 core

layout (location = 0) in vec3 pos;		// quad corner, in [-1, 1]
layout (location = 2) in vec2 texCoord;

uniform vec3 center;		// world position of the glow
uniform float size;			// half size of the quad, world units
uniform vec3 cameraRight;
uniform vec3 cameraUp;
uniform mat4 view;
uniform mat4 projection;

out vec2 TexCoord;

void main()
{
	vec3 worldPos = center + (cameraRight * pos.x + cameraUp * pos.y) * size;
	TexCoord = texCoord;
	gl_Position = projection * view * vec4(worldPos, 1.0);
}
