#include "pch.h"
#include "assets.h"
#include "public/rson.h"

static std::vector<uint8_t> RSON_DecodeHexString(const char* hexStr, size_t expectedSize)
{
	std::vector<uint8_t> result;
	result.reserve(expectedSize);

	for (size_t i = 0; hexStr[i] && hexStr[i + 1]; i += 2)
	{
		char byte[3] = { hexStr[i], hexStr[i + 1], 0 };
		result.push_back(static_cast<uint8_t>(strtoul(byte, nullptr, 16)));
	}

	return result;
}

void Assets::AddRSONAsset_v1(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& /*mapEntry*/)
{
	const std::string fileName = pak->GetAssetPath() + assetPath;

	rapidjson::Document document;
	if (!JSON_ParseFromFile(fileName.c_str(), "rson asset", document, true))
		Error("Failed to open rson asset \"%s\".\n", fileName.c_str());

	const size_t headerDataSize = JSON_GetNumberRequired<size_t>(document, "headerDataSize");
	const char* const headerDataHex = JSON_GetValueRequired<const char*>(document, "headerDataHex");

	std::vector<uint8_t> headerData = RSON_DecodeHexString(headerDataHex, headerDataSize);

	if (headerData.size() != headerDataSize)
		Error("RSON header data size mismatch: expected %zu, got %zu.\n", headerDataSize, headerData.size());

	if (headerDataSize != sizeof(RSONAssetHeader_v1_t))
		Error("RSON header size %zu does not match expected %zu.\n", headerDataSize, sizeof(RSONAssetHeader_v1_t));

	const size_t cpuDataSize = JSON_GetNumberRequired<size_t>(document, "cpuDataSize");
	const char* const cpuDataHex = JSON_GetValueRequired<const char*>(document, "cpuDataHex");

	std::vector<uint8_t> cpuData = RSON_DecodeHexString(cpuDataHex, cpuDataSize);

	if (cpuData.size() != cpuDataSize)
		Error("RSON cpu data size mismatch: expected %zu, got %zu.\n", cpuDataSize, cpuData.size());

	PakAsset_t& asset = pak->BeginAsset(assetGuid, assetPath);

	PakPageLump_s hdrLump = pak->CreatePageLump(sizeof(RSONAssetHeader_v1_t), SF_HEAD, 8);
	memcpy(hdrLump.data, headerData.data(), sizeof(RSONAssetHeader_v1_t));

	PakPageLump_s cpuLump = pak->CreatePageLump(cpuDataSize, SF_CPU, 8);
	memcpy(cpuLump.data, cpuData.data(), cpuDataSize);

	rapidjson::Value::ConstMemberIterator ptrFixupsIt;
	if (JSON_GetIterator(document, "pointerFixups", JSONFieldType_e::kArray, ptrFixupsIt))
	{
		const rapidjson::Value::ConstArray& ptrFixupsArray = ptrFixupsIt->value.GetArray();

		for (rapidjson::SizeType i = 0; i < ptrFixupsArray.Size(); i++)
		{
			const rapidjson::Value& fixup = ptrFixupsArray[i];

			const char* srcSection = JSON_GetValueRequired<const char*>(fixup, "srcSection");
			const size_t srcOffset = JSON_GetNumberRequired<size_t>(fixup, "srcOffset");
			const char* dstSection = JSON_GetValueRequired<const char*>(fixup, "dstSection");
			const size_t dstOffset = JSON_GetNumberRequired<size_t>(fixup, "dstOffset");

			PakPageLump_s* srcLump = nullptr;
			PakPageLump_s* dstLump = nullptr;

			if (strcmp(srcSection, "head") == 0)
				srcLump = &hdrLump;
			else if (strcmp(srcSection, "cpu") == 0)
				srcLump = &cpuLump;

			if (strcmp(dstSection, "head") == 0)
				dstLump = &hdrLump;
			else if (strcmp(dstSection, "cpu") == 0)
				dstLump = &cpuLump;

			if (srcLump && dstLump)
				pak->AddPointer(*srcLump, srcOffset, *dstLump, dstOffset);
		}
	}

	asset.InitAsset(hdrLump.GetPointer(), sizeof(RSONAssetHeader_v1_t), PagePtr_t::NullPtr(), RSON_VERSION, AssetType::RSON);
	asset.SetHeaderPointer(hdrLump.data);

	pak->FinishAsset();
}
