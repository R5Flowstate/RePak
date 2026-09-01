#pragma once
#include "public/rpak.h"

struct AnimRigAssetHeader_t
{
	PagePtr_t data;
	PagePtr_t name;
	char gap_10[4];
	uint32_t sequenceCount;
	PagePtr_t pSequences;
	char gap_20[8];
};

static_assert(sizeof(AnimRigAssetHeader_t) == 40);

// S21-native arig v6 (40B). Unlike v4 (sequenceCount is a u32 @0x14), v6 stores the
// count as a u16 @0x12 with a u16 externalSequenceCount @0x10 and a 4-byte pad @0x14
// (matches RSX AnimRigAssetHeader_v5_t, verified against the shipping district paks). The blob
// is a v16-era studio skeleton (.rrig) without an IDST magic at offset 0.
struct AnimRigAssetHeader_v6_t
{
	PagePtr_t data;              // 0x00 studio (.rrig) blob
	PagePtr_t name;              // 0x08
	uint16_t externalSeqCount;   // 0x10
	uint16_t sequenceCount;      // 0x12
	uint32_t pad_14;             // 0x14
	PagePtr_t pSequences;        // 0x18 sequence guid array
	PagePtr_t pExternalSeqNames; // 0x20 (null when externalSeqCount == 0)
};

static_assert(sizeof(AnimRigAssetHeader_v6_t) == 40);