//-----------------------------------------------------------------------------
// Basic Mesh class
//-----------------------------------------------------------------------------
#include "Mesh.h"
#include "GLB.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"


//-----------------------------------------------------------------------------
// split
//
// Params:  s - string to split
//		    t - string to split (ie. delimiter)
//
//Result:  Splits string according to some substring and returns it as a vector.
//-----------------------------------------------------------------------------
std::vector<std::string> split(std::string s, std::string t)
{
	std::vector<std::string> res;
	while(1)
	{
		int pos = s.find(t);
		if(pos == -1)
		{
			res.push_back(s); 
			break;
		}
		res.push_back(s.substr(0, pos));
		s = s.substr(pos+1, s.size() - pos - 1);
	}
	return res;
}


//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
Mesh::Mesh()
	:mLoaded(false)
{
}

//-----------------------------------------------------------------------------
// Destructor
//-----------------------------------------------------------------------------
Mesh::~Mesh()
{
	glDeleteVertexArrays(1, &mVAO);
	glDeleteBuffers(1, &mVBO);
}

//-----------------------------------------------------------------------------
// Loads a Wavefront OBJ model
//
// NOTE: This is not a complete, full featured OBJ loader.  It is greatly
// simplified.
// Assumptions!
//  - OBJ file must contain only triangles
//  - We ignore materials
//  - We ignore normals
//  - only commands "v", "vt" and "f" are supported
//-----------------------------------------------------------------------------
bool Mesh::loadOBJ(const std::string& filename)
{
	std::vector<unsigned int> vertexIndices, uvIndices, normalIndices;
	std::vector<glm::vec3> tempVertices;
	std::vector<glm::vec2> tempUVs;
	std::vector<glm::vec3> tempNormals;


	if (filename.find(".obj") != std::string::npos)
	{
		std::ifstream fin(filename, std::ios::in);
		if (!fin)
		{
			std::cerr << "Cannot open " << filename << std::endl;
			return false;
		}

		std::cout << "Loading OBJ file " << filename << " ..." << std::endl;

		std::string lineBuffer;
		while (std::getline(fin, lineBuffer))
		{
			std::stringstream ss(lineBuffer);
			std::string cmd;
			ss >> cmd;

			if (cmd == "v")
			{
				glm::vec3 vertex;
				int dim = 0;
				while (dim < 3 && ss >> vertex[dim])
					dim++;

				tempVertices.push_back(vertex);
			}
			else if (cmd == "vt")
			{
				glm::vec2 uv;
				int dim = 0;
				while (dim < 2 && ss >> uv[dim])
					dim++;
				
				tempUVs.push_back(uv);
			}
			else if (cmd == "vn")
			{
				glm::vec3 normal;
				int dim = 0;
				while (dim < 3 && ss >> normal[dim])
					dim++;
				normal = glm::normalize(normal);
				tempNormals.push_back(normal);
			}
			else if (cmd == "f")
			{
				std::string faceData;
				int vertexIndex, uvIndex, normalIndex;

				while (ss>>faceData)
				{
					std::vector<std::string> data = split(faceData, "/");

					if (data[0].size() > 0)
					{
						sscanf(data[0].c_str(), "%d", &vertexIndex);
						vertexIndices.push_back(vertexIndex);
					}

					if (data.size() >= 1)
					{
						// Is face format v//vn?  If data[1] is empty string then
						// this vertex has no texture coordinate
						if (data[1].size() > 0)
						{
							sscanf(data[1].c_str(), "%d", &uvIndex);
							uvIndices.push_back(uvIndex);
						}
					}
					
					if (data.size() >= 2)
					{
						// Does this vertex have a normal?
						if (data[2].size() > 0)
						{
							sscanf(data[2].c_str(), "%d", &normalIndex);
							normalIndices.push_back(normalIndex);
						}
					}
				}
			}
		}

		// Close the file
		fin.close();


		// For each vertex of each triangle
		for (unsigned int i = 0; i < vertexIndices.size(); i++)
		{
			Vertex meshVertex;

			// Get the attributes using the indices

			if (tempVertices.size() > 0)
			{
				glm::vec3 vertex = tempVertices[vertexIndices[i] - 1];
				meshVertex.position = vertex;
			}

			if (tempNormals.size() > 0)
			{
				glm::vec3 normal = tempNormals[normalIndices[i] - 1];
				meshVertex.normal = normal;
			}

			if (tempUVs.size() > 0)
			{
				glm::vec2 uv = tempUVs[uvIndices[i] - 1];
				meshVertex.texCoords = uv;
			}

			mVertices.push_back(meshVertex);
		}

		// Create and initialize the buffers
		initBuffers();

		return (mLoaded = true);
	}

	// We shouldn't get here so return failure
	return false;
}

