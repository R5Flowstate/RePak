#include "pch.h"
#include "assets.h"
#include "public/weapon_definition.h"
#include <numeric>

// wepn and impa share one key-value tree layout. Input is the lossless JSON export:
//   { "name": "...", "dependencies": ["0x<guid>", ...],
//     "root": { "name": "...", "values": [ { "key", "type", "raw" | "value" } ],
//               "children": [ <block>, ... ], "valueOrder": [...], "childOrder": [...] } }
// int and float values carry their exact bits in "raw"; strings carry "value".

namespace
{
	// The whole tree is laid out into one cpu lump. Offsets, not pointers, are kept
	// while building because the buffer reallocates as it grows.
	struct WepnLayout_s
	{
		std::vector<char> buf;
		std::vector<std::pair<size_t, size_t>> pointers; // field offset -> target offset

		size_t Alloc(const size_t size, const size_t align)
		{
			const size_t off = IALIGN(buf.size(), align);
			buf.resize(off + size);
			return off;
		}

		size_t AddString(const char* const str)
		{
			const size_t len = strlen(str) + 1;
			const size_t off = Alloc(len, 1);
			memcpy(&buf[off], str, len);
			return off;
		}

		template <typename T>
		T* At(const size_t off) { return reinterpret_cast<T*>(&buf[off]); }

		void Point(const size_t field, const size_t target) { pointers.emplace_back(field, target); }
	};
}

static uint32_t Wepn_KeyHash(const char* const key)
{
	return static_cast<uint32_t>(RTech::StringToGuid(key));
}

// A block's lookup table is its entries sorted by hash. Duplicate keys hash equal and
// the source's tie order decides which one a lookup finds, so a tree exported from a
// pak carries its table; a newly authored tree gets a stable sort.
static size_t Wepn_WriteOrder(WepnLayout_s& out, const std::vector<uint32_t>& hashes,
	const rapidjson::Value& node, const char* const orderKey, const char* const assetPath)
{
	std::vector<uint16_t> order(hashes.size());
	rapidjson::Value::ConstMemberIterator it;

	if (JSON_GetIterator(node, rapidjson::StringRef(orderKey), JSONFieldType_e::kArray, it))
	{
		if (it->value.Size() != hashes.size())
			Error("\"%s\" in \"%s\" has %u entries, expected %zu.\n", orderKey, assetPath, it->value.Size(), hashes.size());

		for (rapidjson::SizeType i = 0; i < it->value.Size(); i++)
		{
			const uint32_t idx = it->value[i].GetUint();

			if (idx >= hashes.size() || (i && hashes[order[i - 1]] > hashes[idx]))
				Error("\"%s\" in \"%s\" is not a hash-sorted permutation.\n", orderKey, assetPath);

			order[i] = static_cast<uint16_t>(idx);
		}
	}
	else
	{
		std::iota(order.begin(), order.end(), static_cast<uint16_t>(0));
		std::stable_sort(order.begin(), order.end(),
			[&hashes](const uint16_t a, const uint16_t b) { return hashes[a] < hashes[b]; });
	}

	const size_t off = out.Alloc(order.size() * sizeof(uint16_t), alignof(uint16_t));
	memcpy(&out.buf[off], order.data(), order.size() * sizeof(uint16_t));
	return off;
}

static void Wepn_WriteBlock(WepnLayout_s& out, const size_t blockOff, const rapidjson::Value& node, const char* const assetPath)
{
	const char* const name = JSON_GetValueRequired<const char*>(node, "name");

	rapidjson::Value::ConstMemberIterator valuesIt, childrenIt;
	const bool hasValues = JSON_GetIterator(node, "values", JSONFieldType_e::kArray, valuesIt);
	const bool hasChildren = JSON_GetIterator(node, "children", JSONFieldType_e::kArray, childrenIt);

	const rapidjson::SizeType numValues = hasValues ? valuesIt->value.Size() : 0;
	const rapidjson::SizeType numChildren = hasChildren ? childrenIt->value.Size() : 0;

	if (numValues > UINT16_MAX || numChildren > UINT16_MAX)
		Error("Block \"%s\" in \"%s\" exceeds %u values or children.\n", name, assetPath, UINT16_MAX);

	const size_t nameOff = out.AddString(name);
	out.Point(blockOff + offsetof(WepnBlock_v1_t, name), nameOff);

	WepnBlock_v1_t* block = out.At<WepnBlock_v1_t>(blockOff);
	block->nameHash = Wepn_KeyHash(name);
	block->numValues = static_cast<uint16_t>(numValues);
	block->numChildren = static_cast<uint16_t>(numChildren);

	if (numValues)
	{
		const size_t valuesOff = out.Alloc(numValues * sizeof(WepnKeyValue_v1_t), 8);
		out.Point(blockOff + offsetof(WepnBlock_v1_t, values), valuesOff);

		std::vector<uint32_t> hashes(numValues);

		for (rapidjson::SizeType i = 0; i < numValues; i++)
		{
			const rapidjson::Value& kv = valuesIt->value[i];
			const char* const key = JSON_GetValueRequired<const char*>(kv, "key");
			const uint32_t type = JSON_GetNumberRequired<uint32_t>(kv, "type");
			const size_t entryOff = valuesOff + i * sizeof(WepnKeyValue_v1_t);

			const size_t keyOff = out.AddString(key);
			out.Point(entryOff + offsetof(WepnKeyValue_v1_t, key), keyOff);

			uint64_t raw = 0;
			size_t strOff = 0;

			switch (static_cast<WepnValueType_e>(type))
			{
			case WepnValueType_e::INTEGER:
			case WepnValueType_e::FLOAT:
				raw = strtoull(JSON_GetValueRequired<const char*>(kv, "raw"), nullptr, 16);
				break;
			case WepnValueType_e::STRING:
				strOff = out.AddString(JSON_GetValueRequired<const char*>(kv, "value"));
				out.Point(entryOff + offsetof(WepnKeyValue_v1_t, value), strOff);
				break;
			default:
				Error("Value \"%s\" in \"%s\" has unknown type %u.\n", key, assetPath, type);
			}

			hashes[i] = Wepn_KeyHash(key);

			WepnKeyValue_v1_t* const entry = out.At<WepnKeyValue_v1_t>(entryOff);
			entry->keyHash = hashes[i];
			entry->valueType = static_cast<WepnValueType_e>(type);

			if (type != static_cast<uint32_t>(WepnValueType_e::STRING))
				entry->value = raw;
		}

		const size_t orderOff = Wepn_WriteOrder(out, hashes, node, "valueOrder", assetPath);
		out.Point(blockOff + offsetof(WepnBlock_v1_t, valueOrder), orderOff);
	}

	if (numChildren)
	{
		const size_t childrenOff = out.Alloc(numChildren * sizeof(WepnBlock_v1_t), 8);
		out.Point(blockOff + offsetof(WepnBlock_v1_t, children), childrenOff);

		std::vector<uint32_t> hashes(numChildren);

		for (rapidjson::SizeType i = 0; i < numChildren; i++)
			hashes[i] = Wepn_KeyHash(JSON_GetValueRequired<const char*>(childrenIt->value[i], "name"));

		const size_t orderOff = Wepn_WriteOrder(out, hashes, node, "childOrder", assetPath);
		out.Point(blockOff + offsetof(WepnBlock_v1_t, childOrder), orderOff);

		for (rapidjson::SizeType i = 0; i < numChildren; i++)
			Wepn_WriteBlock(out, childrenOff + i * sizeof(WepnBlock_v1_t), childrenIt->value[i], assetPath);
	}
}

