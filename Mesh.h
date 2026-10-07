//-----------------------------------------------------------------------------
// Basic Mesh class
//-----------------------------------------------------------------------------
#ifndef MESH_H
#define MESH_H

#include <vector>
#include <string>

#define GLEW_STATIC
#include "GL/glew.h"	// Important - this header must come before glfw3 header
#include "glm/glm.hpp"


struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 texCoords;
	glm::vec3 color = glm::vec3(1.0f);	// vertex color, multiplied with the texture
};

class Mesh
{
public:

	 Mesh();
	~Mesh();

	bool loadOBJ(const std::string& filename);
	bool loadGLB(const std::string& filename);
	bool loadVertices(const std::vector<Vertex>& vertices);	// procedurally generated triangles
	void draw();

private:

	void initBuffers();

	bool mLoaded;
	std::vector<Vertex> mVertices;
	GLuint mVBO, mVAO;
};
#endif //MESH_H