namespace
{
	//-------------------------------------------------------------------------
	// Reads a glTF accessor as floats (numComponents values per element),
	// whatever its component type. Integers are normalized if the accessor
	// says so.
	//-------------------------------------------------------------------------
	bool readAccessor(const GLBFile& glb, int accessorIndex, std::vector<float>& out, int& numComponents)
	{
		const JsonValue& accessor = glb.json["accessors"][accessorIndex];
		if (accessor.type != JsonValue::Object) return false;

		const std::string& type = accessor["type"].str;
		if (type == "SCALAR") numComponents = 1;
		else if (type == "VEC2") numComponents = 2;
		else if (type == "VEC3") numComponents = 3;
		else if (type == "VEC4") numComponents = 4;
		else return false;

		int componentType = accessor["componentType"].asInt();
		size_t componentSize;
		switch (componentType)
		{
		case 5120: case 5121: componentSize = 1; break;	// byte, unsigned byte
		case 5122: case 5123: componentSize = 2; break;	// short, unsigned short
		case 5125: case 5126: componentSize = 4; break;	// unsigned int, float
		default: return false;
		}

		size_t count = (size_t)accessor["count"].asNumber(0);
		bool normalized = accessor["normalized"].boolean;
		out.assign(count * numComponents, 0.0f);

		// An accessor without bufferView is filled with zeros
		if (!accessor.has("bufferView")) return true;

		int viewIndex = accessor["bufferView"].asInt();
		size_t viewLength = 0;
		const unsigned char* data = glb.bufferViewData(viewIndex, &viewLength);
		if (!data) return false;

		size_t elementSize = componentSize * numComponents;
		size_t stride = (size_t)glb.json["bufferViews"][viewIndex]["byteStride"].asNumber(0);
		if (stride == 0) stride = elementSize;
		size_t offset = (size_t)accessor["byteOffset"].asNumber(0);

		if (count > 0 && offset + (count - 1) * stride + elementSize > viewLength) return false;

		for (size_t i = 0; i < count; i++)
		{
			for (int c = 0; c < numComponents; c++)
			{
				const unsigned char* src = data + offset + i * stride + c * componentSize;
				float value = 0.0f;
				switch (componentType)
				{
				case 5120: { int8_t v; memcpy(&v, src, 1); value = normalized ? std::max(v / 127.0f, -1.0f) : v; break; }
				case 5121: { uint8_t v; memcpy(&v, src, 1); value = normalized ? v / 255.0f : v; break; }
				case 5122: { int16_t v; memcpy(&v, src, 2); value = normalized ? std::max(v / 32767.0f, -1.0f) : v; break; }
				case 5123: { uint16_t v; memcpy(&v, src, 2); value = normalized ? v / 65535.0f : v; break; }
				case 5125: { uint32_t v; memcpy(&v, src, 4); value = (float)v; break; }
				case 5126: { memcpy(&value, src, 4); break; }
				}
				out[i * numComponents + c] = value;
			}
		}
		return true;
	}

