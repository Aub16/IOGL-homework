//-----------------------------------------------------------------------------
// "Abandoned tower" scene, love-death-and-robots style ("Three Robots"):
// The robot trio—XBOT 4000, LittleBot (K-VRC) perched on a crate, and the
// grounded pyramidal droid 11-45-G—stand on an upper floor of Tower_Block_abandoned,
// contemplating a preserved dead human like an exhibit. Down below, a dead city:
// cracked roads with sidewalks overgrown with weeds, dead street lamps, and ruined
// buildings. Night falls: moonlight, old street lamps (some flickering, some
// dead) and fog swallowing the horizon.
//-----------------------------------------------------------------------------
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <random>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <cstdio>
#define GLEW_STATIC
#include "GL/glew.h"	// Important - this header must come before glfw3 header
#include "GLFW/glfw3.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/constants.hpp"

#include "ShaderProgram.h"
#include "Texture2D.h"
#include "Camera.h"
#include "Mesh.h"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

#ifdef _WIN32
// On laptops with hybrid graphics (an integrated GPU + a discrete NVIDIA/AMD
// one), Windows otherwise tends to launch this on the weaker integrated GPU.
// Exporting these symbols is how the NVIDIA/AMD drivers pick the discrete
// card automatically for this executable, no manual settings needed.
extern "C"
{
	__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

// Global Variables
const char* APP_TITLE = "Introduction to Modern OpenGL - All Models Showcase";
int gWindowWidth = 1280;
int gWindowHeight = 800;
GLFWwindow* gWindow = NULL;
bool gWireframe = false;
bool gFlashlightOn = true;
glm::vec4 gClearColor(0.06f, 0.08f, 0.14f, 1.0f);	// night sky horizon / fog color

// Fullscreen toggle (F11) - the cursor is captured by the app, so the window
// manager's own maximize button/decorations are not reachable
bool gFullscreen = false;
int gWindowedX = 100, gWindowedY = 100;
int gWindowedWidth = gWindowWidth, gWindowedHeight = gWindowHeight;

// The mouse only gets captured (hidden + used to look around) once the user
// clicks inside the window - until then it behaves like a normal cursor.
// Capturing is deferred to the start of the next update() instead of being
// done directly in the click callback: switching GLFW_CURSOR to DISABLED
// mid-event-processing (inside the click callback itself) left the very
// first cursor reading in a bad state, which was seen as the camera pitch
// snapping and getting stuck looking up no matter which way the mouse moved.
bool gMouseCaptured = false;
bool gMouseCaptureRequested = false;

FPSCamera fpsCamera(glm::vec3(-1.0f, 3.0f, 9.0f));
const double ZOOM_SENSITIVITY = -3.0;
const float MOVE_SPEED = 5.0f; // units per second
const float SPRINT_FACTOR = 3.0f; // speed multiplier when Shift is held
const float MOUSE_SENSITIVITY = 0.02f;

// Function prototypes
void glfw_onKey(GLFWwindow* window, int key, int scancode, int action, int mode);
void glfw_onMouseButton(GLFWwindow* window, int button, int action, int mods);
void glfw_onFramebufferSize(GLFWwindow* window, int width, int height);
void glfw_onMouseScroll(GLFWwindow* window, double deltaX, double deltaY);
void update(double elapsedTime);
void showFPS(GLFWwindow* window);
void toggleFullscreen();
bool initOpenGL();

//-----------------------------------------------------------------------------
// Automatic demo (used to film the presentation video)
//   Scene_AllModels.exe --demo [camera_path.txt] [--record out.mp4] [--size WxH]
// --demo   : the camera follows the keyframes of the path file, inputs are ignored
// --record : frames are rendered at a fixed 60 FPS clock (not real time) and piped
//            to FFmpeg, so the video is perfectly smooth and reproducible
//-----------------------------------------------------------------------------
struct DemoKey { float t; glm::vec3 pos, target; float fov; int flash, wire; };
bool gDemo = false;
std::string gDemoPath = "video/camera_path.txt";
std::string gRecordFile;
const double RECORD_FPS = 60.0;
std::vector<DemoKey> gDemoKeys;

bool loadDemoPath(const std::string& path)
{
	std::ifstream in(path);
	if (!in) { std::cerr << "Cannot open demo path " << path << std::endl; return false; }
	std::string line;
	while (std::getline(in, line))
	{
		if (line.empty() || line[0] == '#') continue;
		std::istringstream ls(line);
		DemoKey k;
		if (ls >> k.t >> k.pos.x >> k.pos.y >> k.pos.z >> k.target.x >> k.target.y >> k.target.z >> k.fov >> k.flash >> k.wire)
			gDemoKeys.push_back(k);
	}
	return !gDemoKeys.empty();
}

// Catmull-Rom spline through p1..p2 (p0 and p3 are the neighbours): the camera
// glides through the keyframes without stopping at each one
glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float u)
{
	return 0.5f * ((2.0f * p1) + (-p0 + p2) * u + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * u * u + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * u * u * u);
}

// Smooth 1D value noise in [-1, 1]: a deterministic function of time, so every
// render of the demo has exactly the same "hand shake"
float shakeNoise(float x, float seed)
{
	auto h = [&](float i) { float v = std::sin(i * 127.1f + seed * 311.7f) * 43758.5453f; return (v - std::floor(v)) * 2.0f - 1.0f; };
	float i = std::floor(x), f = x - i;
	f = f * f * (3.0f - 2.0f * f);
	return glm::mix(h(i), h(i + 1.0f), f);
}

// Camera position / target on the spline at time t (no shake)
void sampleDemoPath(double t, glm::vec3& pos, glm::vec3& target, float& fov, int& flash, int& wire)
{
	int n = (int)gDemoKeys.size();
	t = std::min(t, (double)gDemoKeys[n - 1].t - 1e-4);
	int i = 0;
	while (i + 2 < n && gDemoKeys[i + 1].t <= t) i++;
	const DemoKey& a = gDemoKeys[i];
	const DemoKey& b = gDemoKeys[i + 1];
	// Two keys closer than 10 ms are a hard cut: the spline must not blend across it
	const DemoKey* pp0 = &gDemoKeys[std::max(i - 1, 0)];
	const DemoKey* pp3 = &gDemoKeys[std::min(i + 2, n - 1)];
	if (a.t - pp0->t < 0.01f) pp0 = &a;
	if (pp3->t - b.t < 0.01f) pp3 = &b;
	const DemoKey& p0 = *pp0;
	const DemoKey& p3 = *pp3;
	float u = glm::clamp((float)((t - a.t) / (b.t - a.t)), 0.0f, 1.0f);
	pos = catmullRom(p0.pos, a.pos, b.pos, p3.pos, u);
	target = catmullRom(p0.target, a.target, b.target, p3.target, u);
	fov = glm::mix(a.fov, b.fov, u);
	flash = a.flash;
	wire = a.wire;
}

// Places the camera for time t; returns false once the path is over.
// The camera is meant to feel like a person filming, not like a perfect rail: slow
// drift and breathing, and the aim follows the subject with a small delay (like a
// person turning their head towards what they look at).
bool applyDemo(double t)
{
	if (t >= gDemoKeys.back().t) return false;
	glm::vec3 pos, target;
	float fov, lagFov;
	int flash, wire, dummyFlash, dummyWire;
	sampleDemoPath(t, pos, target, fov, flash, wire);
	glm::vec3 posLag, aimLag;	// where the subject was 0.35 s ago: the aim follows it with that delay
	sampleDemoPath(std::max(t - 0.35, 0.0), posLag, aimLag, lagFov, dummyFlash, dummyWire);

	float tt = (float)t;
	// A person holding a camera is not trembling: the body only drifts slowly and
	// breathes (4 s cycle), and the head eases towards what it looks at. No fast jitter.
	pos += glm::vec3(0.03f * shakeNoise(tt * 0.22f, 1.0f), 0.025f * shakeNoise(tt * 0.18f, 2.0f) + 0.012f * std::sin(tt * 1.6f), 0.03f * shakeNoise(tt * 0.2f, 3.0f));
	// the aim lags behind the subject
	glm::vec3 aim = glm::mix(target, aimLag, 0.6f);
	glm::vec3 dir = aim - pos;
	float dist = glm::length(dir);
	// slow, tiny aim drift (fractions of a degree)
	float yawDeg = 0.20f * shakeNoise(tt * 0.30f, 4.0f);
	float pitchDeg = 0.14f * shakeNoise(tt * 0.26f, 6.0f);
	glm::vec3 right = glm::normalize(glm::cross(dir, glm::vec3(0.0f, 1.0f, 0.0f)));
	aim += (right * std::tan(glm::radians(yawDeg)) + glm::vec3(0.0f, std::tan(glm::radians(pitchDeg)), 0.0f)) * dist;

	fpsCamera.setPosition(pos);
	fpsCamera.lookAt(aim);
	fpsCamera.setFOV(fov);
	gFlashlightOn = flash != 0;
	bool wireOn = wire != 0;
	if (wireOn != gWireframe)
	{
		gWireframe = wireOn;
		glPolygonMode(GL_FRONT_AND_BACK, gWireframe ? GL_LINE : GL_FILL);
	}
	return true;
}

//-----------------------------------------------------------------------------
// Dead city generation
//-----------------------------------------------------------------------------
enum MeshId { M_BARREL, M_WOODCRATE, M_CRATE, M_XBOT, M_LAMPPOST, M_LITTLEBOT, M_TOWER, M_SKELETON, M_BOTTLE, M_MASK, NUM_MESHES };

// A mesh drawn somewhere. rot = degrees around X, Y, Z (applied Z, X, then Y)
struct Prop { int mesh; glm::vec3 pos; glm::vec3 scale; glm::vec3 rot; };

// Street grid: roads along X or along Z, each with a sidewalk on both sides
struct Road { bool alongX; float center; float width; };
const Road ROADS[] = {
	{ true,   23.0f, 12.0f },	// main road in front of the tower
	{ true,  -38.0f, 10.0f },	// road behind the tower
	{ false, -30.0f, 10.0f },	// cross street, left
	{ false,  32.0f, 10.0f },	// cross street, right
};
const int NUM_ROADS = sizeof(ROADS) / sizeof(ROADS[0]);
const float CITY_HALF = 150.0f;		// streets run from -CITY_HALF to +CITY_HALF
const float SIDEWALK_W = 3.0f;
const float SIDEWALK_H = 0.2f;
const float ROAD_Y = 0.03f;			// just above the ground to avoid z-fighting
const float ROAD_TEX_LENGTH = 12.0f;	// one road texture repeat along the road
const float SIDEWALK_TEX = 3.0f;	// one sidewalk texture repeat (2x2 slabs)
const float GROUND_TEX = 16.0f;

// Tower_Block_abandoned local bounds (Z-up): used to size the ruined buildings
const float TOWER_SIZE_X = 1.92f, TOWER_SIZE_Y = 1.92f;
const float TOWER_BOTTOM_Z = -1.131f, TOWER_TOP_Z = 1.178f;
// Heights of its big flat floor slabs, measured on the mesh (area of the
// horizontal faces). Below the lowest slab there are only thin broken walls
// hanging down, so a building resting on those looks like it floats:
// buildings rest on a slab instead. Undersides rest on the ground; the roof
// top is what an upside-down building rests on, and what a stacked piece
// rests on.
const float TOWER_LOW_SLAB_Z = -0.353f;		// underside of the lowest floor
const float TOWER_MID_SLAB_Z = 0.178f;		// underside of the middle floor
const float TOWER_MID_FLOOR_TOP_Z = 0.2575f;	// walkable top of the middle floor
const float TOWER_ROOF_SLAB_Z = 0.873f;		// top of the roof
const float SLAB_LIFT = 0.05f;	// keeps a slab resting on the ground from z-fighting with it

void pushQuad(std::vector<Vertex>& out, const glm::vec3 p[4], const glm::vec2 uv[4], const glm::vec3& normal)
{
	const int order[6] = { 0, 1, 2, 0, 2, 3 };
	for (int k = 0; k < 6; k++)
	{
		Vertex v;
		v.position = p[order[k]];
		v.texCoords = uv[order[k]];
		v.normal = normal;
		out.push_back(v);
	}
}

void pushColoredQuad(std::vector<Vertex>& out, const glm::vec3 p[4], const glm::vec2 uv[4], const glm::vec3& normal, const glm::vec3& color)
{
	const int order[6] = { 0, 1, 2, 0, 2, 3 };
	for (int k = 0; k < 6; k++)
	{
		Vertex v;
		v.position = p[order[k]];
		v.texCoords = uv[order[k]];
		v.normal = normal;
		v.color = color;
		out.push_back(v);
	}
}

void pushColoredTri(std::vector<Vertex>& out, const glm::vec3 p[3], const glm::vec2 uv[3], const glm::vec3& normal, const glm::vec3& color)
{
	for (int k = 0; k < 3; k++)
	{
		Vertex v;
		v.position = p[k];
		v.texCoords = uv[k];
		v.normal = normal;
		v.color = color;
		out.push_back(v);
	}
}

