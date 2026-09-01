#include "pch.h"
#include "assets.h"
#include "public/texture_extra.h"

// page chunk structure and order:
// - header HEAD (align=16)
// - data   CPU  (align=4) the raw blob (floats / RGBA8 samples)
// txtx is a small standalone asset with no GUID dependencies (unreferenced in the
// district base). The re-packable .txtx container carries format/count + the blob.
void Assets::AddTextureExtraAsset_v2(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry)
{
    UNUSED(mapEntry);

    BinaryIO bio;
    const std::string filePath = pak->GetAssetPath() + assetPath;

    if (!bio.Open(filePath, BinaryIO::Mode_e::Read))
        Error("Failed to open txtx asset \"%s\".\n", assetPath);

    const size_t fileSize = bio.GetSize();
    if (fileSize < 16)
        Error("txtx file \"%s\" is too small (%zu bytes).\n", assetPath, fileSize);

    uint32_t fileHead[4];
    bio.Read(reinterpret_cast<uint8_t*>(fileHead), sizeof(fileHead));

    const uint32_t magic = fileHead[0];
    const uint32_t format = fileHead[1];
    const uint32_t count = fileHead[2];
    const uint32_t dataLen = fileHead[3];

    if (magic != TXTX_FILE_MAGIC)
        Error("txtx file \"%s\" has bad magic (expected %x, got %x).\n", assetPath, TXTX_FILE_MAGIC, magic);

    if (16 + static_cast<size_t>(dataLen) > fileSize)
        Error("txtx file \"%s\" is truncated (need %zu, have %zu).\n", assetPath, 16 + static_cast<size_t>(dataLen), fileSize);

    PakAsset_t& asset = pak->BeginAsset(assetGuid, assetPath);

    // txtx lives in the CLIENT segments (SF_HEAD|SF_CLIENT header, SF_CPU|SF_CLIENT data),
    // matching kral; routing it to the non-client segments (flags 0/1) desyncs the slab
    // layout and crashes the S21 client at pak load.
    PakPageLump_s hdrLump = pak->CreatePageLump(sizeof(TextureExtraAssetHeader_v2_t), SF_HEAD | SF_CLIENT, 16);
    TextureExtraAssetHeader_v2_t* const hdr = reinterpret_cast<TextureExtraAssetHeader_v2_t*>(hdrLump.data);
    hdr->format = format;
    hdr->count = count;

    PakPageLump_s dataLump = pak->CreatePageLump(dataLen, SF_CPU | SF_CLIENT, 4);
    bio.Read(reinterpret_cast<uint8_t*>(dataLump.data), dataLen);
    bio.Close();

    pak->AddPointer(hdrLump, offsetof(TextureExtraAssetHeader_v2_t, data), dataLump, 0);

    asset.InitAsset(hdrLump.GetPointer(), sizeof(TextureExtraAssetHeader_v2_t), PagePtr_t::NullPtr(), TXTX_VERSION_V2, AssetType::TXTX);
    asset.SetHeaderPointer(hdrLump.data);

    pak->FinishAsset();
}
