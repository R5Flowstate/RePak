#include "pch.h"
#include "assets.h"
#include "utils/dxutils.h"
#include "public/texture.h"

#define TEXTURE_RESOURCE_FLAGS_FIELD "resourceFlags"
#define TEXTURE_USAGE_FLAGS_FIELD "usageFlags"
#define TEXTURE_MIP_INFO_FIELD "mipInfo"
#define TEXTURE_STREAM_LAYOUT_FIELD "streamLayout"

static void Texture_ValidateMetadataArray(const rapidjson::Value& arrayValue, const int totalMipCount, const char* const fieldName)
{
    if (!JSON_IsOfType(arrayValue, JSONFieldType_e::kArray))
        Error("Field \"%s\" in texture metadata must be an array.\n", fieldName);

    const rapidjson::Value::ConstArray& arrayData = arrayValue.GetArray();

    if (arrayData.Empty())
        Error("Array \"%s\" was found empty in texture metadata.\n", fieldName);

    const size_t arraySize = arrayData.Size();

    if (arraySize != totalMipCount - 1)
        Error("Array \"%s\" in texture metadata must cover all mips except the base (expected %i, got %i).\n", fieldName,
            totalMipCount - 1, static_cast<int>(arraySize));
}

static mipType_e Texture_GetMipTypeFromName(const char* const typeName)
{
    if (strcmp(typeName, "permanent") == 0)
        return mipType_e::STATIC;
    if (strcmp(typeName, "mandatory") == 0)
        return mipType_e::STREAMED;
    if (strcmp(typeName, "optional") == 0)
        return mipType_e::STREAMED_OPT;

    return mipType_e::INVALID;
}

// If the texture has additional metadata, parse it.
static void Texture_ProcessMetaData(CPakFileBuilder* const pak, const char* const assetPath, 
                                    TextureAssetHeader_t* const hdr, const int totalMipCount, std::vector<mipType_e>& streamLayout)
{
    const std::string metaFilePath = Utils::ChangeExtension(pak->GetAssetPath() + assetPath, ".json");
    rapidjson::Document document;

    if (!JSON_ParseFromFile(metaFilePath.c_str(), "texture metadata", document, false))
        return;

    rapidjson::Value::ConstMemberIterator streamLayoutIt;

    if (JSON_GetIterator(document, TEXTURE_STREAM_LAYOUT_FIELD, streamLayoutIt))
    {
        Texture_ValidateMetadataArray(streamLayoutIt->value, totalMipCount, TEXTURE_STREAM_LAYOUT_FIELD);
        const rapidjson::Value::ConstArray& streamLayoutArray = streamLayoutIt->value.GetArray();

        // -1 because the first mip isn't counted, it is always permanent.
        streamLayout.resize(totalMipCount-1);

        // note: unclamped loop and write into dynamic sized array because we
        // have already confirmed that the texture mip count is sane.
        uint32_t index = 0;

        for (const js::Value& streamLayoutEntry : streamLayoutArray)
        {
            if (!streamLayoutEntry.IsString())
                Error("Expected type %s for \"" TEXTURE_MIP_INFO_FIELD "\" #%u, got %s.\n",
                    JSON_TypeToString(JSONFieldType_e::kString), index, JSON_TypeToString(streamLayoutEntry));

            const char* const mipTypeName = streamLayoutEntry.GetString();
            const mipType_e mipType = Texture_GetMipTypeFromName(mipTypeName);

            if (mipType == mipType_e::INVALID)
                Error("Invalid texture mip type \"%s\" in \"" TEXTURE_MIP_INFO_FIELD "\" #%u; expected one of the following: permanent:mandatory:optional\n.",
                    mipTypeName, index);

            // The lookup in Texture_InternalAddTexture happens in reverse,
            // write it out in reverse here.
            const uint32_t idx = (totalMipCount - 2) - index++;
            streamLayout[idx] = mipType;
        }
    }

    rapidjson::Value::ConstMemberIterator mipInfoIt;

    if (JSON_GetIterator(document, TEXTURE_MIP_INFO_FIELD, mipInfoIt))
    {
        Texture_ValidateMetadataArray(mipInfoIt->value, totalMipCount, TEXTURE_MIP_INFO_FIELD);
        const rapidjson::Value::ConstArray& mipInfoArray = mipInfoIt->value.GetArray();

        // note: unclamped loop and write into static sized array because we
        // have already confirmed that the texture mip count is sane.
        uint32_t index = 0;

        for (const js::Value& mipInfoEntry : mipInfoArray)
        {
            int32_t mipInfo;

            if (!JSON_ParseNumber(mipInfoEntry, mipInfo))
                Error("Failed to parse \"" TEXTURE_MIP_INFO_FIELD "\" #%u from texture metadata.\n", index);

            hdr->unkPerMip[index++] = static_cast<uint8_t>(mipInfo);
        }
    }

    rapidjson::Value::ConstMemberIterator resourceFlagsIt;

    if (JSON_GetIterator(document, TEXTURE_RESOURCE_FLAGS_FIELD, resourceFlagsIt))
    {
        int32_t resourceFlags;

        if (!JSON_ParseNumber(resourceFlagsIt->value, resourceFlags))
            Error("Failed to parse \"" TEXTURE_RESOURCE_FLAGS_FIELD "\" from texture metadata.\n");

        hdr->resourceFlags = static_cast<uint8_t>(resourceFlags);
    }

    rapidjson::Value::ConstMemberIterator usageFlagsIt;

    if (JSON_GetIterator(document, TEXTURE_USAGE_FLAGS_FIELD, usageFlagsIt))
    {
        int32_t usageFlags;

        if (!JSON_ParseNumber(usageFlagsIt->value, usageFlags))
            Error("Failed to parse \"" TEXTURE_USAGE_FLAGS_FIELD "\" from texture metadata.\n");

        hdr->usageFlags = static_cast<uint8_t>(usageFlags);
    }
}

