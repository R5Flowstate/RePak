#pragma once
#include "rpak.h"

struct RSONAssetHeader_v1_t
{
	int type;
	int valueCount;
	PagePtr_t values;
};
static_assert(sizeof(RSONAssetHeader_v1_t) == 16, "RSONAssetHeader_v1_t must be 16 bytes");
