#pragma once
#include "public/rpak.h"

// Re-packable .txtx container written by the district extractor:
//   u32 magic 'TXTX', u32 format, u32 count, u32 dataLen, u8 data[dataLen].
#define TXTX_FILE_MAGIC 0x54585854u // 'TXTX'

// S21-native "txtx" v2 (16 bytes). A small standalone, unreferenced data blob asset
// (observed as either an array of floats or RGBA8 samples). Verified against the
// shipping district paks: two u32 fields, then a relocated pointer to the data blob. The
// on-disk data size is count*4 for the district samples, but the re-packable .txtx
// container carries the blob length explicitly so the writer never has to assume it.
struct TextureExtraAssetHeader_v2_t
{
	uint32_t format; // 0x00 format/flags (copied verbatim; 1 and 0x100000 observed)
	uint32_t count;  // 0x04 element count
	PagePtr_t data;  // 0x08 -> data blob
};

static_assert(sizeof(TextureExtraAssetHeader_v2_t) == 16);
