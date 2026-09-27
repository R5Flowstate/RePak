#pragma once

// Shared by wepn (weapon definitions) and impa (impact tables): a key-value tree.

enum class WepnValueType_e : uint32_t
{
	INTEGER = 0,
	FLOAT = 1,
	STRING = 2,
};

#pragma pack(push, 1)
struct WepnKeyValue_v1_t
{
	PagePtr_t key;
	uint32_t keyHash; // low 32 bits of StringToGuid(key)
	WepnValueType_e valueType;
	uint64_t value; // string pointer, or the int/float bits
};
static_assert(sizeof(WepnKeyValue_v1_t) == 24);

struct WepnBlock_v1_t
{
	PagePtr_t name;
	uint32_t nameHash;
	uint16_t numValues;
	uint16_t numChildren;
	PagePtr_t values;
	PagePtr_t children;

	// indices into values/children sorted by hash, for binary search by key
	PagePtr_t valueOrder;
	PagePtr_t childOrder;
};
static_assert(sizeof(WepnBlock_v1_t) == 48);

struct WepnAssetHeader_v1_t
{
	PagePtr_t name;
	PagePtr_t root;
	PagePtr_t reserved;

	// every asset guid the tree's strings name, one per guid ref
	PagePtr_t dependencies;
};
static_assert(sizeof(WepnAssetHeader_v1_t) == 32);
#pragma pack(pop)