// materialGeneratedTexture - whether this texture's creation was invoked by material automatic texture generation
static void Texture_InternalAddTexture(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const bool forceDisableStreaming)
{
    PakAsset_t& asset = pak->BeginAsset(assetGuid, assetPath);

    const std::string textureFilePath = Utils::ChangeExtension(pak->GetAssetPath() + assetPath, ".dds");
    BinaryIO input;

    if (!input.Open(textureFilePath, BinaryIO::Mode_e::Read))
        Error("Failed to open texture asset \"%s\".\n", textureFilePath.c_str());

    PakPageLump_s hdrChunk = pak->CreatePageLump(sizeof(TextureAssetHeader_t), SF_HEAD, 8);
    TextureAssetHeader_t* const hdr = reinterpret_cast<TextureAssetHeader_t*>(hdrChunk.data);

    // used for creating data buffers
    struct {
        int64_t staticSize;
        int64_t streamedSize;
        int64_t streamedOptSize;
    } mipSizes{};

    // parse input image file
    int magic;
    input.Read(magic);

    if (magic != DDS_MAGIC) // b'DDS '
        Error("Attempted to add a texture asset that was not a valid DDS file (invalid magic).\n");

    DDS_HEADER ddsh;
    input.Read(ddsh);

    if (ddsh.dwMipMapCount > MAX_MIPS_PER_TEXTURE)
        Error("Attempted to add a texture asset with too many mipmaps (max %u, got %u).\n", MAX_MIPS_PER_TEXTURE, ddsh.dwMipMapCount);

    std::vector<mipType_e> streamLayout;
    Texture_ProcessMetaData(pak, assetPath, hdr, ddsh.dwMipMapCount, streamLayout);

    DXGI_FORMAT dxgiFormat = DXGI_FORMAT_UNKNOWN;

    uint8_t arraySize = 1;
    bool isDX10 = false;

    // Go to the end of the DX10 header if it exists.
    if (ddsh.ddspf.dwFourCC == '01XD')
    {
        DDS_HEADER_DXT10 ddsh_dx10;
        input.Read(ddsh_dx10);

        dxgiFormat = ddsh_dx10.dxgiFormat;
        arraySize = static_cast<uint8_t>(ddsh_dx10.arraySize);
        isDX10 = true;
    }
    else {
        dxgiFormat = DXUtils::GetFormatFromHeader(ddsh);

        if (dxgiFormat == DXGI_FORMAT_UNKNOWN)
            Error("Attempted to add a texture asset from which the format type couldn't be classified.\n");
    }

    const char* const pDxgiFormat = DXUtils::GetFormatAsString(dxgiFormat);
    const uint16_t imageFormat = Texture_DXGIToImageFormat(dxgiFormat);

    if (imageFormat == TEXTURE_INVALID_FORMAT_INDEX)
        Error("Attempted to add a texture asset using an unsupported format type \"%s\".\n", pDxgiFormat);

    hdr->imageFormat = imageFormat;
    Debug("-> fmt: %s\n", pDxgiFormat);

    hdr->width = static_cast<uint16_t>(ddsh.dwWidth);
    hdr->height = static_cast<uint16_t>(ddsh.dwHeight);
    Debug("-> dimensions: %ux%u\n", ddsh.dwWidth, ddsh.dwHeight);

    bool isStreamable = false; // does this texture require streaming? true if total size of mip levels would exceed 64KiB. can be forced to false.
    bool isStreamableOpt = false; // can this texture use optional starpaks? can only be set if pak is version v8

    // set streamable boolean based on if we have disabled it, also don't stream if we have only one mip
    if (!forceDisableStreaming && ddsh.dwMipMapCount > 1)
        isStreamable = true;

    if (isStreamable && pak->GetVersion() >= 8)
        isStreamableOpt = true;

    /*MIPMAP HANDLING*/
    hdr->arraySize = arraySize;
    std::vector<std::vector<mipLevel_t>> textureArray(arraySize);

    for (auto& mips : textureArray)
        mips.resize(ddsh.dwMipMapCount);

    size_t mipOffset = isDX10 ? 0x94 : 0x80; // add header length
    bool firstTexture = true;

    for (auto& mips : textureArray)
    {
        for (unsigned int mipLevel = 0; mipLevel < mips.size(); mipLevel++)
        {
            // subtracts 1 so skip mips w/h at 1, gets added back when setting in mipLevel_t
            uint16_t mipWidth = 0;
            if (hdr->width >> mipLevel > 1)
                mipWidth = (hdr->width >> mipLevel) - 1;

            uint16_t mipHeight = 0;
            if (hdr->height >> mipLevel > 1)
                mipHeight = (hdr->height >> mipLevel) - 1;

            const auto& bytesPerPixel = s_pBytesPerPixel[hdr->imageFormat];

            const uint8_t x = bytesPerPixel.x;
            const uint8_t y = bytesPerPixel.y;

            const uint32_t bppWidth = (y + mipWidth) >> (y >> 1);
            const uint32_t bppHeight = (y + mipHeight) >> (y >> 1);

            const uint32_t slicePitch = x * bppWidth * bppHeight;
            const uint32_t alignedSize = IALIGN16(slicePitch);

            mipLevel_t& mipMap = mips[mipLevel];

            mipMap.mipOffset = mipOffset;
            mipMap.mipSize = slicePitch;
            mipMap.mipSizeAligned = alignedSize;
            mipMap.mipWidth = static_cast<uint16_t>(mipWidth + 1);
            mipMap.mipHeight = static_cast<uint16_t>(mipHeight + 1);
            mipMap.mipLevel = static_cast<uint8_t>(mipLevel + 1);
            mipMap.mipType = mipType_e::INVALID;

            hdr->dataSize += alignedSize; // all mips are aligned to 16 bytes within rpak/starpak
            mipOffset += slicePitch; // add size for the next mip's offset

            // important:
            // - texture arrays cannot be streamed, the mips for each texture
            //   must be equally mapped and they can only be stored permanently.
            // 
            // - there must always be at least 1 permanent mip level, regardless
            //   of its size. not adhering to this rule will result in a failure
            //   in ID3D11Device::CreateTexture2D during the runtime. we make
            //   sure that the smallest mip is always permanent (static) here.
            if (arraySize == 1 && (mipLevel != (ddsh.dwMipMapCount - 1)))
            {
                const mipType_e override = streamLayout.empty() 
                    ? mipType_e::INVALID 
                    : streamLayout[mipLevel];

                if (override != mipType_e::STATIC)
                {
                    // if opt streamable textures are enabled, check if this mip is supposed to be opt streamed
                    if (isStreamableOpt && (override == mipType_e::INVALID ? (alignedSize > MAX_STREAM_MIP_SIZE) : (override == mipType_e::STREAMED_OPT)))
                    {
                        mipSizes.streamedOptSize += alignedSize; // only reason this is done is to create the data buffers
                        hdr->optStreamedMipLevels++; // add a streamed mip level

                        mipMap.mipType = mipType_e::STREAMED_OPT;
                    }

                    // if streamable textures are enabled, check if this mip is supposed to be streamed
                    else if (isStreamable && (override == mipType_e::INVALID ? (alignedSize > MAX_PERM_MIP_SIZE) : (override == mipType_e::STREAMED)))
                    {
                        mipSizes.streamedSize += alignedSize; // only reason this is done is to create the data buffers
                        hdr->streamedMipLevels++; // add a streamed mip level

                        mipMap.mipType = mipType_e::STREAMED;
                    }
                }
            }

            // texture was not streamed, make it permanent.
            if (mipMap.mipType == mipType_e::INVALID)
            {
                mipSizes.staticSize += alignedSize;

                // Only count mips for the first texture, other textures in the
                // array must have an equal amount of mips as all mips between
                // textures in the array are grouped together into a contiguous
                // block of memory, and indexed by the aligned mip size times
                // the texture index.
                if (firstTexture)
                    hdr->mipLevels++;

                mipMap.mipType = mipType_e::STATIC;
            }
        }

        firstTexture = false;
    }

    hdr->guid = assetGuid;
    Debug("-> total mipmaps permanent:mandatory:optional : %hhu:%hhu:%hhu\n", hdr->mipLevels, hdr->streamedMipLevels, hdr->optStreamedMipLevels);

    if (pak->IsFlagSet(PF_KEEP_DEV))
    {
        char pathStem[PAK_MAX_STEM_PATH];
        const size_t stemLen = Pak_ExtractAssetStem(assetPath, pathStem, sizeof(pathStem), "texture");

        if (stemLen > 0)
        {
            PakPageLump_s nameChunk = pak->CreatePageLump(stemLen + 1, SF_CPU | SF_DEV, 1);
            memcpy(nameChunk.data, pathStem, stemLen + 1);

            pak->AddPointer(hdrChunk, offsetof(TextureAssetHeader_t, name), nameChunk, 0);
        }
    }

    PakPageLump_s dataChunk = pak->CreatePageLump(mipSizes.staticSize, SF_CPU | SF_TEMP, 16);

    // note(amos): page align it because we need to hash this entire block and
    // check for duplicates; starpak data is always page aligned and looked up
    // as such.
    const size_t pageAlignedStreamedSize = IALIGN(mipSizes.streamedSize, STARPAK_DATABLOCK_ALIGNMENT);
    const size_t pageAlignedStreamedOptSize = IALIGN(mipSizes.streamedOptSize, STARPAK_DATABLOCK_ALIGNMENT);

    char* const streamedbuf = new char[pageAlignedStreamedSize];
    char* const optstreamedbuf = new char[pageAlignedStreamedOptSize];

    { // clear the remainder as this will affect the Murmur hash result.
        const size_t streamedbufRemainder = pageAlignedStreamedSize - mipSizes.streamedSize;

        if (streamedbufRemainder > 0)
            memset(&streamedbuf[mipSizes.streamedSize], 0, streamedbufRemainder);

        const size_t optstreamedbufRemainder = pageAlignedStreamedOptSize - mipSizes.streamedOptSize;

        if (optstreamedbufRemainder > 0)
            memset(&optstreamedbuf[mipSizes.streamedOptSize], 0, optstreamedbufRemainder);
    }

    for (size_t i = 0; i < textureArray.size(); i++)
    {
        const auto& mips = textureArray[i];

        char* pCurrentPosStatic = dataChunk.data;
        char* pCurrentPosStreamed = streamedbuf;
        char* pCurrentPosStreamedOpt = optstreamedbuf;

        for (auto mipIter = mips.rbegin(); mipIter != mips.rend(); ++mipIter)
        {
            const mipLevel_t& mipMap = *mipIter;
            input.SeekGet(mipMap.mipOffset);

            // only used by static mip types, used for offsetting the pointer
            // from mip base into the actual mip corresponding to the texture
            // in the array.
            char* targetPos;

            switch (mipMap.mipType)
            {
            case mipType_e::STATIC:
                targetPos = pCurrentPosStatic + (mipMap.mipSizeAligned * i);
                input.Read(targetPos, mipMap.mipSize);

                // texture arrays group mips together, i.e. mip 1 of texture 1
                // 2 and 3 are directly placed into a contiguous block, and to
                // access the second one, the mip size must be multiplied by
                // the texture index.
                pCurrentPosStatic += mipMap.mipSizeAligned * textureArray.size(); // move ptr

                break;
            case mipType_e::STREAMED:
                input.Read(pCurrentPosStreamed, mipMap.mipSize);
                pCurrentPosStreamed += mipMap.mipSizeAligned; // move ptr

                break;
            case mipType_e::STREAMED_OPT:
                input.Read(pCurrentPosStreamedOpt, mipMap.mipSize);
                pCurrentPosStreamedOpt += mipMap.mipSizeAligned; // move ptr

                break;
            default:
                break;
            }
        }
    }

    // now time to add the higher level asset entry
    PakStreamSetEntry_s mandatoryStreamData;

    if (isStreamable && hdr->streamedMipLevels > 0)
        mandatoryStreamData = pak->AddStreamingDataEntry(pageAlignedStreamedSize, (uint8_t*)streamedbuf, STREAMING_SET_MANDATORY);

    delete[] streamedbuf;

    PakStreamSetEntry_s optionalStreamData;

    if (isStreamableOpt && hdr->optStreamedMipLevels > 0)
        optionalStreamData = pak->AddStreamingDataEntry(pageAlignedStreamedOptSize, (uint8_t*)optstreamedbuf, STREAMING_SET_OPTIONAL);

    delete[] optstreamedbuf;

    asset.InitAsset(hdrChunk.GetPointer(), sizeof(TextureAssetHeader_t), dataChunk.GetPointer(), TXTR_VERSION, AssetType::TXTR,
        mandatoryStreamData.streamOffset, mandatoryStreamData.streamIndex, optionalStreamData.streamOffset, optionalStreamData.streamIndex);

    asset.SetHeaderPointer(hdrChunk.data);

    pak->FinishAsset();
}

