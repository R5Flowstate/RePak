#include "pch.h"
#include "assets.h"
#include "public/effect.h"

// Read a raw array of 8-byte PakGuids dumped by the efct converter
// (.efct_childrefs / .efct_assetrefs). Returns true if the sidecar exists.
// Missing child sidecar is legal only when blob+0x20 == 0.
static bool Effect_ReadGuidArray(const std::string& path, std::vector<PakGuid_t>& out, const char* const assetPath, const char* const sidecar)
{
	out.clear();
	BinaryIO in;
	if (!in.Open(path, BinaryIO::Mode_e::Read))
		return false;

	const size_t fileSize = static_cast<size_t>(in.GetSize());
	if ((fileSize % sizeof(PakGuid_t)) != 0)
		Error("efct sidecar \"%s%s\" size %zu is not a multiple of 8.\n", assetPath, sidecar, fileSize);

	const size_t count = fileSize / sizeof(PakGuid_t);
	out.resize(count);
	if (count > 0)
		in.Read(reinterpret_cast<uint8_t*>(out.data()), count * sizeof(PakGuid_t));

	in.Close();
	return true;
}

// efct v16: 24-byte GUID-array header + cpu blob. Pointers in the blob are blob-relative.
void Assets::AddEffectAsset_v16(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry)
{
	UNUSED(mapEntry);

	const std::string basePath = pak->GetAssetPath() + assetPath;

	BinaryIO def;
	if (!def.Open(basePath + ".efct_def", BinaryIO::Mode_e::Read))
		Error("Failed to open efct definition \"%s.efct_def\".\n", assetPath);

	uint32_t head[4];
	def.Read(reinterpret_cast<uint8_t*>(head), sizeof(head));

	if (head[0] != EFCT_DEF_MAGIC)
		Error("efct definition \"%s\" has bad magic (expected %x, got %x).\n", assetPath, EFCT_DEF_MAGIC, head[0]);
	if (head[1] != EFCT_DEF_VERSION)
		Error("efct definition \"%s\" is version %u, expected %u.\n", assetPath, head[1], EFCT_DEF_VERSION);

	const uint32_t blobSize = head[2];
	const uint32_t pointerCount = head[3];
	if (blobSize < sizeof(EffectDefinition_v16_t))
		Error("efct definition \"%s\" is too small (%u bytes).\n", assetPath, blobSize);

	std::vector<PakGuid_t> childGuids;
	std::vector<PakGuid_t> assetGuids;
	const bool haveChildSidecar = Effect_ReadGuidArray(basePath + ".efct_childrefs", childGuids, assetPath, ".efct_childrefs");
	Effect_ReadGuidArray(basePath + ".efct_assetrefs", assetGuids, assetPath, ".efct_assetrefs");

	PakAsset_t& asset = pak->BeginAsset(assetGuid, assetPath);

	PakPageLump_s hdrLump = pak->CreatePageLump(sizeof(EffectAssetHeader_v16_t), SF_HEAD | SF_CLIENT, 8);
	EffectAssetHeader_v16_t* const hdr = reinterpret_cast<EffectAssetHeader_v16_t*>(hdrLump.data);
	memset(hdr, 0, sizeof(EffectAssetHeader_v16_t));
	hdr->childRefCount = static_cast<uint32_t>(childGuids.size());
	hdr->assetRefCount = static_cast<uint32_t>(assetGuids.size());

	PakPageLump_s defLump = pak->CreatePageLump(static_cast<int>(blobSize), SF_CPU | SF_TEMP | SF_CLIENT, 16);
	def.Read(reinterpret_cast<uint8_t*>(defLump.data), blobSize);

	const uint32_t blobChildCount = reinterpret_cast<const EffectDefinition_v16_t*>(defLump.data)->childRefCount;
	if (!haveChildSidecar && blobChildCount != 0)
		Error("efct \"%s\" blob child count is %u but .efct_childrefs is missing.\n", assetPath, blobChildCount);
	if (haveChildSidecar && childGuids.size() != blobChildCount)
	{
		if (blobChildCount == 0)
			Error("efct \"%s\" has leftover .efct_childrefs (%zu) but blob+0x20 is 0.\n", assetPath, childGuids.size());
		else
			Error("efct \"%s\" child sidecar count %zu != blob+0x20 count %u.\n", assetPath, childGuids.size(), blobChildCount);
	}

	for (uint32_t i = 0; i < pointerCount; i++)
	{
		uint32_t link[2];
		def.Read(reinterpret_cast<uint8_t*>(link), sizeof(link));

		if (link[0] + sizeof(PagePtr_t) > blobSize || link[1] >= blobSize)
			Error("efct definition \"%s\" has an out-of-range pointer (%u -> %u, blob %u).\n", assetPath, link[0], link[1], blobSize);

		pak->AddPointer(defLump, link[0], defLump, link[1]);
	}
	def.Close();

	const size_t totalGuids = childGuids.size() + assetGuids.size();
	if (totalGuids > 0)
	{
		asset.ExpandGuidBuf(totalGuids);

		// Both arrays are packed contiguously as [child GUIDs][asset GUIDs]; childRefs/assetRefs
		// point at their own base.
		PakPageLump_s refsLump = pak->CreatePageLump(static_cast<int>(totalGuids * sizeof(PakGuid_t)), SF_CPU | SF_CLIENT, 8);
		PakGuid_t* const refs = reinterpret_cast<PakGuid_t*>(refsLump.data);

		size_t idx = 0;
		for (const PakGuid_t g : childGuids)
		{
			refs[idx] = g;
			Pak_RegisterGuidRefAtOffset(g, idx * sizeof(PakGuid_t), refsLump, asset);
			idx++;
		}
		const size_t assetBase = childGuids.size() * sizeof(PakGuid_t);
		for (const PakGuid_t g : assetGuids)
		{
			refs[idx] = g;
			Pak_RegisterGuidRefAtOffset(g, idx * sizeof(PakGuid_t), refsLump, asset);
			idx++;
		}

		if (!childGuids.empty())
			pak->AddPointer(hdrLump, offsetof(EffectAssetHeader_v16_t, childRefs), refsLump, 0);
		if (!assetGuids.empty())
			pak->AddPointer(hdrLump, offsetof(EffectAssetHeader_v16_t, assetRefs), refsLump, assetBase);
	}

	asset.InitAsset(hdrLump.GetPointer(), sizeof(EffectAssetHeader_v16_t), defLump.GetPointer(), EFCT_VERSION_V16, AssetType::EFCT);
	asset.SetHeaderPointer(hdrLump.data);

	pak->FinishAsset();
}