// [from, to] minus the given holes, as a list of (start, end) pieces
std::vector<glm::vec2> spansMinus(float from, float to, std::vector<glm::vec2> holes)
{
	std::vector<glm::vec2> pieces;
	float cursor = from;
	std::sort(holes.begin(), holes.end(), [](const glm::vec2& a, const glm::vec2& b) { return a.x < b.x; });
	for (const glm::vec2& h : holes)
	{
		if (h.x > cursor) pieces.push_back(glm::vec2(cursor, std::min(h.x, to)));
		cursor = std::max(cursor, h.y);
	}
	if (cursor < to) pieces.push_back(glm::vec2(cursor, to));
	return pieces;
}

// Spans along a road that are crossed by the perpendicular roads, optionally
// widened by their sidewalks
std::vector<glm::vec2> crossingSpans(const Road& road, float extra)
{
	std::vector<glm::vec2> holes;
	for (int i = 0; i < NUM_ROADS; i++)
		if (ROADS[i].alongX != road.alongX)
			holes.push_back(glm::vec2(ROADS[i].center - ROADS[i].width * 0.5f - extra, ROADS[i].center + ROADS[i].width * 0.5f + extra));
	return holes;
}

// World position from (along, across) coordinates of a road
glm::vec3 roadPoint(const Road& road, float along, float across, float y)
{
	return road.alongX ? glm::vec3(along, y, across) : glm::vec3(across, y, along);
}

// Axis aligned box from its min/max corners, with world-space UVs
void pushBox(std::vector<Vertex>& out, glm::vec3 lo, glm::vec3 hi, float texSize)
{
	glm::vec3 p[4]; glm::vec2 uv[4];
	// top
	p[0] = glm::vec3(lo.x, hi.y, lo.z); p[1] = glm::vec3(hi.x, hi.y, lo.z); p[2] = glm::vec3(hi.x, hi.y, hi.z); p[3] = glm::vec3(lo.x, hi.y, hi.z);
	for (int k = 0; k < 4; k++) uv[k] = glm::vec2(p[k].x, p[k].z) / texSize;
	pushQuad(out, p, uv, glm::vec3(0, 1, 0));
	// sides (the curbs)
	float h = (hi.y - lo.y) / texSize;
	p[0] = glm::vec3(lo.x, lo.y, lo.z); p[1] = glm::vec3(hi.x, lo.y, lo.z); p[2] = glm::vec3(hi.x, hi.y, lo.z); p[3] = glm::vec3(lo.x, hi.y, lo.z);
	uv[0] = glm::vec2(lo.x / texSize, 0); uv[1] = glm::vec2(hi.x / texSize, 0); uv[2] = glm::vec2(hi.x / texSize, h); uv[3] = glm::vec2(lo.x / texSize, h);
	pushQuad(out, p, uv, glm::vec3(0, 0, -1));
	for (int k = 0; k < 4; k++) p[k].z = hi.z;
	pushQuad(out, p, uv, glm::vec3(0, 0, 1));
	p[0] = glm::vec3(lo.x, lo.y, lo.z); p[1] = glm::vec3(lo.x, lo.y, hi.z); p[2] = glm::vec3(lo.x, hi.y, hi.z); p[3] = glm::vec3(lo.x, hi.y, lo.z);
	uv[0] = glm::vec2(lo.z / texSize, 0); uv[1] = glm::vec2(hi.z / texSize, 0); uv[2] = glm::vec2(hi.z / texSize, h); uv[3] = glm::vec2(lo.z / texSize, h);
	pushQuad(out, p, uv, glm::vec3(-1, 0, 0));
	for (int k = 0; k < 4; k++) p[k].x = hi.x;
	pushQuad(out, p, uv, glm::vec3(1, 0, 0));
}

// Road surfaces. Roads along Z stop where they meet a road along X so the
// two never overlap (no z-fighting in the intersections).
std::vector<Vertex> buildRoads(float g)
{
	std::vector<Vertex> out;
	for (int r = 0; r < NUM_ROADS; r++)
	{
		const Road& road = ROADS[r];
		float half = road.width * 0.5f;
		std::vector<glm::vec2> pieces = road.alongX
			? std::vector<glm::vec2>(1, glm::vec2(-CITY_HALF, CITY_HALF))
			: spansMinus(-CITY_HALF, CITY_HALF, crossingSpans(road, 0.0f));
		for (const glm::vec2& piece : pieces)
		{
			glm::vec3 p[4] = {
				roadPoint(road, piece.x, road.center - half, g + ROAD_Y),
				roadPoint(road, piece.y, road.center - half, g + ROAD_Y),
				roadPoint(road, piece.y, road.center + half, g + ROAD_Y),
				roadPoint(road, piece.x, road.center + half, g + ROAD_Y) };
			// U goes across the road (texture edge to edge), V along it
			glm::vec2 uv[4] = {
				glm::vec2(0.0f, piece.x / ROAD_TEX_LENGTH), glm::vec2(0.0f, piece.y / ROAD_TEX_LENGTH),
				glm::vec2(1.0f, piece.y / ROAD_TEX_LENGTH), glm::vec2(1.0f, piece.x / ROAD_TEX_LENGTH) };
			pushQuad(out, p, uv, glm::vec3(0, 1, 0));
		}
	}
	return out;
}

// Raised sidewalks on both sides of every road, interrupted by the crossings
std::vector<Vertex> buildSidewalks(float g)
{
	std::vector<Vertex> out;
	for (int r = 0; r < NUM_ROADS; r++)
	{
		const Road& road = ROADS[r];
		float half = road.width * 0.5f;
		// Sidewalks along X stop at the cross road itself; sidewalks along Z also
		// skip the X road's sidewalks, so corners don't overlap
		float extra = road.alongX ? 0.0f : SIDEWALK_W;
		for (const glm::vec2& piece : spansMinus(-CITY_HALF, CITY_HALF, crossingSpans(road, extra)))
		{
			for (int side = -1; side <= 1; side += 2)
			{
				float a0 = road.center + side * half;
				float a1 = road.center + side * (half + SIDEWALK_W);
				glm::vec3 c0 = roadPoint(road, piece.x, std::min(a0, a1), g);
				glm::vec3 c1 = roadPoint(road, piece.y, std::max(a0, a1), g + SIDEWALK_H);
				pushBox(out, glm::min(c0, c1), glm::max(c0, c1), SIDEWALK_TEX);
			}
		}
	}
	return out;
}

std::vector<Vertex> buildGround(float g)
{
	std::vector<Vertex> out;
	float e = CITY_HALF + 60.0f;	// beyond the streets, hidden by the fog
	glm::vec3 p[4] = { glm::vec3(-e, g, -e), glm::vec3(e, g, -e), glm::vec3(e, g, e), glm::vec3(-e, g, e) };
	glm::vec2 uv[4];
	for (int k = 0; k < 4; k++) uv[k] = glm::vec2(p[k].x, p[k].z) / GROUND_TEX;
	pushQuad(out, p, uv, glm::vec3(0, 1, 0));
	return out;
}

bool onStreet(float x, float z, float margin)
{
	for (int r = 0; r < NUM_ROADS; r++)
	{
		float d = std::fabs((ROADS[r].alongX ? z : x) - ROADS[r].center);
		if (d < ROADS[r].width * 0.5f + SIDEWALK_W + margin) return true;
	}
	return false;
}

bool inCrossing(const Road& road, float along, float extra)
{
	for (const glm::vec2& h : crossingSpans(road, extra))
		if (along > h.x && along < h.y) return true;
	return false;
}

// A tuft = two crossed quads showing half of the grass atlas (0: grass
// blades, 1: weed with flowers). Normals point up so both faces are lit
// like the ground they grow on.
void pushTuft(std::vector<Vertex>& out, glm::vec3 base, float height, float yaw, int variant)
{
	float halfWidth = height * 0.55f;
	float u0 = variant * 0.5f + 0.01f, u1 = variant * 0.5f + 0.49f;
	glm::vec2 uv[4] = { glm::vec2(u0, 0.01f), glm::vec2(u1, 0.01f), glm::vec2(u1, 0.99f), glm::vec2(u0, 0.99f) };
	for (int k = 0; k < 2; k++)
	{
		float a = yaw + k * glm::half_pi<float>();
		glm::vec3 dir(std::cos(a) * halfWidth, 0.0f, std::sin(a) * halfWidth);
		glm::vec3 up(0.0f, height, 0.0f);
		glm::vec3 p[4] = { base - dir, base + dir, base + dir + up, base - dir + up };
		pushQuad(out, p, uv, glm::vec3(0, 1, 0));
	}
}

// Weeds everywhere nature is taking the city back: along the road edges and
// curbs, in the road cracks, in the sidewalk joints, and all over the lots
std::vector<Vertex> buildGrass(float g)
{
	std::vector<Vertex> out;
	std::mt19937 rng(1337);	// fixed seed: same city every run
	auto rnd = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
	auto variant = [&](float weedChance) { return rnd(0.0f, 1.0f) < weedChance ? 1 : 0; };

	for (int r = 0; r < NUM_ROADS; r++)
	{
		const Road& road = ROADS[r];
		float half = road.width * 0.5f;
		for (int side = -1; side <= 1; side += 2)
		{
			// Along the road edge, against the curb
			for (int i = 0; i < 450; i++)
			{
				float along = rnd(-CITY_HALF, CITY_HALF);
				if (inCrossing(road, along, road.alongX ? 0.0f : SIDEWALK_W)) continue;
				float across = road.center + side * (half - rnd(0.0f, 0.7f));
				pushTuft(out, roadPoint(road, along, across, g + ROAD_Y), rnd(0.25f, 0.8f), rnd(0.0f, 6.3f), variant(0.15f));
			}
			// Tall grass where the sidewalk meets the wasteland
			for (int i = 0; i < 300; i++)
			{
				float along = rnd(-CITY_HALF, CITY_HALF);
				float across = road.center + side * (half + SIDEWALK_W + rnd(0.0f, 0.8f));
				glm::vec3 pos = roadPoint(road, along, across, g);
				if (onStreet(pos.x, pos.z, -0.05f)) continue;
				pushTuft(out, pos, rnd(0.5f, 1.3f), rnd(0.0f, 6.3f), variant(0.2f));
			}
			// In the sidewalk joints (slabs are SIDEWALK_TEX / 2 wide)
			for (int i = 0; i < 180; i++)
			{
				float slab = SIDEWALK_TEX * 0.5f;
				float along = std::round(rnd(-CITY_HALF, CITY_HALF) / slab) * slab;
				if (inCrossing(road, along, road.alongX ? 0.0f : SIDEWALK_W)) continue;
				float across = road.center + side * (half + rnd(0.1f, SIDEWALK_W - 0.1f));
				pushTuft(out, roadPoint(road, along, across, g + SIDEWALK_H), rnd(0.15f, 0.45f), rnd(0.0f, 6.3f), variant(0.1f));
			}
		}
		// Small tufts growing out of the cracks in the middle of the road
		for (int i = 0; i < 120; i++)
		{
			float along = rnd(-CITY_HALF, CITY_HALF);
			if (inCrossing(road, along, road.alongX ? 0.0f : SIDEWALK_W)) continue;
			float across = road.center + rnd(-half + 0.5f, half - 0.5f);
			pushTuft(out, roadPoint(road, along, across, g + ROAD_Y), rnd(0.15f, 0.4f), rnd(0.0f, 6.3f), 0);
		}
	}

	// Wasteland lots: patchy, denser in some areas than others
	for (int i = 0; i < 7000; i++)
	{
		float x = rnd(-CITY_HALF, CITY_HALF), z = rnd(-CITY_HALF, CITY_HALF);
		if (onStreet(x, z, 0.3f)) continue;
		float patch = std::sin(x * 0.11f) + std::cos(z * 0.13f) + std::sin((x - z) * 0.05f);
		if (patch < -0.5f && rnd(0.0f, 1.0f) < 0.8f) continue;
		pushTuft(out, glm::vec3(x, g, z), rnd(0.5f, 1.5f), rnd(0.0f, 6.3f), variant(0.2f));
	}
	return out;
}

// The dead man: "Human Skeleton" by Armen Barsegyan (cgicoffee.com, CC0),
// every bone posed rigidly around its joint, sitting against a wall, then
// baked into models/skeleton_sitting.obj. Measured on that posed model:
// its back (resting on the wall) and its right hand, holding the bottle.
const float SKELETON_BACK_Z = -0.764f;
const glm::vec3 SKELETON_RIGHT_HAND(-1.281f, 0.897f, 0.887f);

