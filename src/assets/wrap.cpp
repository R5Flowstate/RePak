#include "pch.h"
#include "assets.h"
#include "public/wrap.h"
#include "utils/binaryio.h"

//-----------------------------------------------------------------------------
// purpose: packs an arbitrary raw file (e.g. .bsp / .bsp_lump) as a wrap asset.
// reproduces the engine-loadable inline/permanent uncompressed layout; pak-level
// compression (oodle/zstd) is applied separately to the whole rpak.
//-----------------------------------------------------------------------------
void Assets::AddWrapAsset_v7(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& /*mapEntry*/)
{
	const std::string filePath = pak->GetAssetPath() + assetPath;

	BinaryIO input;
	if (!input.Open(filePath, BinaryIO::Mode_e::Read))
		Error("Failed to open wrap asset \"%s\".\n", filePath.c_str());

	const std::streamoff fileSize = input.GetSize();

	if (fileSize <= 0)
		Error("Wrap asset \"%s\" is empty.\n", filePath.c_str());

	if (fileSize > UINT32_MAX)
		Error("Wrap asset \"%s\" is too large (%lld bytes); wrap sizes are 32-bit.\n", filePath.c_str(), (long long)fileSize);

	// the stored path uses backslashes; the engine resolves the asset by guid,
	// so skipFirstFolderPos/fileNamePos are only used for naming.
	std::string storedPath = assetPath;
	std::replace(storedPath.begin(), storedPath.end(), '/', '\\');

	const uint16_t pathSize = static_cast<uint16_t>(storedPath.size() + 1);

	const size_t firstSep = storedPath.find('\\');
	const uint16_t skipFirstFolderPos = (firstSep == std::string::npos) ? 0 : static_cast<uint16_t>(firstSep + 1);

	const size_t lastSep = storedPath.rfind('\\');
	const uint16_t fileNamePos = (lastSep == std::string::npos) ? 0 : static_cast<uint16_t>(lastSep + 1);

	PakAsset_t& asset = pak->BeginAsset(assetGuid, assetPath);

	PakPageLump_s hdrLump = pak->CreatePageLump(sizeof(WrapAssetHeader_v7_t), SF_HEAD, 8);
	WrapAssetHeader_v7_t* const hdr = reinterpret_cast<WrapAssetHeader_v7_t*>(hdrLump.data);

	// stored path string
	PakPageLump_s pathLump = pak->CreatePageLump(pathSize, SF_CPU, 1);
	memcpy(pathLump.data, storedPath.c_str(), pathSize);
	pak->AddPointer(hdrLump, offsetof(WrapAssetHeader_v7_t, path), pathLump, 0);

	// wrap data alignment (stored in the header's unk4). The S21 wrap loader
	// uses this in page-pointer relocation; a 0 here corrupts the data page
	// index and AVs at pak load. The shipping paks use 64 for BSP lumps
	// and 256 for the 0x69 (lightmap) lump.
	int dataAlign = 64;
	if (strstr(assetPath, ".0069.bsp_lump") != nullptr)
		dataAlign = 256;

	// inline (permanent) uncompressed payload
	PakPageLump_s dataLump = pak->CreatePageLump(static_cast<int>(fileSize), SF_CPU, dataAlign);
	input.Read(dataLump.data, static_cast<size_t>(fileSize));
	input.Close();
	pak->AddPointer(hdrLump, offsetof(WrapAssetHeader_v7_t, data), dataLump, 0);

	hdr->hash = static_cast<uint32_t>(assetGuid);
	hdr->cmpSize = static_cast<uint32_t>(fileSize);
	hdr->dcmpSize = static_cast<uint32_t>(fileSize);
	hdr->pathSize = pathSize;
	hdr->skipFirstFolderPos = skipFirstFolderPos;
	hdr->fileNamePos = fileNamePos;
	hdr->flags = WRAP_FLAG_FILE_IS_PERMANENT;
	hdr->unk4 = static_cast<uint16_t>(dataAlign);
	hdr->unk5[0] = 0xFF;
	hdr->unk5[1] = 0x00;

	asset.InitAsset(hdrLump.GetPointer(), sizeof(WrapAssetHeader_v7_t), dataLump.GetPointer(), WRAP_VERSION, AssetType::WRAP);
	asset.SetHeaderPointer(hdrLump.data);

	pak->FinishAsset();
}
