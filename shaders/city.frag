//-----------------------------------------------------------------------------
// Fragment shader for the dead city scene: moonlight (directional), up to
// MAX_POINT_LIGHTS street lamps (point), the flashlight (spot), distance fog
// and alpha cutout for the grass.
//-----------------------------------------------------------------------------
#version 330 core

struct Material
{
	vec3 ambient;
	sampler2D diffuseMap;
	vec3 specular;
	float shininess;
};

struct DirectionalLight
{
	vec3 direction;
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
};

struct PointLight
{
	vec3 position;
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;

	float constant;
	float linear;
	float exponent;

	// Street lamps only light downwards (the lantern roof blocks the rest):
	// cosine, measured from straight down, where the light fades out.
	// Below -1 the light shines in all directions.
	float cosCone;
};

struct SpotLight
{
	vec3 position;
	vec3 direction;
	float cosInnerCone;
	float cosOuterCone;
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
	int on;

	float constant;
	float linear;
	float exponent;
};

in vec2 TexCoord;
in vec3 FragPos;
in vec3 Normal;
in vec3 VertexColor;

#define MAX_POINT_LIGHTS 64

uniform DirectionalLight sunLight;
uniform PointLight pointLights[MAX_POINT_LIGHTS];
uniform int numPointLights;
uniform SpotLight spotLight;
uniform Material material;
uniform vec3 viewPos;

uniform vec3 fogColor;
uniform float fogDensity;
uniform float alphaCutout;	// texels with a lower alpha are discarded (grass)

out vec4 frag_color;

vec3 blinnPhong(vec3 lightDir, vec3 diffuseColor, vec3 specularColor, vec3 normal, vec3 viewDir, vec3 albedo)
{
	float NdotL = max(dot(normal, lightDir), 0.0);
	vec3 halfDir = normalize(lightDir + viewDir);
	float NdotH = max(dot(normal, halfDir), 0.0);
	return diffuseColor * NdotL * albedo + specularColor * material.specular * pow(NdotH, material.shininess);
}

void main()
{
	vec4 texel = texture(material.diffuseMap, TexCoord);
	if (texel.a < alphaCutout)
		discard;

	vec3 albedo = texel.rgb * VertexColor;
	vec3 normal = normalize(Normal);
	vec3 viewDir = normalize(viewPos - FragPos);

	// Moonlight
	vec3 color = sunLight.ambient * albedo;
	color += blinnPhong(normalize(-sunLight.direction), sunLight.diffuse, sunLight.specular, normal, viewDir, albedo);

	// Street lamps
	for (int i = 0; i < numPointLights; i++)
	{
		PointLight light = pointLights[i];
		vec3 toLight = light.position - FragPos;
		float d = length(toLight);
		float attenuation = 1.0 / (light.constant + light.linear * d + light.exponent * d * d);
		// toLight.y / d = cosine of the angle between "down" and the light->fragment direction
		if (light.cosCone >= -1.0)
			attenuation *= smoothstep(light.cosCone, light.cosCone + 0.35, toLight.y / d);
		color += (light.ambient * albedo + blinnPhong(toLight / d, light.diffuse, light.specular, normal, viewDir, albedo)) * attenuation;
	}

	// Flashlight
	if (spotLight.on == 1)
	{
		vec3 toLight = spotLight.position - FragPos;
		float d = length(toLight);
		vec3 lightDir = toLight / d;
		float spotIntensity = smoothstep(spotLight.cosOuterCone, spotLight.cosInnerCone, dot(-lightDir, normalize(spotLight.direction)));
		float attenuation = 1.0 / (spotLight.constant + spotLight.linear * d + spotLight.exponent * d * d);
		color += blinnPhong(lightDir, spotLight.diffuse, spotLight.specular, normal, viewDir, albedo) * attenuation * spotIntensity;
	}

	float fogAmount = fogDensity * length(viewPos - FragPos);
	frag_color = vec4(mix(fogColor, color, exp(-fogAmount * fogAmount)), 1.0);
}