// A glass bottle: a profile (radius, height) revolved around Y, bottom at
// y = 0. U goes around it, V up it (textures/bottle_old.png: its label is
// between heights 0.15 and 0.40).
std::vector<Vertex> buildBottle()
{
	const glm::vec2 profile[] = {
		{ 0.0f, 0.0f }, { 0.13f, 0.0f }, { 0.14f, 0.02f }, { 0.14f, 0.15f }, { 0.141f, 0.40f }, { 0.14f, 0.48f }, { 0.12f, 0.56f },
		{ 0.06f, 0.64f }, { 0.045f, 0.70f }, { 0.045f, 0.80f }, { 0.055f, 0.81f }, { 0.055f, 0.85f }, { 0.0f, 0.85f } };
	const int NP = sizeof(profile) / sizeof(profile[0]), SEG = 24;
	const float HEIGHT = 0.85f;
	std::vector<Vertex> out;
	for (int i = 0; i + 1 < NP; i++)
	{
		glm::vec2 a = profile[i], b = profile[i + 1];
		if (glm::length(b - a) < 1e-6f) continue;
		glm::vec2 slope = glm::normalize(glm::vec2(b.y - a.y, -(b.x - a.x)));	// outward profile normal
		for (int k = 0; k < SEG; k++)
		{
			float u0 = k / (float)SEG, u1 = (k + 1) / (float)SEG;
			float t0 = u0 * glm::two_pi<float>(), t1 = u1 * glm::two_pi<float>();
			glm::vec3 p[4] = {
				glm::vec3(a.x * std::cos(t0), a.y, a.x * std::sin(t0)), glm::vec3(a.x * std::cos(t1), a.y, a.x * std::sin(t1)),
				glm::vec3(b.x * std::cos(t1), b.y, b.x * std::sin(t1)), glm::vec3(b.x * std::cos(t0), b.y, b.x * std::sin(t0)) };
			glm::vec2 uv[4] = { glm::vec2(u0, a.y / HEIGHT), glm::vec2(u1, a.y / HEIGHT), glm::vec2(u1, b.y / HEIGHT), glm::vec2(u0, b.y / HEIGHT) };
			float tm = (t0 + t1) * 0.5f;
			pushQuad(out, p, uv, glm::normalize(glm::vec3(slope.x * std::cos(tm), slope.y, slope.x * std::sin(tm))));
		}
	}
	return out;
}
// Helper to push a hemisphere for the spherical camera eye
void pushHemisphere(std::vector<Vertex>& out, glm::vec3 center, float radius, glm::vec3 forward, glm::vec3 up, int rings, int segments, glm::vec3 color)
{
	glm::vec3 right = glm::normalize(glm::cross(forward, up));
	up = glm::normalize(glm::cross(right, forward));
	glm::vec2 uv(0.5f, 0.5f);
	for (int r = 0; r < rings; r++)
	{
		float phi0 = (r / (float)rings) * glm::half_pi<float>();
		float phi1 = ((r + 1) / (float)rings) * glm::half_pi<float>();
		float z0 = std::cos(phi0) * radius, z1 = std::cos(phi1) * radius;
		float r0 = std::sin(phi0) * radius, r1 = std::sin(phi1) * radius;
		for (int s = 0; s < segments; s++)
		{
			float t0 = (s / (float)segments) * glm::two_pi<float>();
			float t1 = ((s + 1) / (float)segments) * glm::two_pi<float>();
			glm::vec3 p0 = center + forward * z0 + right * (std::cos(t0) * r0) + up * (std::sin(t0) * r0);
			glm::vec3 p1 = center + forward * z0 + right * (std::cos(t1) * r0) + up * (std::sin(t1) * r0);
			glm::vec3 p2 = center + forward * z1 + right * (std::cos(t1) * r1) + up * (std::sin(t1) * r1);
			glm::vec3 p3 = center + forward * z1 + right * (std::cos(t0) * r1) + up * (std::sin(t0) * r1);
			glm::vec3 n0 = glm::normalize(p0 - center);
			glm::vec3 n1 = glm::normalize(p1 - center);
			glm::vec3 n2 = glm::normalize(p2 - center);
			glm::vec3 n3 = glm::normalize(p3 - center);
			Vertex v0 = { p0, n0, uv, color };
			Vertex v1 = { p1, n1, uv, color };
			Vertex v2 = { p2, n2, uv, color };
			Vertex v3 = { p3, n3, uv, color };
			out.push_back(v0); out.push_back(v1); out.push_back(v2);
			out.push_back(v0); out.push_back(v2); out.push_back(v3);
		}
	}
}


// 11-45-G: tall monolithic triangular pyramid robot from Love, Death & Robots
// Features a dark matte gunmetal shell, beveled 45-degree chamfers, a dark
// speaker/vent grill across the lower half, and an elevated spherical camera
// eye protruding from the upper front face.
void build1145G(std::vector<Vertex>& bodyOut, std::vector<Vertex>& eyeOut)
{
	// 2D profile polygon in XZ plane (facing +Z):
	// Symmetrical around X=0.
	// Front face: flat central panel with 45-degree chamfers on left and right.
	// Sides taper back towards a rounded rear apex.
	const glm::vec2 baseProfile[] = {
		{ -0.24f,  0.44f }, // 0: front-left center
		{  0.00f,  0.45f }, // 1: front center (slight center crease)
		{  0.24f,  0.44f }, // 2: front-right center
		{  0.42f,  0.32f }, // 3: front-right chamfer
		{  0.48f,  0.16f }, // 4: right side front
		{  0.34f, -0.12f }, // 5: right side
		{  0.18f, -0.36f }, // 6: right rear
		{  0.08f, -0.46f }, // 7: rear apex right
		{  0.00f, -0.48f }, // 8: rear apex center
		{ -0.08f, -0.46f }, // 9: rear apex left
		{ -0.18f, -0.36f }, // 10: left rear
		{ -0.34f, -0.12f }, // 11: left side
		{ -0.48f,  0.16f }, // 12: left side front
		{ -0.42f,  0.32f }  // 13: front-left chamfer
	};
	const int NV = sizeof(baseProfile) / sizeof(baseProfile[0]);

	struct RingDef {
		float y;
		float sx, sz;
		bool isGrill;
		glm::vec3 darkColor;
	};

	const glm::vec3 kDarkBody(0.13f, 0.14f, 0.16f);        // Dark matte gunmetal
	const glm::vec3 kChamfer(0.18f, 0.19f, 0.22f);         // Slightly lighter chamfer edges
	const glm::vec3 kGrill(0.08f, 0.085f, 0.095f);         // Dark vent grill
	const glm::vec3 kDarkPlinth(0.10f, 0.10f, 0.12f);      // Heavy base plinth
	const glm::vec3 kCameraBezel(0.24f, 0.25f, 0.28f);     // Dark metallic bezel

	const RingDef rings[] = {
		{ 0.00f, 1.18f, 0.94f, false, kDarkPlinth },       // 0: ground plinth resting flush on floor
		{ 0.08f, 1.16f, 0.92f, false, kDarkPlinth },       // 1: plinth bevel
		{ 0.18f, 1.13f, 0.89f, false, glm::vec3(0.0f) },   // 2: lower body
		{ 0.25f, 1.10f, 0.87f, true,  glm::vec3(0.0f) },   // 3: grill start
		{ 0.95f, 0.88f, 0.69f, true,  glm::vec3(0.0f) },   // 4: grill mid
		{ 1.65f, 0.68f, 0.53f, true,  glm::vec3(0.0f) },   // 5: grill end
		{ 1.72f, 0.66f, 0.51f, false, kDarkPlinth },       // 6: seam above grill
		{ 2.02f, 0.57f, 0.44f, false, glm::vec3(0.0f) },   // 7: below camera
		{ 2.24f, 0.50f, 0.39f, false, glm::vec3(0.0f) },   // 8: above camera
		{ 2.70f, 0.36f, 0.28f, false, glm::vec3(0.0f) },   // 9: upper spire
		{ 2.78f, 0.33f, 0.26f, false, kDarkPlinth },       // 10: top chamfer
		{ 2.85f, 0.28f, 0.22f, false, kDarkPlinth }        // 11: flat top cap
	};
	const int NR = sizeof(rings) / sizeof(rings[0]);

	// Build vertex positions for each ring
	std::vector<std::vector<glm::vec3>> ringVerts(NR, std::vector<glm::vec3>(NV));
	for (int r = 0; r < NR; r++)
	{
		for (int v = 0; v < NV; v++)
		{
			float px = baseProfile[v].x * rings[r].sx;
			float pz = baseProfile[v].y * rings[r].sz;
			ringVerts[r][v] = glm::vec3(px, rings[r].y, pz);
		}
	}

	glm::vec2 dummyUV[4] = { glm::vec2(0,0), glm::vec2(1,0), glm::vec2(1,1), glm::vec2(0,1) };
	for (int r = 0; r + 1 < NR; r++)
	{
		bool isGrillBand = rings[r].isGrill && rings[r + 1].isGrill;
		for (int v = 0; v < NV; v++)
		{
			int nextV = (v + 1) % NV;
			glm::vec3 p[4] = {
				ringVerts[r][v],
				ringVerts[r][nextV],
				ringVerts[r + 1][nextV],
				ringVerts[r + 1][v]
			};
			glm::vec3 normal = glm::normalize(glm::cross(p[1] - p[0], p[3] - p[0]));

			glm::vec3 color;
			if (rings[r].darkColor != glm::vec3(0.0f) && rings[r + 1].darkColor != glm::vec3(0.0f))
			{
				color = rings[r].darkColor;
			}
			else if (isGrillBand && (v == 0 || v == 1))
			{
				color = kGrill; // dark front vent / speaker grill
			}
			else if (v == 3 || v == 13)
			{
				color = kChamfer; // front 45-degree chamfers
			}
			else
			{
				color = kDarkBody; // dark matte gunmetal main panels
			}
			pushColoredQuad(bodyOut, p, dummyUV, normal, color);
		}
	}

	// Bottom cap (ring 0) facing down flush on the floor
	glm::vec3 bottomCenter(0.0f, rings[0].y, 0.0f);
	for (int v = 0; v < NV; v++)
	{
		int nextV = (v + 1) % NV;
		glm::vec3 tri[3] = { bottomCenter, ringVerts[0][nextV], ringVerts[0][v] };
		pushColoredTri(bodyOut, tri, dummyUV, glm::vec3(0, -1, 0), kDarkPlinth);
	}

	// Top cap (ring NR - 1) facing up
	glm::vec3 topCenter(0.0f, rings[NR - 1].y, 0.0f);
	for (int v = 0; v < NV; v++)
	{
		int nextV = (v + 1) % NV;
		glm::vec3 tri[3] = { topCenter, ringVerts[NR - 1][v], ringVerts[NR - 1][nextV] };
		pushColoredTri(bodyOut, tri, dummyUV, glm::vec3(0, 1, 0), kDarkPlinth);
	}

	// Camera eye on the front face (y = 2.12m)
	const float camY = 2.12f;
	const float camZ = baseProfile[1].y * 0.42f; // ~0.189f
	const glm::vec3 camCenter(0.0f, camY, camZ);
	const glm::vec3 camForward(0.0f, 0.22f, 0.975f); // normal tilted slightly along trapezoid slope
	const glm::vec3 camUp(0.0f, 0.975f, -0.22f);

	// Bezel collar ring around the camera lens
	const int CSEG = 20;
	const float rBezel = 0.09f, bezelDepth = 0.025f;
	for (int k = 0; k < CSEG; k++)
	{
		float a0 = k * glm::two_pi<float>() / CSEG, a1 = (k + 1) * glm::two_pi<float>() / CSEG;
		glm::vec3 right = glm::normalize(glm::cross(camForward, camUp));
		glm::vec3 d0 = right * std::cos(a0) + camUp * std::sin(a0);
		glm::vec3 d1 = right * std::cos(a1) + camUp * std::sin(a1);
		glm::vec3 cp[4] = {
			camCenter + d0 * rBezel,
			camCenter + d1 * rBezel,
			camCenter + d1 * rBezel + camForward * bezelDepth,
			camCenter + d0 * rBezel + camForward * bezelDepth
		};
		glm::vec3 n = glm::normalize(d0 + d1);
		pushColoredQuad(bodyOut, cp, dummyUV, n, kCameraBezel);
	}

	// Spherical eyeball lens (bulging hemisphere from bezel)
	const float rLens = 0.075f;
	const glm::vec3 lensCenter = camCenter + camForward * 0.015f;
	pushHemisphere(bodyOut, lensCenter, rLens, camForward, camUp, 8, 16, glm::vec3(0.05f, 0.055f, 0.065f));

	// Glowing optical aperture / pupil on eyeOut (facing forward)
	const float rPupil = 0.038f;
	const glm::vec3 pupilCenter = lensCenter + camForward * (rLens + 0.002f);
	const glm::vec3 kCyanScan(0.05f, 0.90f, 1.0f);
	const glm::vec3 kWhiteCore(0.85f, 0.95f, 1.0f);
	glm::vec3 right = glm::normalize(glm::cross(camForward, camUp));

	for (int k = 0; k < CSEG; k++)
	{
		float a0 = k * glm::two_pi<float>() / CSEG, a1 = (k + 1) * glm::two_pi<float>() / CSEG;
		glm::vec3 d0 = right * std::cos(a0) + camUp * std::sin(a0);
		glm::vec3 d1 = right * std::cos(a1) + camUp * std::sin(a1);
		glm::vec3 tri[3] = { pupilCenter, pupilCenter + d0 * rPupil, pupilCenter + d1 * rPupil };
		pushColoredTri(eyeOut, tri, dummyUV, camForward, kCyanScan);
		// Hot core center
		glm::vec3 triCore[3] = { pupilCenter + camForward * 0.001f, pupilCenter + d0 * (rPupil * 0.45f), pupilCenter + d1 * (rPupil * 0.45f) };
		pushColoredTri(eyeOut, triCore, dummyUV, camForward, kWhiteCore);
	}
}

