//-----------------------------------------------------------------------------
// Minimal GLB (binary glTF 2.0) file reader
//
// Reads the JSON chunk (with a tiny JSON parser) and the BIN chunk of a .glb
// file. Used by Mesh::loadGLB and Texture2D::loadGLB.
//
// NOTE: This is not a complete glTF implementation. Assumptions!
//  - only the embedded BIN chunk is supported (no external .bin / data: URI)
//  - no sparse accessors, no Draco / meshopt compression
//-----------------------------------------------------------------------------
#ifndef GLB_H
#define GLB_H

#include <vector>
#include <string>
#include <cstddef>

//-----------------------------------------------------------------------------
// A JSON value (null, bool, number, string, array or object)
//-----------------------------------------------------------------------------
struct JsonValue
{
	enum Type { Null, Bool, Number, String, Array, Object };

	Type type = Null;
	bool boolean = false;
	double number = 0.0;
	std::string str;
	std::vector<JsonValue> items;		// array elements, or object values
	std::vector<std::string> keys;		// object keys (same order as items)

	bool has(const std::string& key) const;

	// Return a null value if the key/index does not exist, so accesses can be chained
	const JsonValue& operator[](const std::string& key) const;
	const JsonValue& operator[](size_t index) const;

	size_t size() const { return items.size(); }
	double asNumber(double def = 0.0) const { return type == Number ? number : def; }
	int asInt(int def = -1) const { return type == Number ? (int)number : def; }
};

//-----------------------------------------------------------------------------
// A loaded .glb file
//-----------------------------------------------------------------------------
struct GLBFile
{
	JsonValue json;
	std::vector<unsigned char> bin;

	bool load(const std::string& filename);

	// Bytes of a bufferView (nullptr if invalid). length receives its size.
	const unsigned char* bufferViewData(int viewIndex, size_t* length) const;
};
#endif //GLB_H