	//-------------------------------------------------------------------------
	// Reads an index accessor (unsigned byte, short or int)
	//-------------------------------------------------------------------------
	bool readIndices(const GLBFile& glb, int accessorIndex, std::vector<unsigned int>& out)
	{
		const JsonValue& accessor = glb.json["accessors"][accessorIndex];
		int componentType = accessor["componentType"].asInt();
		size_t componentSize;
		switch (componentType)
		{
		case 5121: componentSize = 1; break;
		case 5123: componentSize = 2; break;
		case 5125: componentSize = 4; break;
		default: return false;
		}

		int viewIndex = accessor["bufferView"].asInt();
		size_t viewLength = 0;
		const unsigned char* data = glb.bufferViewData(viewIndex, &viewLength);
		if (!data) return false;

		size_t count = (size_t)accessor["count"].asNumber(0);
		size_t stride = (size_t)glb.json["bufferViews"][viewIndex]["byteStride"].asNumber(0);
		if (stride == 0) stride = componentSize;
		size_t offset = (size_t)accessor["byteOffset"].asNumber(0);

		if (count > 0 && offset + (count - 1) * stride + componentSize > viewLength) return false;

		out.resize(count);
		for (size_t i = 0; i < count; i++)
		{
			const unsigned char* src = data + offset + i * stride;
			if (componentSize == 1) out[i] = *src;
			else if (componentSize == 2) { uint16_t v; memcpy(&v, src, 2); out[i] = v; }
			else { uint32_t v; memcpy(&v, src, 4); out[i] = v; }
		}
		return true;
	}

	//-------------------------------------------------------------------------
	// Local transform of a node: either "matrix" or translation * rotation * scale
	//-------------------------------------------------------------------------
	glm::mat4 nodeLocalMatrix(const JsonValue& node)
	{
		const JsonValue& m = node["matrix"];
		if (m.size() == 16)
		{
			glm::mat4 mat;
			for (int i = 0; i < 16; i++)
				mat[i / 4][i % 4] = (float)m[i].asNumber();	// column-major, like glm
			return mat;
		}

		glm::mat4 T(1.0f), R(1.0f), S(1.0f);
		const JsonValue& t = node["translation"];
		if (t.size() == 3)
			T = glm::translate(glm::mat4(1.0f), glm::vec3(t[0].asNumber(), t[1].asNumber(), t[2].asNumber()));
		const JsonValue& r = node["rotation"];
		if (r.size() == 4)	// quaternion stored as x, y, z, w
			R = glm::mat4_cast(glm::quat((float)r[3].asNumber(), (float)r[0].asNumber(), (float)r[1].asNumber(), (float)r[2].asNumber()));
		const JsonValue& s = node["scale"];
		if (s.size() == 3)
			S = glm::scale(glm::mat4(1.0f), glm::vec3(s[0].asNumber(), s[1].asNumber(), s[2].asNumber()));
		return T * R * S;
	}