bool Texture_AutoAddTexture(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const bool forceDisableStreaming); // defined after v10 writer

void Assets::AddTextureAsset_v8(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry)
{
    const bool disableStreaming = JSON_GetValueOrDefault(mapEntry, "$disableStreaming", false);
    Texture_InternalAddTexture(pak, assetGuid, assetPath, disableStreaming);
}

//=============================================================================
// txtr v10. Permanent mips in the rpak; streamed mips in our starpak (or reused).
// $hdrTail copies [0x14..0x37] from the source pak -- dataSize is stream-dependent.
//=============================================================================
#define TXTR_V10_TAIL_OFF  0x14
#define TXTR_V10_TAIL_SIZE (0x38 - TXTR_V10_TAIL_OFF) // 0x24 (36)

struct TextureV10Meta_s
{
    uint8_t  strmMips = 0;          // mandatory-streamed mip count (referenced, not packed)
    uint8_t  optMips = 0;           // optional-streamed mip count (referenced, not packed)
    uint8_t  layerCount = 0;        // +0x11 (cubemap if & 2); RSX drops it from the DDS, source-pak byte
    bool     hasTail = false;
    uint8_t  hdrTail[TXTR_V10_TAIL_SIZE] = {}; // header bytes 0x14..0x37 copied from source pak
    int64_t  starpakOff = -1;       // offset into the mandatory starpak (pc_all_sdk.starpak)
    int64_t  optStarpakOff = -1;    // offset into the optional starpak (pc_all_sdk.opt.starpak)
};

