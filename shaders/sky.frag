#version 330 core

in vec3 TexCoords;
out vec4 frag_color;

uniform vec3 fogColor;
uniform vec3 moonDir;
uniform float time;

// Fast 3D hash
float hash(vec3 p)
{
	p = fract(p * 0.3183099 + vec3(0.1, 0.1, 0.1));
	p *= 17.0;
	return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// 3D smooth noise
float noise(vec3 x)
{
	vec3 i = floor(x);
	vec3 f = fract(x);
	f = f * f * (3.0 - 2.0 * f);

	return mix(mix(mix(hash(i + vec3(0.0, 0.0, 0.0)), hash(i + vec3(1.0, 0.0, 0.0)), f.x),
	               mix(hash(i + vec3(0.0, 1.0, 0.0)), hash(i + vec3(1.0, 1.0, 0.0)), f.x), f.y),
	           mix(mix(hash(i + vec3(0.0, 0.0, 1.0)), hash(i + vec3(1.0, 0.0, 1.0)), f.x),
	               mix(hash(i + vec3(0.0, 1.0, 1.0)), hash(i + vec3(1.0, 1.0, 1.0)), f.x), f.y), f.z);
}

// Fractal Brownian Motion
float fbm(vec3 p)
{
	float v = 0.0;
	float a = 0.5;
	for (int i = 0; i < 4; i++)
	{
		v += a * noise(p);
		p = p * 2.02 + vec3(1.7, 9.2, 0.4);
		a *= 0.5;
	}
	return v;
}

// Procedural stars with twinkling
vec3 stars(vec3 dir, float t)
{
	vec3 starLight = vec3(0.0);

	// Layer 1 - sharp bright stars
	vec3 p1 = dir * 160.0;
	vec3 id1 = floor(p1);
	vec3 f1 = fract(p1) - 0.5;
	float h1 = hash(id1);
	if (h1 > 0.94)
	{
		vec3 center = (vec3(hash(id1 + 11.0), hash(id1 + 23.0), hash(id1 + 37.0)) - 0.5) * 0.65;
		float d = length(f1 - center);
		float twinkle = sin(t * (2.0 + 5.0 * hash(id1 + 5.0)) + hash(id1 + 7.0) * 6.28) * 0.5 + 0.5;
		float b = smoothstep(0.075, 0.0, d) * (0.6 + 0.4 * twinkle);
		vec3 starCol = mix(vec3(0.7, 0.85, 1.0), vec3(1.0, 0.92, 0.75), hash(id1 + 3.0));
		starLight += starCol * b * 1.2;
	}

	// Layer 2 - denser small faint stars
	vec3 p2 = dir * 320.0;
	vec3 id2 = floor(p2);
	vec3 f2 = fract(p2) - 0.5;
	float h2 = hash(id2);
	if (h2 > 0.965)
	{
		vec3 center2 = (vec3(hash(id2 + 13.0), hash(id2 + 29.0), hash(id2 + 41.0)) - 0.5) * 0.6;
		float d2 = length(f2 - center2);
		float b2 = smoothstep(0.09, 0.0, d2) * 0.45;
		starLight += vec3(0.8, 0.9, 1.0) * b2;
	}

	return starLight;
}

void main()
{
	vec3 dir = normalize(TexCoords);

	// -------------------------------------------------------------
	// GESTION DU BROUILLARD ATMOSPHÉRIQUE :
	// 1. Sous et à l'horizon (dir.y <= 0.05), le brouillard est 100% opaque
	//    et identique à fogColor. Ainsi, le sol qui se termine à distance
	//    se fond de manière invisible et continue sans aucune coupure.
	// 2. Les bâtiments lointains estompés par le brouillard de city.frag
	//    se fondent exactement dans cette même couleur.
	// 3. Entre dir.y = 0.05 et 0.35, la couche de brume se dissipe
	//    progressivement vers le ciel nocturne étoilé.
	// -------------------------------------------------------------
	float fogFactor = 1.0;
	if (dir.y > 0.05)
	{
		fogFactor = 1.0 - smoothstep(0.05, 0.28, dir.y);
	}

	// Ciel de nuit profond (au-delà du brouillard)
	vec3 zenithColor = vec3(0.025, 0.04, 0.10); // bleu nuit
	float zenithT = pow(clamp(dir.y, 0.0, 1.0), 0.45);
	vec3 nightSky = mix(fogColor * 0.8, zenithColor, zenithT);

	// Voiles nuageux légers dans la haute atmosphère
	float cloudMask = 0.0;
	if (dir.y > 0.08)
	{
		vec3 cloudCoord = dir / max(dir.y, 0.15) * 0.75;
		cloudCoord.x += time * 0.005;
		cloudCoord.z += time * 0.002;
		float cloudVal = fbm(cloudCoord * 1.6);
		cloudMask = smoothstep(0.50, 0.80, cloudVal);

		float moonProximity = max(dot(dir, moonDir), 0.0);
		vec3 cloudBase = vec3(0.05, 0.07, 0.11);
		vec3 cloudLit  = vec3(0.17, 0.21, 0.31) + pow(moonProximity, 12.0) * 0.2;
		vec3 cloudColor = mix(cloudBase, cloudLit, cloudMask);

		float cloudFade = smoothstep(0.08, 0.25, dir.y) * 0.65;
		nightSky = mix(nightSky, cloudColor, cloudMask * cloudFade);
	}

	// Étoiles (visibles uniquement là où le brouillard est dissipé)
	if (fogFactor < 1.0)
	{
		vec3 starLight = stars(dir, time);
		nightSky += starLight * 1.4 * (1.0 - cloudMask * 0.7);
	}

	// Rendu de la Lune
	float moonDot = dot(dir, moonDir);
	if (moonDot > 0.0)
	{
		// Halo lunaire doux
		float glowInner = pow(moonDot, 512.0) * 0.70;
		float glowMid   = pow(moonDot, 64.0)  * 0.22;
		float glowOuter = pow(moonDot, 10.0)  * 0.08;
		vec3 moonGlow = vec3(0.75, 0.85, 1.0);
		nightSky += (glowInner + glowMid + glowOuter) * moonGlow;
	}

	// Disque de la Lune (~2.2 degrés)
	float moonRadiusCos = 0.99926;
	float moonEdge = 0.00035;
	float moonDisc = smoothstep(moonRadiusCos - moonEdge, moonRadiusCos, moonDot);
	if (moonDisc > 0.0)
	{
		vec3 mOffset = dir - moonDir;
		float maria = fbm(mOffset * 150.0 + vec3(5.1, 2.3, 1.7));
		vec3 moonSurface = mix(vec3(0.72, 0.78, 0.85), vec3(0.98, 0.98, 1.0), maria);
		nightSky = mix(nightSky, moonSurface * 1.4, moonDisc);
	}

	// Fusion finale : le brouillard masque parfaitement le ciel près de l'horizon
	vec3 finalColor = mix(nightSky, fogColor, fogFactor);

	frag_color = vec4(finalColor, 1.0);
}
