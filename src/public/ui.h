#pragma once

enum class VariableType : uint8_t {
	NONE = 0x0,
	STRING = 0x1,
	ASSET = 0x2,
	BOOL = 0x3,
	INT = 0x4,
	FLOAT = 0x5,
	FLOAT2 = 0x6,
	FLOAT3 = 0x7,
	COLOR_ALPHA = 0x8,
	GAMETIME = 0x9,
	FLOAT_UNK = 0xA,
	IMAGE = 0xB
};

struct Argument_s
{
	VariableType type;
	uint8_t unk_1;
	uint16_t dataOffset;
	uint16_t nameOffset;
	uint16_t shortHash;
};

struct ArgCluster_s
{
	uint16_t argIndex;
	uint16_t argCount;
	uint8_t byte_4;
	uint8_t byte_5;
	uint16_t short_6;
	uint16_t valueSize;
	uint16_t dataStructSize;
	uint16_t short_C;
	uint16_t short_E;
	uint16_t renderJobCount;
};

// V30 style descriptor - 52 bytes
// Contains base style header (44 bytes) + partial font fields (8 bytes)
struct StyleDescriptor_v30_s {
	// Base style header (44 bytes)
	uint16_t type;              // +0x00: widget type
	uint16_t color[3][4];       // +0x02: color indices [3 colors][RGBA] into data buffer
	uint16_t tint[4];           // +0x1A: tint indices [RGBA]
	uint16_t blend;             // +0x22: blend index
	uint16_t premul;            // +0x24: premultiply index
	uint16_t hue;               // +0x26: hue index
	uint16_t saturation;        // +0x28: saturation index
	uint16_t lightness;         // +0x2A: lightness index

	// Font-specific fields (partial, 8 bytes)
	uint16_t fontHash;          // +0x2C: font hash index
	uint16_t shadowAlpha;       // +0x2E: shadow alpha index
	uint16_t shadowOffset[2];   // +0x30: shadow offset indices [X,Y]
};
static_assert(sizeof(StyleDescriptor_v30_s) == 52, "StyleDescriptor_v30_s must be 52 bytes");

// V39+ style descriptor - 68 bytes
// Contains full base style header (44 bytes) + complete font fields (24 bytes)
struct StyleDescriptor_v39_s {
	// Base style header (44 bytes)
	uint16_t type;              // +0x00: widget type
	uint16_t color[3][4];       // +0x02: color indices [3 colors][RGBA] into data buffer
	uint16_t tint[4];           // +0x1A: tint indices [RGBA]
	uint16_t blend;             // +0x22: blend index
	uint16_t premul;            // +0x24: premultiply index
	uint16_t hue;               // +0x26: hue index
	uint16_t saturation;        // +0x28: saturation index
	uint16_t lightness;         // +0x2A: lightness index

	// Font-specific fields (24 bytes)
	uint16_t fontHash;          // +0x2C: font hash index
	uint16_t shadowAlpha;       // +0x2E: shadow alpha index
	uint16_t shadowOffset[2];   // +0x30: shadow offset indices [X,Y]
	uint16_t shadowBlur;        // +0x34: shadow blur index
	uint16_t pixelHeight;       // +0x36: pixel height index
	uint16_t pixelAspect;       // +0x38: pixel aspect ratio index
	uint16_t outlineWidth;      // +0x3A: outline width index
	uint16_t thicken;           // +0x3C: thicken index
	uint16_t blur;              // +0x3E: blur index
	uint16_t baselineShift;     // +0x40: baseline shift index
	uint16_t kerning;           // +0x42: kerning index
};
static_assert(sizeof(StyleDescriptor_v39_s) == 68, "StyleDescriptor_v39_s must be 68 bytes");

// Keyframing mapping entry - maps values through linear/cubic spline regression
struct UIAssetMapping_t
{
	uint32_t dataCount;
	uint16_t unk_4;
	uint16_t unk_6;
	float* data;
};

struct RuiHeader_v30_s
{
	const char* name;
	uint8_t* dataStructInitData;
	uint8_t* transformData;
	float elementWidth;
	float elementHeight;
	float elementWidthRcp;
	float elementHeightRcp;
	char* argNames;
	ArgCluster_s* argClusters;
	Argument_s* arguments;
	short argumentCount;
	short keyframingCount;
	uint16_t dataStructSize;
	uint16_t dataStructInitSize;
	uint16_t styleDescriptorCount;
	uint16_t maxTransformIndex;
	uint16_t renderJobCount;
	uint16_t argClusterCount;
	StyleDescriptor_v30_s* styleDescriptors;
	uint8_t* renderJobData;
	void* keyframings;
	uint64_t codeCRC;
};
static_assert(sizeof(RuiHeader_v30_s) == 112, "S21 UI disk header is 112 bytes");