static void Wepn_AddTreeAsset(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath,
	const uint32_t version, const AssetType type)
{
	const std::string fileName = Utils::ChangeExtension(pak->GetAssetPath() + assetPath, ".json");

	rapidjson::Document document;
	if (!JSON_ParseFromFile(fileName.c_str(), "key-value tree asset", document, true))
		Error("Failed to open key-value tree asset \"%s\".\n", fileName.c_str());

	WepnLayout_s out;

	const size_t nameOff = out.AddString(JSON_GetValueRequired<const char*>(document, "name"));
	rapidjson::Value::ConstMemberIterator rootIt;
	if (!JSON_GetIterator(document, "root", JSONFieldType_e::kObject, rootIt))
		Error("Key-value tree asset \"%s\" has no root block.\n", fileName.c_str());

	const size_t rootOff = out.Alloc(sizeof(WepnBlock_v1_t), 8);
	Wepn_WriteBlock(out, rootOff, rootIt->value, assetPath);

	std::vector<PakGuid_t> dependencies;
	rapidjson::Value::ConstMemberIterator depsIt;

	if (JSON_GetIterator(document, "dependencies", JSONFieldType_e::kArray, depsIt))
	{
		for (const rapidjson::Value& dep : depsIt->value.GetArray())
			dependencies.push_back(Pak_ParseGuid(dep));
	}

	size_t depsOff = 0;

	if (!dependencies.empty())
	{
		depsOff = out.Alloc(dependencies.size() * sizeof(PakGuid_t), 8);
		memcpy(&out.buf[depsOff], dependencies.data(), dependencies.size() * sizeof(PakGuid_t));
	}

	PakAsset_t& asset = pak->BeginAsset(assetGuid, assetPath);

	PakPageLump_s hdrLump = pak->CreatePageLump(sizeof(WepnAssetHeader_v1_t), SF_HEAD, 8);
	PakPageLump_s cpuLump = pak->CreatePageLump(out.buf.size(), SF_CPU, 8);
	memcpy(cpuLump.data, out.buf.data(), out.buf.size());

	for (const auto& [field, target] : out.pointers)
		pak->AddPointer(cpuLump, field, cpuLump, target);

	pak->AddPointer(hdrLump, offsetof(WepnAssetHeader_v1_t, name), cpuLump, nameOff);
	pak->AddPointer(hdrLump, offsetof(WepnAssetHeader_v1_t, root), cpuLump, rootOff);

	if (!dependencies.empty())
	{
		pak->AddPointer(hdrLump, offsetof(WepnAssetHeader_v1_t, dependencies), cpuLump, depsOff);

		for (size_t i = 0; i < dependencies.size(); i++)
			Pak_RegisterGuidRefAtOffset(dependencies[i], depsOff + i * sizeof(PakGuid_t), cpuLump, asset);
	}

	asset.InitAsset(hdrLump.GetPointer(), sizeof(WepnAssetHeader_v1_t), PagePtr_t::NullPtr(), version, type);
	asset.SetHeaderPointer(hdrLump.data);

	pak->FinishAsset();
}

void Assets::AddWeaponDefinitionAsset_v1(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& /*mapEntry*/)
{
	Wepn_AddTreeAsset(pak, assetGuid, assetPath, WEPN_VERSION, AssetType::WEPN);
}

void Assets::AddImpactAsset_v1(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& /*mapEntry*/)
{
	Wepn_AddTreeAsset(pak, assetGuid, assetPath, IMPA_VERSION, AssetType::IMPA);
}
