#pragma once
#include <cstdint>

struct ShaderSetAssetHeader_v8_t
{
	uint64_t reserved_vftable;

	PagePtr_t name; // const char*

	uint64_t reserved_inputFlags; // unknown data type, but definitely 8 bytes

	uint16_t textureInputCounts[2];

	uint16_t numSamplers; // number of samplers used by the pixel shader

	uint16_t firstResourceBindPoint;
	uint16_t numResources;

	uint8_t unk_20[32];

	PakGuid_t vertexShader;
	PakGuid_t pixelShader;
};
static_assert(sizeof(ShaderSetAssetHeader_v8_t) == 88);
static_assert(offsetof(ShaderSetAssetHeader_v8_t, vertexShader) == 72);

struct ShaderSetAssetHeader_v11_t
{
	uint64_t reserved_vftable;

	PagePtr_t name; // const char*

	uint64_t reserved_inputFlags; // stores some calculated value of vertex shader input flags

	uint16_t textureInputCounts[2];

	uint16_t numSamplers; // number of samplers used by the pixel shader

	uint8_t firstResourceBindPoint;
	uint8_t numResources;

	uint8_t unk_20[16]; // at least some of this is reserved, potentially all of it

	PakGuid_t vertexShader;
	PakGuid_t pixelShader;
};
static_assert(sizeof(ShaderSetAssetHeader_v11_t) == 64);
static_assert(offsetof(ShaderSetAssetHeader_v11_t, vertexShader) == 48);

// S21 shaderset header (80 bytes). Same field semantics as v11
// (reserved_inputFlags=vtxFmt, textureInputCounts=texSpanPerStage,
// firstResourceBindPoint=firstStructBuf, numResources=numStructBufs) but the
// reserved per-shader input-layout id array grew 16 -> 32, pushing the shader GUID
// refs to +0x40/+0x48. subheaderSize=80; vertexShader GUID @0x40, pixelShader @0x48.
struct ShaderSetAssetHeader_v12_t
{
	uint64_t reserved_vftable;

	PagePtr_t name; // const char*

	uint64_t reserved_inputFlags; // vtxFmt

	uint16_t textureInputCounts[2]; // texSpanPerStage

	uint16_t numSamplers; // number of samplers used by the pixel shader

	uint8_t firstResourceBindPoint; // firstStructBuf
	uint8_t numResources;           // numStructBufs

	uint8_t unk_20[32]; // reserved vsInputLayoutIds[32] + instancingSupport + pad

	PakGuid_t vertexShader;
	PakGuid_t pixelShader;
};
static_assert(sizeof(ShaderSetAssetHeader_v12_t) == 80);
static_assert(offsetof(ShaderSetAssetHeader_v12_t, vertexShader) == 0x40);
static_assert(offsetof(ShaderSetAssetHeader_v12_t, pixelShader) == 0x48);
