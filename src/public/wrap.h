#pragma once
#include "public/rpak.h"

// wrap asset flags (see rsx wrap reader)
#define WRAP_FLAG_FILE_IS_COMPRESSED 0x1  // payload is oodle compressed (cmpSize < dcmpSize)
#define WRAP_FLAG_FILE_IS_PERMANENT  0x4  // payload is stored inline (resident), not streamed
#define WRAP_FLAG_FILE_IS_STREAMED   0x10 // payload is stored in a starpak

#pragma pack(push, 1)

// version 7 (apex/s21). raw-file wrapper used to embed arbitrary files
// (notably .bsp and .bsp_lump map data) into an rpak.
struct WrapAssetHeader_v7_t
{
	PagePtr_t path;     // +0x00 -> stored path string
	PagePtr_t data;     // +0x08 -> inline payload (when permanent); unused when streamed

	uint32_t hash;      // +0x10 low 32 bits of the asset guid (StringToGuid of the engine path)
	uint32_t cmpSize;   // +0x14 on-disk payload size (== dcmpSize when uncompressed)
	uint32_t dcmpSize;  // +0x18 decompressed payload size

	uint16_t pathSize;          // +0x1C strlen(path) + 1 (includes null terminator)
	uint16_t skipFirstFolderPos;// +0x1E index after first path separator (engine name = path + this)
	uint16_t fileNamePos;       // +0x20 index after last path separator (offset of basename)

	uint16_t flags;     // +0x22 WRAP_FLAG_*
	uint16_t unk4;      // +0x24 0 when uncompressed
	uint8_t  unk5[2];   // +0x26 always { 0xFF, 0x00 }
};
static_assert(sizeof(WrapAssetHeader_v7_t) == 40);

#pragma pack(pop)