	//-------------------------------------------------------------------------
	// Adds the triangles of one primitive, transformed to world space
	//-------------------------------------------------------------------------
	void appendPrimitive(const GLBFile& glb, const JsonValue& primitive, const glm::mat4& world, std::vector<Vertex>& vertices)
	{
		int mode = primitive.has("mode") ? primitive["mode"].asInt() : 4;
		if (mode != 4)
		{
			std::cerr << "GLB: skipping primitive (only triangles are supported)" << std::endl;
			return;
		}

		const JsonValue& attributes = primitive["attributes"];
		std::vector<float> positions, normals, uvs, colors;
		int nPos = 0, nNormal = 0, nUV = 0, nColor = 0;

		if (!attributes.has("POSITION") || !readAccessor(glb, attributes["POSITION"].asInt(), positions, nPos) || nPos != 3)
		{
			std::cerr << "GLB: skipping primitive with invalid positions" << std::endl;
			return;
		}
		bool hasNormals = attributes.has("NORMAL") && readAccessor(glb, attributes["NORMAL"].asInt(), normals, nNormal) && nNormal == 3;
		bool hasUVs = attributes.has("TEXCOORD_0") && readAccessor(glb, attributes["TEXCOORD_0"].asInt(), uvs, nUV) && nUV == 2;
		// Vertex colors are RGB or RGBA (alpha is ignored)
		bool hasColors = attributes.has("COLOR_0") && readAccessor(glb, attributes["COLOR_0"].asInt(), colors, nColor) && nColor >= 3;

		// Without vertex colors, the material's base color factor tints the model
		glm::vec3 baseColor(1.0f);
		const JsonValue& factor = glb.json["materials"][primitive["material"].asInt(0)]["pbrMetallicRoughness"]["baseColorFactor"];
		if (factor.size() >= 3)
			baseColor = glm::vec3(factor[0].asNumber(1.0), factor[1].asNumber(1.0), factor[2].asNumber(1.0));

		size_t vertexCount = positions.size() / 3;
		std::vector<unsigned int> indices;
		if (primitive.has("indices"))
		{
			if (!readIndices(glb, primitive["indices"].asInt(), indices))
			{
				std::cerr << "GLB: skipping primitive with invalid indices" << std::endl;
				return;
			}
		}
		else
		{
			// Non-indexed: vertices are already in triangle order
			indices.resize(vertexCount);
			for (size_t i = 0; i < vertexCount; i++) indices[i] = (unsigned int)i;
		}

		glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
		// A mirroring transform reverses the triangle winding
		bool flipWinding = glm::determinant(glm::mat3(world)) < 0.0f;

		for (size_t i = 0; i + 2 < indices.size(); i += 3)
		{
			Vertex tri[3];
			for (int k = 0; k < 3; k++)
			{
				unsigned int idx = indices[i + k];
				if (idx >= vertexCount) return;

				glm::vec3 p(positions[3 * idx], positions[3 * idx + 1], positions[3 * idx + 2]);
				tri[k].position = glm::vec3(world * glm::vec4(p, 1.0f));

				if (hasNormals)
				{
					glm::vec3 n(normals[3 * idx], normals[3 * idx + 1], normals[3 * idx + 2]);
					tri[k].normal = glm::normalize(normalMatrix * n);
				}

				// glTF puts the UV origin at the top-left, OBJ (and Texture2D, which
				// flips the image) at the bottom-left
				if (hasUVs)
					tri[k].texCoords = glm::vec2(uvs[2 * idx], 1.0f - uvs[2 * idx + 1]);
				else
					tri[k].texCoords = glm::vec2(0.0f);

				if (hasColors)
					tri[k].color = glm::vec3(colors[nColor * idx], colors[nColor * idx + 1], colors[nColor * idx + 2]);
				else
					tri[k].color = baseColor;
			}

			if (flipWinding) std::swap(tri[1], tri[2]);

			if (!hasNormals)
			{
				// No normals in the file: use the face normal
				glm::vec3 n = glm::cross(tri[1].position - tri[0].position, tri[2].position - tri[0].position);
				n = glm::length(n) > 0.0f ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
				tri[0].normal = tri[1].normal = tri[2].normal = n;
			}

			vertices.push_back(tri[0]);
			vertices.push_back(tri[1]);
			vertices.push_back(tri[2]);
		}
	}

	//-------------------------------------------------------------------------
	// Walks the node hierarchy, accumulating the transforms
	//-------------------------------------------------------------------------
	void appendNode(const GLBFile& glb, int nodeIndex, const glm::mat4& parent, std::vector<Vertex>& vertices, int depth)
	{
		const JsonValue& node = glb.json["nodes"][nodeIndex];
		if (node.type != JsonValue::Object || depth > 64) return;

		glm::mat4 world = parent * nodeLocalMatrix(node);

		if (node.has("mesh"))
		{
			const JsonValue& primitives = glb.json["meshes"][node["mesh"].asInt()]["primitives"];
			for (size_t i = 0; i < primitives.size(); i++)
				appendPrimitive(glb, primitives[i], world, vertices);
		}

		const JsonValue& children = node["children"];
		for (size_t i = 0; i < children.size(); i++)
			appendNode(glb, children[i].asInt(), world, vertices, depth + 1);
	}

}

