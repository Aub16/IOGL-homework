//-----------------------------------------------------------------------------
// Fragment shader for laser scan line
// Ultra-thin planar laser fan with smooth distance dissolution (soft & subtle)
//-----------------------------------------------------------------------------
#version 330 core
in vec3 LocalPos;
in vec2 TexCoord;

uniform vec3 scanColor;
uniform float time;
uniform float intensity;
uniform float beamLength;

out vec4 frag_color;

void main()
{
	// Distance along the beam normalized: 0.0 at camera eye -> 1.0 at maximum range
	float zNorm = clamp(LocalPos.z / beamLength, 0.0, 1.0);

	// Dissolution avec la distance :
	// Forte attenuation progressive pour un faisceau diaphane et discret
	float distDissolve = exp(-zNorm * 3.8);

	// Fondu au ras de la lentille
	float startFade = smoothstep(0.005, 0.04, zNorm);

	// Profil lateral le long de la ligne laser (0 au centre, 1 aux bords gauche/droite)
	float xDist = abs(TexCoord.x - 0.5) * 2.0;
	float fanProfile = smoothstep(1.0, 0.25, xDist);

	// Epaisseur de la ligne : ultra-fine
	float ySpread = 0.006 + zNorm * 0.012;
	float lineSharpness = exp(-pow(LocalPos.y / ySpread, 2.0));

	// Balayage discret
	float sweep = exp(-pow((fract(time * 0.50) - zNorm) * 14.0, 2.0)) * 0.8;

	// Alpha doux et mesure (non aveuglant, beaucoup moins prononce)
	float alpha = (lineSharpness * 0.45 + sweep * 0.25) * fanProfile * distDissolve * startFade * intensity;

	// Teinte cyan delicate, legerement plus claire au coeur sans saturation excessive
	vec3 coreColor = mix(scanColor * 0.75, vec3(0.75, 0.92, 1.0), lineSharpness * 0.35);

	// Pre-multiplication par alpha pour un melange additif doux
	frag_color = vec4(coreColor * alpha, alpha);
}