static void Texture_InternalAddTexture_v10(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const TextureV10Meta_s& meta)
{
    PakAsset_t& asset = pak->BeginAsset(assetGuid, assetPath);

    const std::string textureFilePath = Utils::ChangeExtension(pak->GetAssetPath() + assetPath, ".dds");
    BinaryIO input;

    if (!input.Open(textureFilePath, BinaryIO::Mode_e::Read))
        Error("Failed to open texture asset \"%s\".\n", textureFilePath.c_str());

    PakPageLump_s hdrChunk = pak->CreatePageLump(sizeof(TextureAssetHeader_v10_t), SF_HEAD, 8);
    TextureAssetHeader_v10_t* const hdr = reinterpret_cast<TextureAssetHeader_v10_t*>(hdrChunk.data);

    int magic;
    input.Read(magic);

    if (magic != DDS_MAGIC)
        Error("Attempted to add a texture asset that was not a valid DDS file (invalid magic).\n");

    DDS_HEADER ddsh;
    input.Read(ddsh);

    if (ddsh.dwMipMapCount > MAX_MIPS_PER_TEXTURE)
        Error("Attempted to add a texture asset with too many mipmaps (max %u, got %u).\n", MAX_MIPS_PER_TEXTURE, ddsh.dwMipMapCount);

    DXGI_FORMAT dxgiFormat = DXGI_FORMAT_UNKNOWN;
    uint8_t arraySize = 1;
    bool isDX10 = false;

    if (ddsh.ddspf.dwFourCC == '01XD')
    {
        DDS_HEADER_DXT10 ddsh_dx10;
        input.Read(ddsh_dx10);

        dxgiFormat = ddsh_dx10.dxgiFormat;
        arraySize = static_cast<uint8_t>(ddsh_dx10.arraySize);
        isDX10 = true;
    }
    else
    {
        dxgiFormat = DXUtils::GetFormatFromHeader(ddsh);

        if (dxgiFormat == DXGI_FORMAT_UNKNOWN)
            Error("Attempted to add a texture asset from which the format type couldn't be classified.\n");
    }

    const uint16_t imageFormat = Texture_DXGIToImageFormat(dxgiFormat);

    if (imageFormat == TEXTURE_INVALID_FORMAT_INDEX)
        Error("Attempted to add a texture asset using an unsupported format \"%s\".\n", DXUtils::GetFormatAsString(dxgiFormat));

    hdr->imgFormat = imageFormat;
    hdr->width = static_cast<uint16_t>(ddsh.dwWidth);
    hdr->height = static_cast<uint16_t>(ddsh.dwHeight);
    hdr->arraySize = arraySize;
    hdr->layerCount = meta.layerCount; // +0x11 cubemap flag (RSX drops it from the DDS)

    // texture arrays cannot be streamed (v8 invariant) -> force all permanent.
    const uint8_t strmMips = (arraySize > 1) ? 0 : meta.strmMips;
    const uint8_t optMips = (arraySize > 1) ? 0 : meta.optMips;
    if (static_cast<unsigned>(strmMips) + optMips >= ddsh.dwMipMapCount)
        Error("Texture \"%s\": streamed mip count (%u+%u) exceeds mip count %u.\n", assetPath, strmMips, optMips, ddsh.dwMipMapCount);

    hdr->permanentMipLevels = static_cast<uint8_t>(ddsh.dwMipMapCount - strmMips - optMips);
    hdr->streamedMipLevels = strmMips;
    hdr->optStreamedMipLevels = optMips;

    // per-mip size/aligned size (same for every array slice -- depends only on the
    // mip dimensions), plus per-(slice,mip) DDS file offset. The DDS stores array
    // textures slice-major, mip-minor (largest mip first), so file offsets accumulate
    // by slicePitch across slices then mips. All mips are stored permanently here.
    struct MipInfo_s { uint32_t size; uint32_t alignedSize; };
    std::vector<MipInfo_s> mips(ddsh.dwMipMapCount);
    std::vector<std::vector<size_t>> fileOffset(arraySize, std::vector<size_t>(ddsh.dwMipMapCount));

    // mip index 0 = largest. The streamed mips are the LARGEST: optMips (indices
    // [0..optMips)) go to the optional starpak, the next strmMips to the mandatory
    // starpak; the permanent (smallest) mips [firstPermMip..N) are packed into the rpak.
    const unsigned int firstPermMip = static_cast<unsigned int>(optMips) + strmMips;
    size_t permanentDataSize = 0;
    size_t mipOffset = isDX10 ? 0x94 : 0x80;

    for (unsigned int slice = 0; slice < arraySize; slice++)
    {
        for (unsigned int m = 0; m < ddsh.dwMipMapCount; m++)
        {
            uint16_t mipWidth = 0;
            if ((hdr->width >> m) > 1) mipWidth = static_cast<uint16_t>((hdr->width >> m) - 1);

            uint16_t mipHeight = 0;
            if ((hdr->height >> m) > 1) mipHeight = static_cast<uint16_t>((hdr->height >> m) - 1);

            const auto& bpp = s_pBytesPerPixel[hdr->imgFormat];
            const uint32_t bppWidth = (bpp.y + mipWidth) >> (bpp.y >> 1);
            const uint32_t bppHeight = (bpp.y + mipHeight) >> (bpp.y >> 1);
            const uint32_t slicePitch = bpp.x * bppWidth * bppHeight;

            if (slice == 0)
            {
                mips[m] = { slicePitch, IALIGN16(slicePitch) };
                // dataSize is the total logical size across ALL sources (perm + streamed),
                // matching the v8 writer; the rpak data lump only holds the permanent mips.
                hdr->dataSize += IALIGN16(slicePitch) * arraySize;
                if (m >= firstPermMip)
                    permanentDataSize += static_cast<size_t>(IALIGN16(slicePitch)) * arraySize;
            }

            fileOffset[slice][m] = mipOffset;
            mipOffset += slicePitch;
        }
    }

    // permanent data lump: only the smallest (permanent) mips, mip-major (smallest
    // first), slice-minor. Within each mip the array slices are a contiguous block
    // (slice i at alignedSize*i), matching the v8 array layout.
    PakPageLump_s dataChunk = pak->CreatePageLump(permanentDataSize, SF_CPU | SF_TEMP, 16);
    char* pCur = dataChunk.data;

    for (int m = static_cast<int>(ddsh.dwMipMapCount) - 1; m >= static_cast<int>(firstPermMip); m--) // smallest perm mip first
    {
        for (unsigned int slice = 0; slice < arraySize; slice++)
        {
            input.SeekGet(fileOffset[slice][m]);
            input.Read(pCur + mips[m].alignedSize * slice, mips[m].size);
        }
        pCur += mips[m].alignedSize * arraySize;
    }

    if (pak->IsFlagSet(PF_KEEP_DEV))
    {
        char pathStem[PAK_MAX_STEM_PATH];
        const size_t stemLen = Pak_ExtractAssetStem(assetPath, pathStem, sizeof(pathStem), "texture");

        if (stemLen > 0)
        {
            PakPageLump_s nameChunk = pak->CreatePageLump(stemLen + 1, SF_CPU | SF_DEV, 1);
            memcpy(nameChunk.data, pathStem, stemLen + 1);
            pak->AddPointer(hdrChunk, offsetof(TextureAssetHeader_v10_t, name), nameChunk, 0);
        }
    }

    // Overlay the source pak's header block [0x14..0x37] verbatim (dataSize +
    // perm/strm/opt counts + type + compTypePacked/compressedBytes/unk metadata) for a
    // byte-exact 1:1 header; the structural fields [0x00..0x13] stay computed+validated.
    if (meta.hasTail)
        memcpy(reinterpret_cast<uint8_t*>(hdr) + TXTR_V10_TAIL_OFF, meta.hdrTail, sizeof(meta.hdrTail));
    else if ((strmMips + optMips) > 0)
    {
        // +0x1B is minStreamableMipsToLoad. 0 underflows unsigned in the
        // client's stream request builder and overruns the mip malloc.
        Warning("Texture \"%s\": streamed mips without $hdrTail; synthesizing v10 tail.\n", assetPath);
        hdr->unk_1B = 1;
        hdr->unkMipLevels = 0;
        hdr->compTypePacked = 0;
        uint32_t streamedBytes = 0;
        const unsigned int streamedCount = static_cast<unsigned int>(strmMips) + optMips;
        const unsigned int n = streamedCount < 7u ? streamedCount : 7u;
        for (unsigned int m = 0; m < n; m++)
        {
            const uint32_t sz = mips[m].alignedSize;
            hdr->compressedBytes[m] = static_cast<uint16_t>((sz - 1u) / 4096u);
            streamedBytes += sz;
        }
        hdr->dataSize = streamedBytes;
    }

    // Streamed mips: largest-first (mip 0 first). Permanent rpak mips are smallest-first --
    // flipping this order keeps the entry size and corrupts the pixels.
    auto writeStreamedRange = [&](const unsigned int loMip, const unsigned int hiMip, const PakStreamSet_e set) -> PakStreamSetEntry_s
    {
        size_t streamedSize = 0;
        for (unsigned int m = loMip; m < hiMip; m++)
            streamedSize += static_cast<size_t>(mips[m].alignedSize) * arraySize;

        const size_t pageAligned = IALIGN(streamedSize, STARPAK_DATABLOCK_ALIGNMENT);
        char* const sbuf = new char[pageAligned](); // zero-init (per-mip padding + page tail = zeros, matches kral)

        char* pCur = sbuf;
        for (unsigned int m = loMip; m < hiMip; m++) // LARGEST streamed mip first (matches kral/engine)
        {
            for (unsigned int slice = 0; slice < arraySize; slice++)
            {
                input.SeekGet(fileOffset[slice][m]);
                input.Read(pCur + mips[m].alignedSize * slice, mips[m].size);
            }
            pCur += mips[m].alignedSize * arraySize;
        }

        const PakStreamSetEntry_s entry = pak->AddStreamingDataEntry(pageAligned, (uint8_t*)sbuf, set);
        delete[] sbuf;
        return entry;
    };

    PakStreamSetEntry_s mandatoryStream;
    PakStreamSetEntry_s optionalStream;
    pak->TryReuseStreaming(assetGuid, &mandatoryStream, &optionalStream);

    if (strmMips > 0 && mandatoryStream.streamOffset < 0)
        mandatoryStream = writeStreamedRange(optMips, firstPermMip, STREAMING_SET_MANDATORY);

    if (optMips > 0 && optionalStream.streamOffset < 0)
        optionalStream = writeStreamedRange(0, optMips, STREAMING_SET_OPTIONAL);

    asset.InitAsset(hdrChunk.GetPointer(), sizeof(TextureAssetHeader_v10_t), dataChunk.GetPointer(), 10, AssetType::TXTR,
        strmMips > 0 ? mandatoryStream.streamOffset : -1, strmMips > 0 ? mandatoryStream.streamIndex : -1,
        optMips > 0 ? optionalStream.streamOffset : -1, optMips > 0 ? optionalStream.streamIndex : -1);
    asset.SetHeaderPointer(hdrChunk.data);

    pak->FinishAsset();
}