//-----------------------------------------------------------------------------
// Loads the geometry of a binary glTF 2.0 (.glb) model
//
// NOTE: Like loadOBJ, this is a simplified loader. Assumptions!
//  - all the meshes of the default scene are merged into a single mesh, with
//    the node transforms applied
//  - only triangles, POSITION, NORMAL, TEXCOORD_0 and COLOR_0 are used
//  - materials are ignored (see Texture2D::loadGLB for the base color texture)
//  - skinned meshes are drawn in their bind pose, animations are ignored
//-----------------------------------------------------------------------------
bool Mesh::loadGLB(const std::string& filename)
{
	GLBFile glb;
	if (!glb.load(filename))
		return false;

	std::cout << "Loading GLB file " << filename << " ..." << std::endl;

	// Root nodes of the default scene
	std::vector<int> roots;
	const JsonValue& nodes = glb.json["nodes"];
	const JsonValue& scene = glb.json["scenes"][glb.json["scene"].asInt(0)];
	if (scene.has("nodes"))
	{
		for (size_t i = 0; i < scene["nodes"].size(); i++)
			roots.push_back(scene["nodes"][i].asInt());
	}
	else
	{
		// No scene: every node which is not a child of another node is a root
		std::vector<bool> isChild(nodes.size(), false);
		for (size_t i = 0; i < nodes.size(); i++)
			for (size_t c = 0; c < nodes[i]["children"].size(); c++)
			{
				int child = nodes[i]["children"][c].asInt();
				if (child >= 0 && child < (int)nodes.size()) isChild[child] = true;
			}
		for (size_t i = 0; i < nodes.size(); i++)
			if (!isChild[i]) roots.push_back((int)i);
	}

	mVertices.clear();
	for (size_t i = 0; i < roots.size(); i++)
		appendNode(glb, roots[i], glm::mat4(1.0f), mVertices, 0);

	if (mVertices.empty())
	{
		std::cerr << filename << ": no triangles found" << std::endl;
		return false;
	}

	// Create and initialize the buffers
	initBuffers();

	return (mLoaded = true);
}

//-----------------------------------------------------------------------------
// Uses already built triangles (3 vertices per triangle) as the mesh
//-----------------------------------------------------------------------------
bool Mesh::loadVertices(const std::vector<Vertex>& vertices)
{
	if (vertices.empty())
		return false;

	mVertices = vertices;
	initBuffers();
	return (mLoaded = true);
}

//-----------------------------------------------------------------------------
// Create and initialize the vertex buffer and vertex array object
// Must have valid, non-empty std::vector of Vertex objects.
//-----------------------------------------------------------------------------
void Mesh::initBuffers()
{
	glGenVertexArrays(1, &mVAO);
	glGenBuffers(1, &mVBO);

	glBindVertexArray(mVAO);
	glBindBuffer(GL_ARRAY_BUFFER, mVBO);
	glBufferData(GL_ARRAY_BUFFER, mVertices.size() * sizeof(Vertex), &mVertices[0], GL_STATIC_DRAW);

	// Vertex Positions
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid*)0);
	glEnableVertexAttribArray(0);

	// Normals attribute
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid*)(3 * sizeof(GLfloat)));
	glEnableVertexAttribArray(1);

	// Vertex Texture Coords
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid*)(6 * sizeof(GLfloat)));
	glEnableVertexAttribArray(2);

	// Vertex Colors
	glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid*)(8 * sizeof(GLfloat)));
	glEnableVertexAttribArray(3);
	
	// unbind to make sure other code does not change it somewhere else
	glBindVertexArray(0);
}

//-----------------------------------------------------------------------------
// Render the mesh
//-----------------------------------------------------------------------------
void Mesh::draw()
{
	if (!mLoaded) return;

	glBindVertexArray(mVAO);
	glDrawArrays(GL_TRIANGLES, 0, mVertices.size());
	glBindVertexArray(0);
}

