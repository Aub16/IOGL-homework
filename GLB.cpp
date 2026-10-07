//-----------------------------------------------------------------------------
// Minimal GLB (binary glTF 2.0) file reader
//-----------------------------------------------------------------------------
#include "GLB.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <cstdint>


//-----------------------------------------------------------------------------
// JsonValue accessors
//-----------------------------------------------------------------------------
static const JsonValue gJsonNull;

bool JsonValue::has(const std::string& key) const
{
	for (size_t i = 0; i < keys.size(); i++)
		if (keys[i] == key) return true;
	return false;
}

const JsonValue& JsonValue::operator[](const std::string& key) const
{
	if (type != Object) return gJsonNull;
	for (size_t i = 0; i < keys.size(); i++)
		if (keys[i] == key) return items[i];
	return gJsonNull;
}

const JsonValue& JsonValue::operator[](size_t index) const
{
	if (type != Array || index >= items.size()) return gJsonNull;
	return items[index];
}


//-----------------------------------------------------------------------------
// Tiny recursive descent JSON parser
//-----------------------------------------------------------------------------
namespace
{
	struct JsonParser
	{
		const char* p;
		const char* end;

		void skipSpaces()
		{
			while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
				p++;
		}

		bool parseString(std::string& out)
		{
			if (p >= end || *p != '"') return false;
			p++;
			while (p < end && *p != '"')
			{
				char c = *p++;
				if (c == '\\' && p < end)
				{
					char e = *p++;
					switch (e)
					{
					case 'n': out += '\n'; break;
					case 't': out += '\t'; break;
					case 'r': out += '\r'; break;
					case 'b': out += '\b'; break;
					case 'f': out += '\f'; break;
					case 'u':
					{
						// Encode the code point in UTF-8 (surrogate pairs not handled)
						if (end - p < 4) return false;
						unsigned int cp = (unsigned int)strtoul(std::string(p, 4).c_str(), nullptr, 16);
						p += 4;
						if (cp < 0x80) out += (char)cp;
						else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
						else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
						break;
					}
					default: out += e; break;	// \" \\ \/
					}
				}
				else
					out += c;
			}
			if (p >= end) return false;
			p++;	// closing quote
			return true;
		}

		bool parseValue(JsonValue& v)
		{
			skipSpaces();
			if (p >= end) return false;

			if (*p == '{')
			{
				v.type = JsonValue::Object;
				p++;
				skipSpaces();
				if (p < end && *p == '}') { p++; return true; }
				while (true)
				{
					skipSpaces();
					std::string key;
					if (!parseString(key)) return false;
					skipSpaces();
					if (p >= end || *p != ':') return false;
					p++;
					v.keys.push_back(key);
					v.items.push_back(JsonValue());
					if (!parseValue(v.items.back())) return false;
					skipSpaces();
					if (p < end && *p == ',') { p++; continue; }
					if (p < end && *p == '}') { p++; return true; }
					return false;
				}
			}
			else if (*p == '[')
			{
				v.type = JsonValue::Array;
				p++;
				skipSpaces();
				if (p < end && *p == ']') { p++; return true; }
				while (true)
				{
					v.items.push_back(JsonValue());
					if (!parseValue(v.items.back())) return false;
					skipSpaces();
					if (p < end && *p == ',') { p++; continue; }
					if (p < end && *p == ']') { p++; return true; }
					return false;
				}
			}
			else if (*p == '"')
			{
				v.type = JsonValue::String;
				return parseString(v.str);
			}
			else if (end - p >= 4 && strncmp(p, "true", 4) == 0)
			{
				v.type = JsonValue::Bool;
				v.boolean = true;
				p += 4;
				return true;
			}
			else if (end - p >= 5 && strncmp(p, "false", 5) == 0)
			{
				v.type = JsonValue::Bool;
				p += 5;
				return true;
			}
			else if (end - p >= 4 && strncmp(p, "null", 4) == 0)
			{
				p += 4;
				return true;
			}
			else
			{
				// Number (the chunk is not null-terminated so copy it first)
				const char* start = p;
				while (p < end && (strchr("+-.eE", *p) || (*p >= '0' && *p <= '9')))
					p++;
				if (p == start) return false;
				v.type = JsonValue::Number;
				v.number = strtod(std::string(start, p).c_str(), nullptr);
				return true;
			}
		}
	};

	uint32_t readU32(const unsigned char* data)
	{
		// GLB is little-endian
		return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
	}
}


//-----------------------------------------------------------------------------
// Loads a .glb file: 12 byte header, then a JSON chunk and an optional BIN chunk
// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#glb-file-format-specification
//-----------------------------------------------------------------------------
bool GLBFile::load(const std::string& filename)
{
	std::ifstream fin(filename, std::ios::in | std::ios::binary);
	if (!fin)
	{
		std::cerr << "Cannot open " << filename << std::endl;
		return false;
	}

	std::vector<unsigned char> data((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
	fin.close();

	const uint32_t GLB_MAGIC = 0x46546C67;	// "glTF"
	const uint32_t CHUNK_JSON = 0x4E4F534A;	// "JSON"
	const uint32_t CHUNK_BIN = 0x004E4942;	// "BIN\0"

	if (data.size() < 12 || readU32(&data[0]) != GLB_MAGIC)
	{
		std::cerr << filename << " is not a GLB file" << std::endl;
		return false;
	}
	if (readU32(&data[4]) != 2)
	{
		std::cerr << filename << ": only glTF 2.0 is supported" << std::endl;
		return false;
	}

	bool hasJson = false;
	size_t offset = 12;
	while (offset + 8 <= data.size())
	{
		uint32_t chunkLength = readU32(&data[offset]);
		uint32_t chunkType = readU32(&data[offset + 4]);
		offset += 8;
		if (offset + chunkLength > data.size())
		{
			std::cerr << filename << ": truncated chunk" << std::endl;
			return false;
		}

		if (chunkType == CHUNK_JSON)
		{
			JsonParser parser = { (const char*)&data[offset], (const char*)&data[offset] + chunkLength };
			if (!parser.parseValue(json) || json.type != JsonValue::Object)
			{
				std::cerr << filename << ": invalid JSON chunk" << std::endl;
				return false;
			}
			hasJson = true;
		}
		else if (chunkType == CHUNK_BIN)
		{
			bin.assign(data.begin() + offset, data.begin() + offset + chunkLength);
		}
		// Unknown chunks must be ignored

		offset += chunkLength;
	}

	if (!hasJson)
	{
		std::cerr << filename << ": missing JSON chunk" << std::endl;
		return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Returns the bytes of a bufferView. Only buffer 0 stored in the BIN chunk
// is supported.
//-----------------------------------------------------------------------------
const unsigned char* GLBFile::bufferViewData(int viewIndex, size_t* length) const
{
	const JsonValue& view = json["bufferViews"][viewIndex];
	if (view.type != JsonValue::Object) return nullptr;

	if (view["buffer"].asInt(0) != 0 || json["buffers"][(size_t)0].has("uri"))
	{
		std::cerr << "GLB: external buffers are not supported" << std::endl;
		return nullptr;
	}

	size_t byteOffset = (size_t)view["byteOffset"].asNumber(0);
	size_t byteLength = (size_t)view["byteLength"].asNumber(0);
	if (byteOffset + byteLength > bin.size()) return nullptr;

	if (length) *length = byteLength;
	return bin.data() + byteOffset;
}
