//-----------------------------------------------------------------------------
// Simple 2D texture class
//-----------------------------------------------------------------------------
#include "Texture2D.h"
#include "GLB.h"
#include <iostream>
#include <cassert>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image/stb_image.h"

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
Texture2D::Texture2D()
	: mTexture(0)
{
}

//-----------------------------------------------------------------------------
// Destructor
//-----------------------------------------------------------------------------
Texture2D::~Texture2D()
{
	glDeleteTextures(1, &mTexture);
}

//-----------------------------------------------------------------------------
// Load a texture with a given filename using stb image loader
// http://nothings.org/stb_image.h
// Creates mip maps if generateMipMaps is true.
//-----------------------------------------------------------------------------
bool Texture2D::loadTexture(const string& fileName, bool generateMipMaps)
{
	int width, height, components;

	// Use stbi image library to load our image
	unsigned char* imageData = stbi_load(fileName.c_str(), &width, &height, &components, STBI_rgb_alpha);

	if (imageData == NULL)
	{
		std::cerr << "Error loading texture '" << fileName << "'" << std::endl;
		return false;
	}

	bool result = createTexture(imageData, width, height, generateMipMaps);
	stbi_image_free(imageData);
	return result;
}

//-----------------------------------------------------------------------------
// Load the base color texture embedded in a binary glTF 2.0 (.glb) file.
// Uses the base color of the first material that has one, or the first image
// of the file otherwise. Without any image, creates a 1x1 white texture.
//-----------------------------------------------------------------------------
bool Texture2D::loadGLB(const string& fileName, bool generateMipMaps)
{
	GLBFile glb;
	if (!glb.load(fileName))
		return false;

	const JsonValue& json = glb.json;

	int imageIndex = -1;
	const JsonValue& materials = json["materials"];
	for (size_t i = 0; i < materials.size() && imageIndex < 0; i++)
	{
		const JsonValue& baseColor = materials[i]["pbrMetallicRoughness"]["baseColorTexture"];
		if (baseColor.has("index"))
			imageIndex = json["textures"][baseColor["index"].asInt()]["source"].asInt();
	}
	if (imageIndex < 0 && json["images"].size() > 0)
		imageIndex = 0;

	const JsonValue& image = json["images"][imageIndex];
	if (imageIndex < 0)
	{
		// No texture at all (e.g. a model colored with vertex colors): use a
		// 1x1 white texture so the vertex colors are displayed unchanged
		std::cout << "No texture in " << fileName << ", using a white texture" << std::endl;
		unsigned char white[4] = { 255, 255, 255, 255 };
		return createTexture(white, 1, 1, generateMipMaps);
	}
	if (!image.has("bufferView"))
	{
		std::cerr << "Error loading texture '" << fileName << "': only embedded images are supported" << std::endl;
		return false;
	}

	size_t length = 0;
	const unsigned char* data = glb.bufferViewData(image["bufferView"].asInt(), &length);
	if (!data)
	{
		std::cerr << "Error loading texture '" << fileName << "': invalid image data" << std::endl;
		return false;
	}

	// The image is a PNG or JPEG file stored in the BIN chunk
	int width, height, components;
	unsigned char* imageData = stbi_load_from_memory(data, (int)length, &width, &height, &components, STBI_rgb_alpha);
	if (imageData == NULL)
	{
		std::cerr << "Error loading texture '" << fileName << "': " << stbi_failure_reason() << std::endl;
		return false;
	}

	std::cout << "Loading GLB texture " << fileName << " (" << width << "x" << height << ")" << std::endl;

	bool result = createTexture(imageData, width, height, generateMipMaps);
	stbi_image_free(imageData);
	return result;
}

//-----------------------------------------------------------------------------
// Create the OpenGL texture from RGBA pixels (flipped vertically in place)
//-----------------------------------------------------------------------------
bool Texture2D::createTexture(unsigned char* imageData, int width, int height, bool generateMipMaps)
{
	// Invert image
	int widthInBytes = width * 4;
	unsigned char *top = NULL;
	unsigned char *bottom = NULL;
	unsigned char temp = 0;
	int halfHeight = height / 2;
	for (int row = 0; row < halfHeight; row++)
	{
		top = imageData + row * widthInBytes;
		bottom = imageData + (height - row - 1) * widthInBytes;
		for (int col = 0; col < widthInBytes; col++)
		{ 
			temp = *top;
			*top = *bottom;
			*bottom = temp;
			top++;
			bottom++;
		}
	}

	glGenTextures(1, &mTexture);
	glBindTexture(GL_TEXTURE_2D, mTexture); // all upcoming GL_TEXTURE_2D operations will affect our texture object (mTexture)

	// Set the texture wrapping/filtering options (on the currently bound texture object)
	// GL_CLAMP_TO_EDGE
	// GL_REPEAT
	// GL_MIRRORED_REPEAT
	// GL_CLAMP_TO_BORDER
	// GL_LINEAR
	// GL_NEAREST
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, imageData);

	if (generateMipMaps)
		glGenerateMipmap(GL_TEXTURE_2D);

	glBindTexture(GL_TEXTURE_2D, 0); // unbind texture when done so we don't accidentally mess up our mTexture

	return true;
}

//-----------------------------------------------------------------------------
// Bind the texture unit passed in as the active texture in the shader
//-----------------------------------------------------------------------------
void Texture2D::bind(GLuint texUnit)
{
	assert(texUnit >= 0 && texUnit < 32);

	glActiveTexture(GL_TEXTURE0 + texUnit);
	glBindTexture(GL_TEXTURE_2D, mTexture);
}

//-----------------------------------------------------------------------------
// Unbind the texture unit passed in as the active texture in the shader
//-----------------------------------------------------------------------------
void Texture2D::unbind(GLuint texUnit)
{
	glActiveTexture(GL_TEXTURE0 + texUnit);
	glBindTexture(GL_TEXTURE_2D, 0);
}
