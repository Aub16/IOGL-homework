//-----------------------------------------------------------------------------
// Glow halo: bright in the center, fading smoothly to nothing at the edge.
// Drawn with additive blending, so it only ever brightens what is behind it.
//-----------------------------------------------------------------------------
#version 330 core

in vec2 TexCoord;

uniform vec3 glowColor;
uniform float intensity;

out vec4 frag_color;

void main()
{
	float d = length(TexCoord * 2.0 - 1.0);	// 0 in the center, 1 at the edge
	float falloff = pow(max(1.0 - d, 0.0), 2.5);
	frag_color = vec4(glowColor * falloff * intensity, 1.0);
}
