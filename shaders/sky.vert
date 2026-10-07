#version 330 core
layout (location = 0) in vec3 aPos;

out vec3 TexCoords;

uniform mat4 projection;
uniform mat4 view;

void main()
{
	TexCoords = aPos;
	// Remove camera translation so skybox is always centered at camera
	mat4 viewNoTranslation = mat4(mat3(view));
	vec4 pos = projection * viewNoTranslation * vec4(aPos, 1.0);
	// Putting z equal to w ensures that in NDC depth is 1.0 (far plane)
	gl_Position = pos.xyww;
}
