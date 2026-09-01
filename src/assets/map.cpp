#include "pch.h"
#include "assets.h"
#include "public/map.h"

// S21-native rmap v4. For a ported district the map asset is an all-null 104-byte stub
// (no rawData, no pointers, no GUID dependencies) -- the real geometry/props/terrain are
// delivered by the BSP wrap (perm/temp) paks. Reproduced byte-exact as a zeroed header.
void Assets::AddMapAsset_v4(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry)
{
    UNUSED(mapEntry);

    PakAsset_t& asset = pak->BeginAsset(assetGuid, assetPath);

    // rmap v4 subheader uses 8-byte alignment: kral places it at the next 8-aligned offset,
    // not 16. Align 16 left an 8-byte gap that drifted seg0 page#11 +16B vs kral and broke 1:1.
    PakPageLump_s hdrLump = pak->CreatePageLump(sizeof(MapAssetHeader_v4_t), SF_HEAD, 8);
    memset(hdrLump.data, 0, sizeof(MapAssetHeader_v4_t));

    asset.InitAsset(hdrLump.GetPointer(), sizeof(MapAssetHeader_v4_t), PagePtr_t::NullPtr(), RMAP_VERSION_V4, AssetType::RMAP);
    asset.SetHeaderPointer(hdrLump.data);

    pak->FinishAsset();
}