// Planar laser scanning sheet (razor-thin laser fan projecting a sharp horizontal scanline)
std::vector<Vertex> buildScanBeam(float length, float targetWidth)
{
	std::vector<Vertex> out;
	auto addTwoSidedQuad = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
		glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2, glm::vec2 uv3) {
		glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p3 - p0));
		glm::vec3 col(1.0f);
		Vertex v0 = { p0, n, uv0, col };
		Vertex v1 = { p1, n, uv1, col };
		Vertex v2 = { p2, n, uv2, col };
		Vertex v3 = { p3, n, uv3, col };
		// Side A
		out.push_back(v0); out.push_back(v1); out.push_back(v2);
		out.push_back(v0); out.push_back(v2); out.push_back(v3);
		// Side B
		out.push_back(v0); out.push_back(v3); out.push_back(v2);
		out.push_back(v0); out.push_back(v2); out.push_back(v1);
	};

	const int NZ = 24;
	const float halfW = targetWidth * 0.5f;

	// 1. Planar laser fan (horizontal plane XZ)
	for (int i = 0; i < NZ; i++)
	{
		float z0 = length * (i / (float)NZ);
		float z1 = length * ((i + 1) / (float)NZ);
		float w0 = halfW * (z0 / length);
		float w1 = halfW * ((z1) / length);
		float v0 = z0 / length, v1 = z1 / length;

		// Main horizontal laser line sheet
		glm::vec3 p0(-w0, 0.0f, z0), p1(w0, 0.0f, z0);
		glm::vec3 p2(w1, 0.0f, z1),  p3(-w1, 0.0f, z1);
		addTwoSidedQuad(p0, p1, p2, p3, glm::vec2(0.0f, v0), glm::vec2(1.0f, v0), glm::vec2(1.0f, v1), glm::vec2(0.0f, v1));

		// Micro vertical core ribbon (height only a few millimeters: ~5mm max)
		float h0 = 0.005f * (z0 / length), h1 = 0.005f * (z1 / length);
		glm::vec3 c0(0.0f, -h0, z0), c1(0.0f, h0, z0), c2(0.0f, h1, z1), c3(0.0f, -h1, z1);
		addTwoSidedQuad(c0, c1, c2, c3, glm::vec2(0.5f, v0), glm::vec2(0.5f, v0), glm::vec2(0.5f, v1), glm::vec2(0.5f, v1));
	}

	// 2. Target laser scanline: a sharp horizontal laser line drawn at distance length
	const float lineThickness = 0.008f;
	glm::vec3 t0(-halfW, -lineThickness, length);
	glm::vec3 t1( halfW, -lineThickness, length);
	glm::vec3 t2( halfW,  lineThickness, length);
	glm::vec3 t3(-halfW,  lineThickness, length);
	addTwoSidedQuad(t0, t1, t2, t3, glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(1.0f, 1.0f), glm::vec2(0.0f, 1.0f));

	return out;
}

