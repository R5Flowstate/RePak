#pragma once
#include "public/rpak.h"

// efct v16: 24-byte header (two GUID arrays) + cpu ParticleDefinition blob.
#pragma pack(push, 1)
struct EffectAssetHeader_v16_t
{
	PagePtr_t childRefs;
	PagePtr_t assetRefs;
	uint32_t childRefCount;
	uint32_t assetRefCount;
};
#pragma pack(pop)
static_assert(sizeof(EffectAssetHeader_v16_t) == 24, "EffectAssetHeader_v16_t must be 24 bytes");

// Leading fields of the cpu blob (ParticleDefinition_DiskTemp). The parms block and the six
// operator lists follow; only this prefix is fixed enough for the writer to sanity-check against.
#pragma pack(push, 1)
struct EffectDefinition_v16_t
{
	PagePtr_t name;
	uint64_t opsCheckSum;
	PagePtr_t childRefs;
	PagePtr_t scriptRefs;
	uint32_t childRefCount;
	uint32_t scriptRefCount;
};
#pragma pack(pop)
static_assert(sizeof(EffectDefinition_v16_t) == 0x28, "EffectDefinition_v16_t must be 0x28 bytes");

#define EFCT_DEF_MAGIC   0x54434645  // 'EFCT'
#define EFCT_DEF_VERSION 1
