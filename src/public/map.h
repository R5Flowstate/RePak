#pragma once
#include "public/rpak.h"

// S21-native rmap v4 (104 bytes = 13 8-byte pointer slots). For a ported / disk-loaded
// district map this is an all-null STUB: the real geometry/props/terrain live in the BSP
// wrap (perm/temp) paks, not in the base rmap. Slots are name + index/vertex
// buffers + geos + static props + spawn points + collision geos + terrain
// heightmaps + terrain virtual texture; all zero on disk for a stub map.
struct MapAssetHeader_v4_t
{
	PagePtr_t name;               // 0x00
	PagePtr_t pIndexBuffer;       // 0x08
	PagePtr_t pVertexBuffers;     // 0x10
	PagePtr_t pGeos;              // 0x18
	PagePtr_t pStaticProps;       // 0x20
	PagePtr_t pSpawnPoints;       // 0x28
	PagePtr_t pCollisionGeos;     // 0x30
	PagePtr_t pTerrainHeightmaps; // 0x38
	PagePtr_t pTerrainVirtualTex; // 0x40
	PagePtr_t unk_48;             // 0x48
	PagePtr_t unk_50;             // 0x50
	PagePtr_t unk_58;             // 0x58
	PagePtr_t unk_60;             // 0x60
};

static_assert(sizeof(MapAssetHeader_v4_t) == 104);