glm::mat4 propMatrix(const Prop& p)
{
	return glm::translate(glm::mat4(1.0), p.pos) *
		glm::rotate(glm::mat4(1.0), glm::radians(p.rot.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
		glm::rotate(glm::mat4(1.0), glm::radians(p.rot.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
		glm::rotate(glm::mat4(1.0), glm::radians(p.rot.z), glm::vec3(0.0f, 0.0f, 1.0f)) *
		glm::scale(glm::mat4(1.0), p.scale);
}

// The light of a lamp post, in its lantern
enum LampMode { LAMP_STEADY, LAMP_FLICKER, LAMP_DEAD };
struct LampLight
{
	glm::vec3 pos;		// where the light and its glowing bulb are
	float bulbScale;	// scale of light.obj for the bulb
	LampMode mode;
	float seed;			// desynchronizes the flickering lamps
	glm::vec3 color;	// light color at full intensity
	float linear, exponent;	// attenuation
	float cosCone;		// downward cone (see city.frag)
	bool ceiling;		// ceiling light: draw its fixture above the bulb
};

// Ceiling lights inside the ruined buildings. Anchors are points of the
// tower mesh (local X, Y) that are really inside a room: under a ceiling
// slab, above a floor slab, away from the holes and edges. Found by
// rasterizing the mesh's horizontal faces.
const int NUM_CEILING_LEVELS = 2;
const float CEILING_Z[NUM_CEILING_LEVELS] = { 0.178f, 0.830f };		// slab undersides
const float CEILING_FLOOR_Z[NUM_CEILING_LEVELS] = { -0.27f, 0.2575f };	// floor below each
const int NUM_CEILING_ANCHORS = 6;
const glm::vec2 CEILING_ANCHORS[NUM_CEILING_LEVELS][NUM_CEILING_ANCHORS] = {
	{ {-0.279f, 0.236f}, {0.662f, -0.801f}, {0.493f, 0.832f}, {-0.971f, -0.125f}, {-0.126f, 0.832f}, {0.340f, 0.236f} },
	{ {-0.376f, 0.172f}, {0.493f, 0.832f}, {0.662f, -0.029f}, {-0.971f, -0.134f}, {-0.143f, 0.832f}, {-0.014f, -0.206f} },
};
const float FIXTURE_CORD = 0.6f;		// cord length under the ceiling
const float FIXTURE_BULB_Y = -0.88f;	// bulb center, relative to the ceiling

// Hanging ceiling lamp: a cord and a metal shade, ceiling at y = 0
std::vector<Vertex> buildCeilingFixture()
{
	std::vector<Vertex> out;
	const float c = 0.015f;	// cord half width
	glm::vec3 lo(-c, -FIXTURE_CORD, -c), hi(c, 0.0f, c);
	glm::vec2 uv[4] = { glm::vec2(0.2f, 0.0f), glm::vec2(0.3f, 0.0f), glm::vec2(0.3f, 0.3f), glm::vec2(0.2f, 0.3f) };
	glm::vec3 q[4];
	for (int side = 0; side < 4; side++)
	{
		float a0 = side * glm::half_pi<float>(), a1 = a0 + glm::half_pi<float>();
		glm::vec3 d0(std::cos(a0) * c * 1.4f, 0.0f, std::sin(a0) * c * 1.4f), d1(std::cos(a1) * c * 1.4f, 0.0f, std::sin(a1) * c * 1.4f);
		q[0] = d0 + glm::vec3(0, lo.y, 0); q[1] = d1 + glm::vec3(0, lo.y, 0); q[2] = d1; q[3] = d0;
		pushQuad(out, q, uv, glm::normalize(d0 + d1));
	}
	// Shade: truncated cone, open at the bottom
	const int SEG = 12;
	const float yTop = -FIXTURE_CORD, yBottom = -0.95f, rTop = 0.1f, rBottom = 0.42f;
	for (int i = 0; i < SEG; i++)
	{
		float a0 = i * glm::two_pi<float>() / SEG, a1 = (i + 1) * glm::two_pi<float>() / SEG;
		glm::vec3 p[4] = {
			glm::vec3(std::cos(a0) * rBottom, yBottom, std::sin(a0) * rBottom), glm::vec3(std::cos(a1) * rBottom, yBottom, std::sin(a1) * rBottom),
			glm::vec3(std::cos(a1) * rTop, yTop, std::sin(a1) * rTop), glm::vec3(std::cos(a0) * rTop, yTop, std::sin(a0) * rTop) };
		glm::vec2 t[4] = { glm::vec2(i / (float)SEG, 0.4f), glm::vec2((i + 1) / (float)SEG, 0.4f), glm::vec2((i + 1) / (float)SEG, 0.6f), glm::vec2(i / (float)SEG, 0.6f) };
		float am = (a0 + a1) * 0.5f;
		pushQuad(out, p, t, glm::normalize(glm::vec3(std::cos(am), (rBottom - rTop) / (yTop - yBottom), std::sin(am))));
	}
	return out;
}
const float LANTERN_Y = 3.72f;		// center of the glass bulb in lampPost.obj
const float BULB_RADIUS = 0.045f;	// radius of that glass bulb
const float LIGHT_OBJ_RADIUS = 0.22f;	// radius of light.obj
const int NUM_FIXED_LIGHTS = 6;		// 2 rim lights, xbot's 2 eyes, LittleBot's face, 11-45-G's visor
const int MAX_LAMP_LIGHTS = 64 - NUM_FIXED_LIGHTS;	// the shader has 64 point lights

float hash(float x, float seed)
{
	float v = std::sin(x * 12.9898f + seed * 78.233f) * 43758.5453f;
	return v - std::floor(v);
}

// Brightness of a lamp at time t (0 = off, 1 = full)
float lampIntensity(const LampLight& lamp, float t)
{
	switch (lamp.mode)
	{
	case LAMP_DEAD:
		return 0.0f;
	case LAMP_STEADY:
		return 1.0f - 0.04f * std::sin(t * 1.7f + lamp.seed * 10.0f);	// old bulb, slight hum
	case LAMP_FLICKER:
	default:
	{
		// Long calm phases broken by bursts where the bulb stutters on and off
		bool unstable = hash(std::floor(t * 0.7f + lamp.seed * 7.0f), lamp.seed) < 0.4f;
		float h = hash(std::floor(t * 14.0f + lamp.seed * 31.0f), lamp.seed + 1.7f);
		if (unstable)
			return h < 0.45f ? 0.04f : 0.55f + 0.45f * h;
		return 0.9f + 0.1f * std::sin(t * 37.0f + lamp.seed * 5.0f);
	}
	}
}

// Ruined buildings all reuse the tower mesh. What makes them read as
// different buildings is the silhouette, not the texture: proportions,
// orientation, and one of these treatments
enum BuildingStyle
{
	STANDING,		// as is
	MIRRORED,		// flipped left/right: the gaps and broken edges move
	UPSIDE_DOWN,	// resting on its roof: its jagged underside becomes the skyline
	LEANING,		// tilted, about to fall
	COLLAPSED,		// the lower floors have given way: only the top floors remain
	STACKED,		// two pieces on top of each other, the upper one turned around
};

// height = visible height above the street
void addBuilding(std::vector<Prop>& props, float g, float x, float z, float width, float depth, float height, float yaw, BuildingStyle style)
{
	// With a quarter turn the local X/Y of the mesh map to world depth/width
	bool quarterTurn = (int)std::round(yaw / 90.0f) % 2 != 0;
	float sx = (quarterTurn ? depth : width) / TOWER_SIZE_X;
	float sy = (quarterTurn ? width : depth) / TOWER_SIZE_Y;
	glm::vec3 standingRot(-90.0f, yaw, 0.0f);

	// Mesh resting on the slab at local height restZ, rising to local topZ
	auto standingOn = [&](float restZ, float topZ, float visibleHeight, float baseY) {
		float sz = visibleHeight / (topZ - restZ);
		return Prop{ M_TOWER, glm::vec3(x, baseY + SLAB_LIFT - restZ * sz, z), glm::vec3(sx, sy, sz), standingRot };
	};

	switch (style)
	{
	case STANDING:
		props.push_back(standingOn(TOWER_LOW_SLAB_Z, TOWER_TOP_Z, height, g));
		break;
	case MIRRORED:
	{
		Prop b = standingOn(TOWER_LOW_SLAB_Z, TOWER_TOP_Z, height, g);
		b.scale.x = -b.scale.x;
		props.push_back(b);
		break;
	}
	case UPSIDE_DOWN:
	{
		// Flipped, local Z points down: y = pos.y - z * sz
		float sz = height / (TOWER_ROOF_SLAB_Z - TOWER_BOTTOM_Z);
		props.push_back({ M_TOWER, glm::vec3(x, g + SLAB_LIFT + TOWER_ROOF_SLAB_Z * sz, z), glm::vec3(sx, sy, sz), glm::vec3(90.0f, yaw, 0.0f) });
		break;
	}
	case LEANING:
	{
		Prop b = standingOn(TOWER_LOW_SLAB_Z, TOWER_TOP_Z, height, g);
		b.rot.x += 7.0f;
		b.pos.y -= 0.07f * std::max(width, depth);	// sink the raised edge of the base
		props.push_back(b);
		break;
	}
	case COLLAPSED:
		props.push_back(standingOn(TOWER_MID_SLAB_Z, TOWER_TOP_Z, height, g));
		break;
	case STACKED:
	{
		// Lower piece on the street, upper piece resting on its roof slab
		Prop lower = standingOn(TOWER_LOW_SLAB_Z, TOWER_TOP_Z, height * 0.55f, g);
		float roofY = lower.pos.y + TOWER_ROOF_SLAB_Z * lower.scale.z;
		Prop upper = standingOn(TOWER_LOW_SLAB_Z, TOWER_TOP_Z, height * 0.45f, roofY);
		upper.scale.x *= 0.8f;
		upper.scale.y *= 0.8f;
		upper.rot.y += 180.0f;
		props.push_back(lower);
		props.push_back(upper);
		break;
	}
	}
}

// Dead street lamps along both sidewalks of every road; some are bent,
// some are missing
void addStreetLamps(std::vector<Prop>& props, float g)
{
	int n = 0;
	for (int r = 0; r < NUM_ROADS; r++)
	{
		const Road& road = ROADS[r];
		for (int side = -1; side <= 1; side += 2)
		{
			for (float along = -CITY_HALF + 10.0f; along < CITY_HALF - 10.0f; along += 22.0f)
			{
				n++;
				if (inCrossing(road, along, SIDEWALK_W + 2.0f)) continue;
				if (n % 7 == 3) continue;	// this one has fallen or been taken
				glm::vec3 pos = roadPoint(road, along, road.center + side * (road.width * 0.5f + 0.6f), g + SIDEWALK_H);
				glm::vec3 rot(0.0f);
				if (n % 5 == 1) rot.z = (n % 2) ? 16.0f : -12.0f;	// bent
				if (n % 6 == 4) rot.x = side * 9.0f;				// leaning over the road
				props.push_back({ M_LAMPPOST, pos, glm::vec3(1.5f), rot });
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Main Application Entry Point
//-----------------------------------------------------------------------------
int main(int argc, char** argv)
{
	for (int i = 1; i < argc; i++)
	{
		std::string a = argv[i];
		if (a == "--demo") { gDemo = true; if (i + 1 < argc && argv[i + 1][0] != '-') gDemoPath = argv[++i]; }
		else if (a == "--record" && i + 1 < argc) { gDemo = true; gRecordFile = argv[++i]; }
		else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &gWindowWidth, &gWindowHeight);
	}
	if (gDemo && !loadDemoPath(gDemoPath)) return -1;

	if (!initOpenGL())
	{
		// An error occured
		std::cerr << "GLFW initialization failed" << std::endl;
		return -1;
	}

	ShaderProgram lightingShader;
	lightingShader.loadShaders("shaders/lighting_dir_point_spot.vert", "shaders/city.frag");

	ShaderProgram bulbShader;
	bulbShader.loadShaders("shaders/bulb.vert", "shaders/bulb.frag");

	ShaderProgram glowShader;
	glowShader.loadShaders("shaders/glow.vert", "shaders/glow.frag");

	ShaderProgram skyShader;
	skyShader.loadShaders("shaders/sky.vert", "shaders/sky.frag");

	ShaderProgram scanShader;
	scanShader.loadShaders("shaders/scan.vert", "shaders/scan.frag");

	// Every mesh is loaded once, then drawn as many times as needed through
	// the props list below (buildings, lamp posts...)
	Mesh mesh[NUM_MESHES];
	Texture2D texture[NUM_MESHES];

	mesh[M_BARREL].loadOBJ("models/barrel.obj");
	mesh[M_WOODCRATE].loadOBJ("models/woodcrate.obj");
	mesh[M_CRATE].loadOBJ("models/crate.obj");
	mesh[M_XBOT].loadGLB("models/xbot_4000.glb");
	mesh[M_LAMPPOST].loadOBJ("models/lampPost.obj");
	mesh[M_LITTLEBOT].loadGLB("models/LittleBotV1_textured.glb");
	mesh[M_TOWER].loadGLB("models/Tower_Block_abandoned.glb");
	mesh[M_SKELETON].loadOBJ("models/skeleton_sitting.obj");
	mesh[M_BOTTLE].loadVertices(buildBottle());
	mesh[M_MASK].loadGLB("models/masque_sans_cordon.glb");

	texture[M_BARREL].loadTexture("textures/barrel_diffuse.png", true);
	texture[M_WOODCRATE].loadTexture("textures/woodcrate_diffuse.jpg", true);
	texture[M_CRATE].loadTexture("textures/crate.jpg", true);
	texture[M_XBOT].loadGLB("models/xbot_4000.glb", true);
	texture[M_LAMPPOST].loadTexture("textures/lamp_post_diffuse.png", true);
	texture[M_LITTLEBOT].loadGLB("models/LittleBotV1_textured.glb", true);	// no image in this file: vertex colors + white texture
	texture[M_TOWER].loadGLB("models/Tower_Block_abandoned.glb", true);	// no image either (plain "concrete" material): vertex colors + white texture
	texture[M_SKELETON].loadTexture("textures/skeleton_albedo.jpg", true);
	texture[M_BOTTLE].loadTexture("textures/bottle_old.png", true);
	texture[M_MASK].loadGLB("models/masque_sans_cordon.glb", true);

	// The light bulb model is rendered separately (unlit / flat colored)
	Mesh lightMesh;
	lightMesh.loadOBJ("models/light.obj");

	// 11-45-G: the third robot from "Three Robots" (Love, Death & Robots)
	Mesh robot1145Mesh, robot1145EyeMesh;
	{
		std::vector<Vertex> bVerts, eVerts;
		build1145G(bVerts, eVerts);
		robot1145Mesh.loadVertices(bVerts);
		robot1145EyeMesh.loadVertices(eVerts);
	}
	Mesh scanBeamMesh;
	scanBeamMesh.loadVertices(buildScanBeam(5.8f, 2.2f));

	const glm::vec3 robot1145BasePos(3.9f, 0.0f, 0.45f); // grounded on the floor
	const glm::vec3 kScanColor(0.05f, 0.90f, 1.0f); // vibrant electric cyan scan
	const glm::vec3 kCamEyeLocal(0.0f, 2.14f, 0.28f); // camera lens position on 11-45-G

	// Everything (building + props) is raised together by this amount so the
	// whole composition sits higher up - tweak this single value to raise or
	// lower the scene instead of touching every position below.
	const float kSceneLift = 5.0f;
	const glm::vec3 robot1145Pos(robot1145BasePos.x, kSceneLift + 0.01f, robot1145BasePos.z);


	// The main tower: the walkable top of its middle floor is at y = kSceneLift
	// where the robots stand, and its lowest floor rests on the street (the
	// broken walls hanging under it go underground), one storey below.
	const float kTowerScale = 12.0f;
	const glm::vec3 towerPos(1.2f, kSceneLift - kTowerScale * TOWER_MID_FLOOR_TOP_Z, -0.8f);
	const float g = towerPos.y + kTowerScale * TOWER_LOW_SLAB_Z - SLAB_LIFT;	// street level

	// --- The robots' floor. The floor is only solid in a band (z from -1.5 to
	// 2) between the left wall and the right wall (inner face at x = 10.39),
	// with a big hole behind it: the scene is laid out along that band.
	// The old human world has become a museum: a dead man sits against the
	// right wall, a bottle in his hand, lit like an exhibit, and the robots
	// stand side by side at a respectful distance, contemplating him - xbot,
	// LittleBot perched on a crate, and 11-45-G standing on the floor between them.
	const float kWallX = 10.39f;
	const glm::vec3 skeletonPos(kWallX + SKELETON_BACK_Z - 0.03f, kSceneLift + 0.01f, 0.75f);	// z: every bone touching the floor is on solid floor
	const glm::vec3 exhibitCenter(skeletonPos.x - 0.4f, kSceneLift, skeletonPos.z);			// the dead man's chest, seen from above
	const glm::vec3 xbotPos(4.6f, kSceneLift, 1.6f);
	const glm::vec3 littleBotCratePos(2.5f, kSceneLift, -1.1f);
	const float kCrateHeight = 2.0f;
	// Yaw (degrees) turning a model that faces +Z towards a point
	auto facing = [](glm::vec3 from, glm::vec3 to) { return glm::degrees(std::atan2(to.x - from.x, to.z - from.z)); };
	const float robot1145Yaw = facing(robot1145Pos, exhibitCenter);

	std::vector<Prop> props = {
		{ M_SKELETON,  skeletonPos,                            glm::vec3(1.0f),  glm::vec3(0.0f, -90.0f, 0.0f) },	// back against the wall, facing -X
		// yaw -90: model (x, z) -> world (-z, x)
		{ M_BOTTLE,    glm::vec3(skeletonPos.x - SKELETON_RIGHT_HAND.z, kSceneLift, skeletonPos.z + SKELETON_RIGHT_HAND.x), glm::vec3(1.0f), glm::vec3(0.0f, 0.0f, 6.0f) },
		{ M_XBOT,      xbotPos,                                glm::vec3(7.5f),  glm::vec3(0.0f, facing(xbotPos, exhibitCenter) - 90.0f, 0.0f) },	// this OBJ faces +X, not +Z; ~0.62 units tall: ~4.7
		// The mask hangs on the right wall, next to the dead man's head, face towards -X.
		// The GLB lies face up (front = +Y, forehead = -Z, ~0.6 wide, 1.2 tall, centre (0.17, 0.49, 0.04));
		// the rotation stands it up and cancels its 19 degree tilt. Position = wanted centre - rotated centre
		// (the rotated centre is (-0.482, 0.117, 0.172)); the centre sits 0.1 off the wall, which touches its back.
		{ M_MASK,      glm::vec3(kWallX - 0.09f + 0.482f, kSceneLift + 1.7f - 0.117f, skeletonPos.z - 1.4f - 0.172f), glm::vec3(1.0f), glm::vec3(71.2f, -90.0f, 0.0f) },
		{ M_WOODCRATE, littleBotCratePos,                     glm::vec3(1.0f),  glm::vec3(0.0f, 12.0f, 0.0f) },
		{ M_LITTLEBOT, littleBotCratePos + glm::vec3(0.0f, kCrateHeight, 0.0f), glm::vec3(0.02f), glm::vec3(-90.0f, facing(littleBotCratePos, exhibitCenter), 0.0f) },	// Z-up GLB, perched on the crate
		{ M_BARREL,    glm::vec3(-3.0f, kSceneLift + 1.3f, 0.9f), glm::vec3(1.0f), glm::vec3(0.0f, 35.0f, 90.0f) },	// knocked over
		{ M_TOWER,     towerPos,                               glm::vec3(kTowerScale), glm::vec3(-90.0f, 0.0f, 0.0f) },
	};

	// Starting camera: behind the three visitors, the dead man seen between them
	const glm::vec3 camStart(-1.0f, kSceneLift + 4.3f, 0.5f), camTarget(9.0f, kSceneLift + 1.4f, 0.6f);
	glm::vec3 camDir = glm::normalize(camTarget - camStart);
	fpsCamera.setPosition(camStart);
	// the camera starts looking down -Z (yaw 180 degrees): turn it towards the scene
	fpsCamera.rotate(glm::degrees(std::atan2(camDir.x, camDir.z)) - 180.0f, glm::degrees(std::asin(camDir.y)));

	// --- The dead city around it. Blocks sit between the streets:
	// columns x in (-150,-38) (-22,24) (40,150), rows z in (-150,-46) (-30,14) (32,150)
	//                 x       z     width depth height  yaw    style   (height = visible, above the street)
	// Across the main road
	addBuilding(props, g, -120.0f,  45.0f, 18.0f, 16.0f, 22.0f,  90.0f, UPSIDE_DOWN);
	addBuilding(props, g,  -92.0f,  44.0f, 14.0f, 14.0f,  8.0f,   0.0f, COLLAPSED);
	addBuilding(props, g,  -64.0f,  46.0f, 18.0f, 18.0f, 34.0f, 180.0f, STANDING);
	addBuilding(props, g,  -47.0f,  42.0f, 10.0f, 10.0f, 26.0f,   8.0f, LEANING);
	addBuilding(props, g,   -6.0f,  46.0f, 20.0f, 16.0f, 24.0f, 180.0f, STACKED);
	addBuilding(props, g,   15.0f,  42.0f, 10.0f, 10.0f, 42.0f,   5.0f, STANDING);
	addBuilding(props, g,   58.0f,  46.0f, 20.0f, 18.0f, 16.0f, 270.0f, MIRRORED);
	addBuilding(props, g,   86.0f,  43.0f, 12.0f, 12.0f, 30.0f,   0.0f, LEANING);
	addBuilding(props, g,  118.0f,  46.0f, 16.0f, 16.0f, 20.0f,  90.0f, STACKED);
	// Same side as the tower, on both sides of it
	addBuilding(props, g,  -56.0f,  -6.0f, 16.0f, 20.0f, 28.0f,  90.0f, UPSIDE_DOWN);
	addBuilding(props, g,  -86.0f,  -4.0f, 14.0f, 14.0f, 44.0f,   0.0f, STANDING);
	addBuilding(props, g, -116.0f,  -8.0f, 18.0f, 16.0f,  9.0f, 180.0f, COLLAPSED);
	addBuilding(props, g,   56.0f,  -6.0f, 14.0f, 18.0f, 32.0f, 270.0f, LEANING);
	addBuilding(props, g,   84.0f,  -8.0f, 18.0f, 18.0f, 22.0f, 180.0f, STACKED);
	addBuilding(props, g,  114.0f,  -4.0f, 14.0f, 12.0f, 36.0f,  90.0f, MIRRORED);
	addBuilding(props, g,    0.0f, -22.0f, 14.0f,  8.0f,  6.0f,   0.0f, COLLAPSED);	// right behind the tower
	// Behind the back road: the skyline
	addBuilding(props, g, -104.0f, -60.0f, 16.0f, 16.0f, 30.0f, 270.0f, MIRRORED);
	addBuilding(props, g,  -60.0f, -60.0f, 18.0f, 16.0f, 46.0f,   0.0f, STACKED);
	addBuilding(props, g,   -2.0f, -62.0f, 20.0f, 18.0f, 36.0f,  90.0f, STANDING);
	addBuilding(props, g,   50.0f, -58.0f, 14.0f, 14.0f, 52.0f, 180.0f, LEANING);
	addBuilding(props, g,   98.0f, -62.0f, 18.0f, 16.0f, 24.0f,   0.0f, UPSIDE_DOWN);

	addStreetLamps(props, g);

	// Streets, ground and weeds are generated geometry, built once
	Mesh groundMesh, roadMesh, sidewalkMesh, grassMesh;
	groundMesh.loadVertices(buildGround(g));
	roadMesh.loadVertices(buildRoads(g));
	sidewalkMesh.loadVertices(buildSidewalks(g));
	grassMesh.loadVertices(buildGrass(g));

	Texture2D groundTex, roadTex, sidewalkTex, grassTex;
	groundTex.loadTexture("textures/ground_wasteland.png", true);
	roadTex.loadTexture("textures/road_asphalt.png", true);
	sidewalkTex.loadTexture("textures/sidewalk.png", true);
	grassTex.loadTexture("textures/grass_atlas.png", true);

	// Distance fog, same color as the sky, so the ground melts into the
	// horizon instead of ending on a hard edge in the void
	const float kFogDensity = 0.011f;

	// Every lamp post gets a light in its lantern. They are old: most still
	// glow, some flicker, the bent ones are mostly dead
	const glm::vec3 kLampColor(1.0f, 0.62f, 0.28f);	// old sodium/tungsten orange
	const float kLampCone = 0.09f;	// street lamps fade out ~85 degrees from straight down
	std::vector<LampLight> lamps;
	for (const Prop& p : props)
	{
		if (p.mesh != M_LAMPPOST) continue;
		int k = (int)lamps.size();
		LampMode mode = LAMP_STEADY;
		bool bent = p.rot.x != 0.0f || p.rot.z != 0.0f;
		if (bent) mode = (k % 3 == 0) ? LAMP_FLICKER : LAMP_DEAD;
		else if (k % 4 == 1) mode = LAMP_FLICKER;
		else if (k % 9 == 5) mode = LAMP_DEAD;
		glm::vec3 lantern = glm::vec3(propMatrix(p) * glm::vec4(0.0f, LANTERN_Y, 0.0f, 1.0f));
		// the glow sphere just covers the model's own bulb
		LampLight lamp;
		lamp.pos = lantern;
		lamp.bulbScale = 1.1f * BULB_RADIUS / LIGHT_OBJ_RADIUS * p.scale.y;	// the glow sphere just covers the model's own bulb
		lamp.mode = mode;
		lamp.seed = hash((float)k, 3.1f) * 10.0f;
		lamp.color = kLampColor * 1.8f;
		lamp.linear = 0.14f; lamp.exponent = 0.07f;
		lamp.cosCone = kLampCone;
		lamp.ceiling = false;
		lamps.push_back(lamp);
	}

	// Ceiling lights, on some floors of some buildings: cold neon, warm bulb
	// or sickly green, a good part of them flickering. Upside-down buildings
	// are skipped (their floors are ceilings), and so are floors underground.
	const glm::vec3 kCeilingColors[3] = { glm::vec3(0.75f, 0.88f, 1.0f), glm::vec3(1.0f, 0.72f, 0.4f), glm::vec3(0.7f, 1.0f, 0.72f) };
	// a = point of the tower mesh (local X, Y) under the ceiling of that level
	auto addCeilingLamp = [&](const Prop& building, int level, glm::vec2 a, LampMode mode, glm::vec3 color, float cosCone) {
		glm::mat4 m = propMatrix(building);
		glm::vec3 ceil = glm::vec3(m * glm::vec4(a.x, a.y, CEILING_Z[level], 1.0f));
		float floorY = (m * glm::vec4(a.x, a.y, CEILING_FLOOR_Z[level], 1.0f)).y;
		if (floorY < g - 0.1f || ceil.y - floorY < 2.5f) return;
		LampLight lamp;
		lamp.pos = ceil + glm::vec3(0.0f, FIXTURE_BULB_Y, 0.0f);	// cords hang straight down, even in a leaning building
		lamp.bulbScale = 0.1f / LIGHT_OBJ_RADIUS;
		lamp.mode = mode;
		lamp.seed = hash(ceil.x + ceil.z, 5.3f) * 10.0f;
		lamp.color = color * 1.4f;
		lamp.linear = 0.09f; lamp.exponent = 0.032f;	// floors are 6-11 units below: reaches further than a street lamp
		lamp.cosCone = cosCone;
		lamp.ceiling = true;
		lamps.push_back(lamp);
	};
	int towerIndex = 0;
	for (const Prop& b : props)
	{
		if (b.mesh != M_TOWER) continue;
		int k = towerIndex++;
		if (k == 0)
		{
			// Main tower: the exhibit light, right above the dead man (where the
			// ceiling above him is solid), a narrower cone like a museum spot;
			// and a flickering neon in the room below
			glm::vec2 aboveExhibit((exhibitCenter.x - towerPos.x) / kTowerScale, -(exhibitCenter.z - 0.85f - towerPos.z) / kTowerScale);
			addCeilingLamp(b, 1, aboveExhibit, LAMP_STEADY, glm::vec3(1.0f, 0.85f, 0.65f), 0.45f);
			addCeilingLamp(b, 0, CEILING_ANCHORS[0][0], LAMP_FLICKER, kCeilingColors[0], 0.0f);
			continue;
		}
		if (b.rot.x > 0.0f) continue;	// upside down
		for (int level = 0; level < NUM_CEILING_LEVELS; level++)
		{
			float h = hash((float)(k * 2 + level), 9.7f);
			if (h > 0.45f) continue;	// this floor stays dark
			int anchor = (int)(hash((float)(k * 2 + level), 4.2f) * NUM_CEILING_ANCHORS) % NUM_CEILING_ANCHORS;
			LampMode mode = hash((float)(k * 2 + level), 1.3f) < 0.4f ? LAMP_FLICKER : LAMP_STEADY;
			glm::vec3 color = kCeilingColors[(int)(hash((float)(k * 2 + level), 7.7f) * 3.0f) % 3];
			addCeilingLamp(b, level, CEILING_ANCHORS[level][anchor], mode, color, 0.0f);	// the shade: lights the half space below
			if (h < 0.12f)	// a second light on the same floor
				addCeilingLamp(b, level, CEILING_ANCHORS[level][(anchor + 3) % NUM_CEILING_ANCHORS], mode, color, 0.0f);
		}
	}
	const glm::vec3 kDeadBulbColor(0.06f, 0.05f, 0.04f);

	Mesh fixtureMesh;
	fixtureMesh.loadVertices(buildCeilingFixture());

	// Dim, cool point lights keeping xbot and LittleBot readable on the side
	// away from the lamp (rim light)
	const glm::vec3 rimLightPos[2] = {
		xbotPos + glm::vec3(-1.5f, 3.0f, 1.5f),
		littleBotCratePos + glm::vec3(-1.0f, kCrateHeight + 1.5f, 1.5f)
	};

	// --- The robots' own lights: xbot's two eye LEDs (orange-yellow and blue,
	// like XBOT 4000 in Three Robots) and LittleBot's glowing face screen.
	// Each gets an unlit emissive shape, a glow halo and a small point light.
	glm::mat4 xbotMatrix(1.0f), littleBotMatrix(1.0f);
	for (const Prop& p : props)
	{
		if (p.mesh == M_XBOT) xbotMatrix = propMatrix(p);
		if (p.mesh == M_LITTLEBOT) littleBotMatrix = propMatrix(p);
	}
	struct Glow { glm::vec3 pos; glm::vec3 color; float size; float intensity; };
	// xbot faces +X. Each eye is a tube (axis at local y 0.5715, z +-0.0127,
	// inner radius ~0.0099) with the lens at its back: a glowing disc fills
	// it, with a hotter core, like a lit LED. The +Z eye is on the left when
	// facing the robot.
	const glm::vec3 kXbotLeftEyeColor(1.0f, 0.62f, 0.12f), kXbotRightEyeColor(0.25f, 0.75f, 1.0f);
	const glm::vec3 xbotForward = glm::normalize(glm::vec3(xbotMatrix * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
	const glm::vec3 xbotEye[2] = {
		glm::vec3(xbotMatrix * glm::vec4(-0.0035f, 0.5715f, 0.0127f, 1.0f)),	// the lens, inside each tube
		glm::vec3(xbotMatrix * glm::vec4(-0.0035f, 0.5715f, -0.0127f, 1.0f)) };
	const glm::vec3 xbotEyeLocal[2] = { glm::vec3(-0.0035f, 0.5715f, 0.0127f), glm::vec3(-0.0035f, 0.5715f, -0.0127f) };
	const glm::vec3 xbotEyeColor[2] = { kXbotLeftEyeColor, kXbotRightEyeColor };
	// Unit disc in the YZ plane, facing +X
	Mesh ledDisc;
	{
		std::vector<Vertex> disc;
		const int SEG = 24;
		for (int k = 0; k < SEG; k++)
		{
			float a0 = k * glm::two_pi<float>() / SEG, a1 = (k + 1) * glm::two_pi<float>() / SEG;
			glm::vec3 p[4] = { glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f, std::cos(a1), std::sin(a1)), glm::vec3(0.0f, std::cos(a0), std::sin(a0)) };
			glm::vec2 uv[4] = { glm::vec2(0.5f), glm::vec2(0.5f), glm::vec2(0.5f), glm::vec2(0.5f) };
			pushQuad(disc, p, uv, glm::vec3(1, 0, 0));
		}
		ledDisc.loadVertices(disc);
	}
	// LittleBot's eyes and mouth (models/littlebot_face.obj, its "eyes_mouth"
	// node): the face looks along local -Y
	Mesh littleBotFace;
	littleBotFace.loadOBJ("models/littlebot_face.obj");
	const glm::vec3 kScreenColor(0.62f, 0.8f, 0.85f);
	const glm::vec3 littleBotForward = glm::normalize(glm::vec3(littleBotMatrix * glm::vec4(0.0f, -1.0f, 0.0f, 0.0f)));
	auto littleBotPoint = [&](float x, float y, float z) { return glm::vec3(littleBotMatrix * glm::vec4(x, y, z, 1.0f)); };

	std::vector<Glow> glows = {
		{ xbotEye[0] + xbotForward * 0.045f, xbotEyeColor[0], 0.10f, 0.9f },	// inside the tube opening (front of the tube is ~0.06 ahead of the lens)
		{ xbotEye[1] + xbotForward * 0.045f, xbotEyeColor[1], 0.10f, 0.9f },
		{ littleBotPoint(10.0f, -23.3f, 26.3f) + littleBotForward * 0.05f, kScreenColor, 0.28f, 0.35f },
		{ littleBotPoint(-10.4f, -23.3f, 26.4f) + littleBotForward * 0.05f, kScreenColor, 0.28f, 0.35f },
		{ littleBotPoint(-0.2f, -23.6f, 13.1f) + littleBotForward * 0.05f, kScreenColor, 0.36f, 0.35f },
	};

	Mesh glowQuad;
	{
		std::vector<Vertex> quad;
		glm::vec3 c[4] = { glm::vec3(-1, -1, 0), glm::vec3(1, -1, 0), glm::vec3(1, 1, 0), glm::vec3(-1, 1, 0) };
		glm::vec2 uv[4] = { glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1) };
		pushQuad(quad, c, uv, glm::vec3(0, 0, 1));
		glowQuad.loadVertices(quad);
	}

	Mesh skyMesh;
	{
		float s = 1.0f;
		glm::vec3 p[8] = {
			glm::vec3(-s, -s,  s), glm::vec3( s, -s,  s),
			glm::vec3( s,  s,  s), glm::vec3(-s,  s,  s),
			glm::vec3(-s, -s, -s), glm::vec3( s, -s, -s),
			glm::vec3( s,  s, -s), glm::vec3(-s,  s, -s)
		};
		int faces[6][4] = {
			{ 0, 1, 2, 3 }, // Front (+Z)
			{ 5, 4, 7, 6 }, // Back (-Z)
			{ 4, 0, 3, 7 }, // Left (-X)
			{ 1, 5, 6, 2 }, // Right (+X)
			{ 3, 2, 6, 7 }, // Top (+Y)
			{ 4, 5, 1, 0 }  // Bottom (-Y)
		};
		std::vector<Vertex> verts;
		for (int f = 0; f < 6; f++)
		{
			glm::vec3 facePts[4] = { p[faces[f][0]], p[faces[f][1]], p[faces[f][2]], p[faces[f][3]] };
			glm::vec2 uv[4] = { glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1) };
			pushQuad(verts, facePts, uv, glm::vec3(0, 1, 0));
		}
		skyMesh.loadVertices(verts);
	}

	// Recording: frames go to FFmpeg through a pipe
	FILE* ffmpegPipe = NULL;
	int recW = 0, recH = 0;
	std::vector<unsigned char> frameBuffer;
	if (!gRecordFile.empty())
	{
		glfwGetFramebufferSize(gWindow, &recW, &recH);
		std::ostringstream cmd;
		cmd << "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s " << recW << "x" << recH << " -r " << (int)RECORD_FPS
			<< " -i - -vf \"vflip,scale=trunc(iw/2)*2:trunc(ih/2)*2\" -c:v libx264 -preset medium -crf 16 -pix_fmt yuv420p \"" << gRecordFile << "\"";
		ffmpegPipe = popen(cmd.str().c_str(), "wb");
		if (!ffmpegPipe) { std::cerr << "Cannot start ffmpeg" << std::endl; return -1; }
		frameBuffer.resize((size_t)recW * recH * 3);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
	}
	long frameIndex = 0;
	double demoStart = glfwGetTime();
	double lastTime = demoStart;

	// Rendering loop
	while (!glfwWindowShouldClose(gWindow))
	{
		showFPS(gWindow);

		// Fixed clock when recording (frame / 60), real time otherwise
		double currentTime = ffmpegPipe ? frameIndex / RECORD_FPS : glfwGetTime();
		double deltaTime = currentTime - lastTime;

		// Poll for and process events
		glfwPollEvents();
		if (gDemo)
		{
			double demoTime = ffmpegPipe ? currentTime : currentTime - demoStart;
			if (!applyDemo(demoTime)) break;
		}
		else
			update(deltaTime);

		// Clear the screen
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glm::mat4 model(1.0), view(1.0), projection(1.0);

		// Create the View matrix
		view = fpsCamera.getViewMatrix();

		// Create the projection matrix
		projection = glm::perspective(glm::radians(fpsCamera.getFOV()), (float)gWindowWidth / (float)gWindowHeight, 0.1f, 300.0f);

		// Render skybox (drawn first with depth test disabled so all 3D geometry draws on top)
		if (gWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glDisable(GL_DEPTH_TEST);
		skyShader.use();
		skyShader.setUniform("view", view);
		skyShader.setUniform("projection", projection);
		skyShader.setUniform("fogColor", glm::vec3(gClearColor));
		skyShader.setUniform("moonDir", glm::normalize(glm::vec3(-0.35f, 0.75f, -0.45f)));
		skyShader.setUniform("time", (float)currentTime);
		skyMesh.draw();
		glEnable(GL_DEPTH_TEST);
		if (gWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

		glm::vec3 viewPos = fpsCamera.getPosition();

		// Must be called BEFORE setting uniforms because setting uniforms is done
		// on the currently active shader program.
		lightingShader.use();
		lightingShader.setUniform("view", view);
		lightingShader.setUniform("projection", projection);
		lightingShader.setUniform("viewPos", viewPos);

		// Directional light - cold moonlight
		lightingShader.setUniform("sunLight.direction", glm::vec3(0.35f, -0.75f, 0.45f));
		lightingShader.setUniform("sunLight.ambient", glm::vec3(0.07f, 0.08f, 0.12f));
		lightingShader.setUniform("sunLight.diffuse", glm::vec3(0.20f, 0.23f, 0.34f));
		lightingShader.setUniform("sunLight.specular", glm::vec3(0.12f, 0.14f, 0.2f));

		// Point lights: the 2 rim lights, then the lit lamps closest to the
		// camera (there are more lamps than the shader has lights)
		auto setPointLight = [&](int i, glm::vec3 pos, glm::vec3 color, float ambient, float linear, float exponent, float cosCone) {
			std::string n = "pointLights[" + std::to_string(i) + "].";
			lightingShader.setUniform((n + "position").c_str(), pos);
			lightingShader.setUniform((n + "ambient").c_str(), color * ambient);
			lightingShader.setUniform((n + "diffuse").c_str(), color);
			lightingShader.setUniform((n + "specular").c_str(), color * 0.6f);
			lightingShader.setUniform((n + "constant").c_str(), 1.0f);
			lightingShader.setUniform((n + "linear").c_str(), linear);
			lightingShader.setUniform((n + "exponent").c_str(), exponent);
			lightingShader.setUniform((n + "cosCone").c_str(), cosCone);
		};
		// 11-45-G: grounded robot standing firmly on the floor
		glm::mat4 robot1145Matrix = glm::translate(glm::mat4(1.0), robot1145Pos) *
			glm::rotate(glm::mat4(1.0), glm::radians(robot1145Yaw), glm::vec3(0.0f, 1.0f, 0.0f));

		glm::vec3 camEyeWorld = glm::vec3(robot1145Matrix * glm::vec4(kCamEyeLocal, 1.0f));

		// Scanning beam animation: pitches gently up and down, scanning the skeleton from head to toe
		float scanPitch = -3.5f + 6.5f * std::sin((float)currentTime * 1.6f);
		float scanYaw = 1.8f * std::cos((float)currentTime * 1.2f);
		glm::mat4 scanBeamMatrix = glm::translate(glm::mat4(1.0), camEyeWorld) *
			glm::rotate(glm::mat4(1.0), glm::radians(robot1145Yaw + scanYaw), glm::vec3(0.0f, 1.0f, 0.0f)) *
			glm::rotate(glm::mat4(1.0), glm::radians(scanPitch), glm::vec3(1.0f, 0.0f, 0.0f));

		glm::vec3 beamForward = glm::normalize(glm::vec3(scanBeamMatrix * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
		float beamDist = 5.8f;
		glm::vec3 scanImpactPos = camEyeWorld + beamForward * beamDist;
		float scanPulse = 0.85f + 0.15f * std::sin((float)currentTime * 16.0f);

		const float kOmni = -2.0f;
		setPointLight(0, rimLightPos[0], glm::vec3(0.15f, 0.18f, 0.28f), 0.1f, 0.09f, 0.03f, kOmni);
		setPointLight(1, rimLightPos[1], glm::vec3(0.12f, 0.15f, 0.24f), 0.1f, 0.09f, 0.03f, kOmni);
		// the robots' glow on what is right around them (short reach)
		setPointLight(2, xbotEye[0] + xbotForward * 0.07f, xbotEyeColor[0] * 0.25f, 0.0f, 0.9f, 2.5f, kOmni);
		setPointLight(3, xbotEye[1] + xbotForward * 0.07f, xbotEyeColor[1] * 0.25f, 0.0f, 0.9f, 2.5f, kOmni);
		setPointLight(4, littleBotPoint(0.0f, -23.4f, 20.0f) + littleBotForward * 0.25f, kScreenColor * 0.3f, 0.0f, 0.5f, 0.8f, kOmni);
		// 11-45-G's laser scan line softly illuminating the skeleton (attenuated by distance dissolution)
		setPointLight(5, scanImpactPos, kScanColor * (0.08f * scanPulse), 0.0f, 0.8f, 1.8f, kOmni);

		float t = (float)currentTime;
		std::vector<float> intensity(lamps.size());
		std::vector<std::pair<float, int>> lit;
		for (size_t i = 0; i < lamps.size(); i++)
		{
			intensity[i] = lampIntensity(lamps[i], t);
			if (intensity[i] > 0.05f)
				lit.push_back({ glm::length(lamps[i].pos - viewPos), (int)i });
		}
		int numLamps = std::min((int)lit.size(), MAX_LAMP_LIGHTS);
		std::partial_sort(lit.begin(), lit.begin() + numLamps, lit.end());
		for (int i = 0; i < numLamps; i++)
		{
			const LampLight& lamp = lamps[lit[i].second];
			setPointLight(NUM_FIXED_LIGHTS + i, lamp.pos, lamp.color * intensity[lit[i].second], 0.02f, lamp.linear, lamp.exponent, lamp.cosCone);
		}
		lightingShader.setUniform("numPointLights", NUM_FIXED_LIGHTS + numLamps);

		// Spot light - flashlight attached to the camera, toggle with F
		glm::vec3 spotlightPos = fpsCamera.getPosition();
		spotlightPos.y -= 0.5f; // offset the flash light down a little

		lightingShader.setUniform("spotLight.ambient", glm::vec3(0.1f, 0.1f, 0.1f));
		lightingShader.setUniform("spotLight.diffuse", glm::vec3(0.8f, 0.8f, 0.8f));
		lightingShader.setUniform("spotLight.specular", glm::vec3(1.0f, 1.0f, 1.0f));
		lightingShader.setUniform("spotLight.position", spotlightPos);
		lightingShader.setUniform("spotLight.direction", fpsCamera.getLook());
		lightingShader.setUniform("spotLight.cosInnerCone", glm::cos(glm::radians(15.0f)));
		lightingShader.setUniform("spotLight.cosOuterCone", glm::cos(glm::radians(20.0f)));
		lightingShader.setUniform("spotLight.constant", 1.0f);
		lightingShader.setUniform("spotLight.linear", 0.07f);
		lightingShader.setUniform("spotLight.exponent", 0.017f);
		lightingShader.setUniform("spotLight.on", gFlashlightOn);

		lightingShader.setUniform("fogColor", glm::vec3(gClearColor));
		lightingShader.setUniform("fogDensity", kFogDensity);

		// Same material for everything - the simple OBJ loader has no materials
		lightingShader.setUniform("material.ambient", glm::vec3(0.1f, 0.1f, 0.1f));
		lightingShader.setUniformSampler("material.diffuseMap", 0);
		lightingShader.setUniform("material.specular", glm::vec3(0.8f, 0.8f, 0.8f));
		lightingShader.setUniform("material.shininess", 32.0f);

		for (const Prop& p : props)
		{
			lightingShader.setUniform("model", propMatrix(p));
			if (p.mesh == M_SKELETON)	// old bone is matte
				lightingShader.setUniform("material.specular", glm::vec3(0.12f, 0.12f, 0.12f));

			texture[p.mesh].bind(0);
			mesh[p.mesh].draw();
			texture[p.mesh].unbind(0);

			if (p.mesh == M_SKELETON)
				lightingShader.setUniform("material.specular", glm::vec3(0.8f, 0.8f, 0.8f));
		}

		// City surfaces: already in world space, and matte (no shiny asphalt)
		lightingShader.setUniform("model", glm::mat4(1.0));
		lightingShader.setUniform("material.specular", glm::vec3(0.08f, 0.08f, 0.08f));
		lightingShader.setUniform("material.shininess", 8.0f);

		groundTex.bind(0);
		groundMesh.draw();
		roadTex.bind(0);
		roadMesh.draw();
		sidewalkTex.bind(0);
		sidewalkMesh.draw();

		// Ceiling lamp fixtures (dark metal: the lamp post texture)
		lightingShader.setUniform("material.specular", glm::vec3(0.5f, 0.5f, 0.5f));
		lightingShader.setUniform("material.shininess", 32.0f);
		texture[M_LAMPPOST].bind(0);
		for (const LampLight& lamp : lamps)
		{
			if (!lamp.ceiling) continue;
			lightingShader.setUniform("model", glm::translate(glm::mat4(1.0), lamp.pos - glm::vec3(0.0f, FIXTURE_BULB_Y, 0.0f)));
			fixtureMesh.draw();
		}
		lightingShader.setUniform("model", glm::mat4(1.0));

		// Grass tufts: transparent texels of the atlas are cut out
		lightingShader.setUniform("alphaCutout", 0.5f);
		grassTex.bind(0);
		grassMesh.draw();
		grassTex.unbind(0);
		lightingShader.setUniform("alphaCutout", 0.0f);

		// 11-45-G: ceramic & titanium body (white texture preserves vertex colors)
		lightingShader.setUniform("model", robot1145Matrix);
		lightingShader.setUniform("material.specular", glm::vec3(0.35f, 0.35f, 0.40f));
		lightingShader.setUniform("material.shininess", 24.0f);
		texture[M_TOWER].bind(0);
		robot1145Mesh.draw();
		texture[M_TOWER].unbind(0);

		// A glowing bulb in every lantern (unlit shader), as bright as its light
		bulbShader.use();
		bulbShader.setUniform("view", view);
		bulbShader.setUniform("projection", projection);
		for (size_t i = 0; i < lamps.size(); i++)
		{
			glm::vec3 color = glm::min(lamps[i].color, glm::vec3(1.0f)) * intensity[i];
			bulbShader.setUniform("lightColor", glm::max(color, kDeadBulbColor));
			bulbShader.setUniform("model", glm::translate(glm::mat4(1.0), lamps[i].pos) * glm::scale(glm::mat4(1.0), glm::vec3(lamps[i].bulbScale)));
			lightMesh.draw();
		}

		// LittleBot's eyes and mouth, unlit: lit from inside. Pulled slightly
		// towards the camera in depth so they win over the same face drawn lit.
		bulbShader.setUniform("lightColor", kScreenColor);
		bulbShader.setUniform("model", littleBotMatrix);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-1.0f, -1.0f);
		littleBotFace.draw();
		glDisable(GL_POLYGON_OFFSET_FILL);

		// xbot's eye LEDs: a glowing disc filling the tube, and a hotter core
		for (int e = 0; e < 2; e++)
		{
			glm::mat4 lens = xbotMatrix * glm::translate(glm::mat4(1.0), xbotEyeLocal[e]);
			bulbShader.setUniform("lightColor", xbotEyeColor[e]);
			bulbShader.setUniform("model", lens * glm::scale(glm::mat4(1.0), glm::vec3(1.0f, 0.0085f, 0.0085f)));
			ledDisc.draw();
			bulbShader.setUniform("lightColor", glm::mix(xbotEyeColor[e], glm::vec3(1.0f), 0.6f));
			bulbShader.setUniform("model", lens * glm::translate(glm::mat4(1.0), glm::vec3(0.0003f, 0.0f, 0.0f)) * glm::scale(glm::mat4(1.0), glm::vec3(1.0f, 0.0035f, 0.0035f)));
			ledDisc.draw();
		}

		// 11-45-G's glowing optical aperture / eye
		bulbShader.setUniform("lightColor", kScanColor * (0.70f + 0.12f * std::sin((float)currentTime * 4.0f)));
		bulbShader.setUniform("model", robot1145Matrix);
		robot1145EyeMesh.draw();

		// Glow halos: additive, and not writing depth so they never hide anything
		glowShader.use();
		glowShader.setUniform("view", view);
		glowShader.setUniform("projection", projection);
		glowShader.setUniform("cameraRight", glm::vec3(view[0][0], view[1][0], view[2][0]));
		glowShader.setUniform("cameraUp", glm::vec3(view[0][1], view[1][1], view[2][1]));
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
		glDepthMask(GL_FALSE);
		for (const Glow& glow : glows)
		{
			glowShader.setUniform("center", glow.pos);
			glowShader.setUniform("size", glow.size);
			glowShader.setUniform("glowColor", glow.color);
			glowShader.setUniform("intensity", glow.intensity);
			glowQuad.draw();
		}


		// 11-45-G's camera lens halo (subtle)
		glowShader.setUniform("center", camEyeWorld + beamForward * 0.04f);
		glowShader.setUniform("size", 0.20f);
		glowShader.setUniform("glowColor", kScanColor);
		glowShader.setUniform("intensity", 0.30f * scanPulse);
		glowQuad.draw();

		// Very discreet laser scanline contact glow on the target
		glowShader.setUniform("center", scanImpactPos);
		glowShader.setUniform("size", 0.18f);
		glowShader.setUniform("glowColor", kScanColor);
		glowShader.setUniform("intensity", 0.05f * scanPulse);
		glowQuad.draw();

		// Planar laser scanning line (ultra-thin planar fan with distance dissolution, soft and subtle)
		scanShader.use();
		scanShader.setUniform("view", view);
		scanShader.setUniform("projection", projection);
		scanShader.setUniform("model", scanBeamMatrix);
		scanShader.setUniform("scanColor", kScanColor);
		scanShader.setUniform("time", (float)currentTime);
		scanShader.setUniform("intensity", 0.60f);
		scanShader.setUniform("beamLength", beamDist);
		scanBeamMesh.draw();

		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);

		if (ffmpegPipe)
		{
			glReadPixels(0, 0, recW, recH, GL_RGB, GL_UNSIGNED_BYTE, frameBuffer.data());
			fwrite(frameBuffer.data(), 1, frameBuffer.size(), ffmpegPipe);
		}
		frameIndex++;

		// Swap front and back buffers
		glfwSwapBuffers(gWindow);

		lastTime = currentTime;
	}

	if (ffmpegPipe) pclose(ffmpegPipe);	// lets ffmpeg finish the file
	glfwTerminate();

	return 0;
}

//-----------------------------------------------------------------------------
// Initialize GLFW and OpenGL
//-----------------------------------------------------------------------------
bool initOpenGL()
{
#ifndef _WIN32
	// Force the X11 backend: on WSLg, GLFW's Wayland/EGL path fails to find a
	// GPU device ("failed to get driver name for fd -1"), which then makes
	// glewInit() fail. X11/GLX works reliably in that environment. Native
	// Windows has no X11 platform at all, so this must not run there.
	glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif

	// Intialize GLFW
	// GLFW is configured.  Must be called before calling any GLFW functions
	if (!glfwInit())
	{
		// An error occured
		std::cerr << "GLFW initialization failed" << std::endl;
		return false;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);	// forward compatible with newer versions of OpenGL as they become available but not backward compatible (it will not run on devices that do not support OpenGL 3.3
	glfwWindowHint(GLFW_RESIZABLE, gDemo ? GLFW_FALSE : GLFW_TRUE);	// allow dragging the window edges to resize it (fixed size when filming)

	// Create an OpenGL 3.3 core, forward compatible context window
	gWindow = glfwCreateWindow(gWindowWidth, gWindowHeight, APP_TITLE, NULL, NULL);
	if (gWindow == NULL)
	{
		std::cerr << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return false;
	}

	// Make the window's context the current one
	glfwMakeContextCurrent(gWindow);

	// Sync to the monitor's refresh rate. Without this the app was rendering
	// uncapped (measured ~300 FPS), which just produces tearing/stutter
	// instead of anything smoother - capping to vsync is what actually looks
	// fluid.
	glfwSwapInterval(gRecordFile.empty() ? 1 : 0);	// no vsync when recording: frames are rendered as fast as possible

	// Initialize GLEW
	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK)
	{
		std::cerr << "Failed to initialize GLEW" << std::endl;
		return false;
	}

	// Set the required callback functions
	glfwSetKeyCallback(gWindow, glfw_onKey);
	glfwSetMouseButtonCallback(gWindow, glfw_onMouseButton);
	glfwSetFramebufferSizeCallback(gWindow, glfw_onFramebufferSize);
	glfwSetScrollCallback(gWindow, glfw_onMouseScroll);

	// Cursor stays normal/visible until the user clicks in the window (see
	// captureMouse()), instead of being grabbed right from the start

	glClearColor(gClearColor.r, gClearColor.g, gClearColor.b, gClearColor.a);

	// Define the viewport dimensions
	int w, h;
	glfwGetFramebufferSize(gWindow, &w, &h); // For retina display
	glViewport(0, 0, w, h);

	glEnable(GL_DEPTH_TEST);

	return true;
}

//-----------------------------------------------------------------------------
// Is called whenever a key is pressed/released via GLFW
//-----------------------------------------------------------------------------
void glfw_onKey(GLFWwindow* window, int key, int scancode, int action, int mode)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GL_TRUE);

	if (key == GLFW_KEY_F1 && action == GLFW_PRESS)
	{
		gWireframe = !gWireframe;
		if (gWireframe)
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		else
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}

	if (key == GLFW_KEY_F && action == GLFW_PRESS)
	{
		// toggle the flashlight
		gFlashlightOn = !gFlashlightOn;
	}

	if (key == GLFW_KEY_F11 && action == GLFW_PRESS)
	{
		// The mouse cursor is captured by the app, so the window manager's own
		// maximize button isn't reachable - toggle real fullscreen instead
		toggleFullscreen();
	}
}

//-----------------------------------------------------------------------------
// Switches between windowed and fullscreen (borderless, native resolution)
//-----------------------------------------------------------------------------
void toggleFullscreen()
{
	if (!gFullscreen)
	{
		// Remember the windowed geometry so we can restore it later
		glfwGetWindowPos(gWindow, &gWindowedX, &gWindowedY);
		glfwGetWindowSize(gWindow, &gWindowedWidth, &gWindowedHeight);

		GLFWmonitor* monitor = glfwGetPrimaryMonitor();
		const GLFWvidmode* mode = glfwGetVideoMode(monitor);
		glfwSetWindowMonitor(gWindow, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
		gFullscreen = true;
	}
	else
	{
		glfwSetWindowMonitor(gWindow, NULL, gWindowedX, gWindowedY, gWindowedWidth, gWindowedHeight, 0);
		gFullscreen = false;
	}
}

//-----------------------------------------------------------------------------
// Is called whenever a mouse button is pressed/released via GLFW
//-----------------------------------------------------------------------------
void glfw_onMouseButton(GLFWwindow* window, int button, int action, int mods)
{
	// Just flag the request - the actual capture happens at the start of the
	// next update(), see the comment on gMouseCaptureRequested above.
	if (!gMouseCaptured && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
		gMouseCaptureRequested = true;
}

//-----------------------------------------------------------------------------
// Is called when the window is resized
//-----------------------------------------------------------------------------
void glfw_onFramebufferSize(GLFWwindow* window, int width, int height)
{
	gWindowWidth = width;
	gWindowHeight = height;

	// Define the viewport dimensions
	int w, h;
	glfwGetFramebufferSize(gWindow, &w, &h); // For retina display
	glViewport(0, 0, w, h);
}

//-----------------------------------------------------------------------------
// Called by GLFW when the mouse wheel is rotated
//-----------------------------------------------------------------------------
void glfw_onMouseScroll(GLFWwindow* window, double deltaX, double deltaY)
{
	double fov = fpsCamera.getFOV() + deltaY * ZOOM_SENSITIVITY;

	fov = glm::clamp(fov, 1.0, 120.0);

	fpsCamera.setFOV((float)fov);
}

//-----------------------------------------------------------------------------
// Update stuff every frame
//-----------------------------------------------------------------------------
void update(double elapsedTime)
{
	// Camera orientation - only once the mouse has been captured by a click.
	// The capture itself happens here, at the start of a frame, rather than
	// inside the click callback - doing it there left the very first cursor
	// reading in a bad state, seen as the camera pitch snapping and getting
	// stuck looking up regardless of which way the mouse moved afterwards.
	if (gMouseCaptureRequested)
	{
		gMouseCaptureRequested = false;
		gMouseCaptured = true;
		glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		glfwSetCursorPos(gWindow, gWindowWidth / 2.0, gWindowHeight / 2.0);
	}
	else if (gMouseCaptured)
	{
		double mouseX, mouseY;

		// Get the current mouse cursor position delta
		glfwGetCursorPos(gWindow, &mouseX, &mouseY);

		// Rotate the camera the difference in mouse distance from the center screen.  Multiply this delta by a speed scaler
		fpsCamera.rotate((float)(gWindowWidth / 2.0 - mouseX) * MOUSE_SENSITIVITY, (float)(gWindowHeight / 2.0 - mouseY) * MOUSE_SENSITIVITY);

		// Clamp mouse cursor to center of screen
		glfwSetCursorPos(gWindow, gWindowWidth / 2.0, gWindowHeight / 2.0);
	}

	// Camera FPS movement
	float currentSpeed = MOVE_SPEED;
	if (glfwGetKey(gWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(gWindow, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
		currentSpeed *= SPRINT_FACTOR;

	// Forward/backward
	if (glfwGetKey(gWindow, GLFW_KEY_W) == GLFW_PRESS)
		fpsCamera.move(currentSpeed * (float)elapsedTime * fpsCamera.getLook());
	else if (glfwGetKey(gWindow, GLFW_KEY_S) == GLFW_PRESS)
		fpsCamera.move(currentSpeed * (float)elapsedTime * -fpsCamera.getLook());

	// Strafe left/right
	if (glfwGetKey(gWindow, GLFW_KEY_A) == GLFW_PRESS)
		fpsCamera.move(currentSpeed * (float)elapsedTime * -fpsCamera.getRight());
	else if (glfwGetKey(gWindow, GLFW_KEY_D) == GLFW_PRESS)
		fpsCamera.move(currentSpeed * (float)elapsedTime * fpsCamera.getRight());

	// Up/down
	if (glfwGetKey(gWindow, GLFW_KEY_Z) == GLFW_PRESS)
		fpsCamera.move(currentSpeed * (float)elapsedTime * glm::vec3(0.0f, 1.0f, 0.0f));
	else if (glfwGetKey(gWindow, GLFW_KEY_X) == GLFW_PRESS)
		fpsCamera.move(currentSpeed * (float)elapsedTime * -glm::vec3(0.0f, 1.0f, 0.0f));
}

//-----------------------------------------------------------------------------
// Code computes the average frames per second, and also the average time it takes
// to render one frame.  These stats are appended to the window caption bar.
//-----------------------------------------------------------------------------
void showFPS(GLFWwindow* window)
{
	static double previousSeconds = 0.0;
	static int frameCount = 0;
	double elapsedSeconds;
	double currentSeconds = glfwGetTime(); // returns number of seconds since GLFW started, as double float

	elapsedSeconds = currentSeconds - previousSeconds;

	// Limit text updates to 4 times per second
	if (elapsedSeconds > 0.25)
	{
		previousSeconds = currentSeconds;
		double fps = (double)frameCount / elapsedSeconds;
		double msPerFrame = 1000.0 / fps;

		// The C++ way of setting the window title
		std::ostringstream outs;
		outs.precision(3);	// decimal places
		outs << std::fixed
			<< APP_TITLE << "    "
			<< "FPS: " << fps << "    "
			<< "Frame Time: " << msPerFrame << " (ms)";
		glfwSetWindowTitle(window, outs.str().c_str());

		// Reset for next average.
		frameCount = 0;
	}

	frameCount++;
}