void Assets::AddTextureAsset_v10(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry)
{
    TextureV10Meta_s meta;

    meta.strmMips = static_cast<uint8_t>(JSON_GetValueOrDefault(mapEntry, "$strmMips", 0u));
    meta.optMips = static_cast<uint8_t>(JSON_GetValueOrDefault(mapEntry, "$optMips", 0u));
    meta.layerCount = static_cast<uint8_t>(JSON_GetValueOrDefault(mapEntry, "$layerCount", 0u));
    meta.starpakOff = JSON_GetValueOrDefault(mapEntry, "$starpakOff", static_cast<int64_t>(-1));
    meta.optStarpakOff = JSON_GetValueOrDefault(mapEntry, "$optStarpakOff", static_cast<int64_t>(-1));

    // $hdrTail = hex of the source pak's header bytes [0x1B..0x37] (29B), copied verbatim.
    const char* const tailHex = JSON_GetValueOrDefault(mapEntry, "$hdrTail", static_cast<const char*>(nullptr));
    if (tailHex)
    {
        const size_t hexLen = strlen(tailHex);
        if (hexLen != sizeof(meta.hdrTail) * 2)
            Error("Texture \"%s\": $hdrTail must be %zu hex chars, got %zu.\n", assetPath, sizeof(meta.hdrTail) * 2, hexLen);

        for (size_t i = 0; i < sizeof(meta.hdrTail); i++)
            meta.hdrTail[i] = static_cast<uint8_t>(strtoul(std::string(tailHex + i * 2, 2).c_str(), nullptr, 16));

        meta.hasTail = true;
    }

    Texture_InternalAddTexture_v10(pak, assetGuid, assetPath, meta);
}

bool Texture_AutoAddTexture(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const bool forceDisableStreaming)
{
    PakAsset_t* const existingAsset = pak->GetAssetByGuid(assetGuid, nullptr, true);

    if (existingAsset)
        return false; // already present in the pak.

    Debug("Auto-adding 'txtr' asset \"%s\".\n", assetPath);

    // S21 (pak v8, non-dedi) must use the v10 writer. Material auto-add used to
    // call Texture_InternalAddTexture (v8) and ship txtr v8 that AVs on S21 load.
    if (pak->GetVersion() >= 8 && !pak->IsFlagSet(PF_DEDI))
    {
        TextureV10Meta_s meta{};
        UNUSED(forceDisableStreaming);
        Texture_InternalAddTexture_v10(pak, assetGuid, assetPath, meta);
    }
    else
    {
        Texture_InternalAddTexture(pak, assetGuid, assetPath, forceDisableStreaming);
    }

    return true;
}
