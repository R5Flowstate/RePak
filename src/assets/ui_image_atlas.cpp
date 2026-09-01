#include "pch.h"
#include "assets.h"
#include "utils/dxutils.h"
#include "public/texture.h"
#include <string>
#include <vector>

extern bool Texture_AutoAddTexture(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const bool forceDisableStreaming);

static std::vector<uint8_t> HexStringToBytes(const char* hexStr)
{
    std::vector<uint8_t> bytes;
    if (!hexStr || !hexStr[0])
        return bytes;

    size_t len = strlen(hexStr);
    bytes.reserve(len / 2);

    for (size_t i = 0; i + 1 < len; i += 2)
    {
        char byte[3] = { hexStr[i], hexStr[i + 1], 0 };
        bytes.push_back(static_cast<uint8_t>(strtoul(byte, nullptr, 16)));
    }
    return bytes;
}

static bool UIImage_OpenFile(CPakFileBuilder* const pak, const char* const assetPath, rapidjson::Document& document)
{
    const std::string fileName = pak->GetAssetPath() + assetPath;
    return JSON_ParseFromFile(fileName.c_str(), "uimg asset", document, true);
}

// page lump structure and order:
// - header        HEAD        (align=8)
// - image offsets CPU_CLIENT  (align=32)
// - information   CPU_CLIENT  (align=4?16)
// - uv data       TEMP_CLIENT (align=4)
void Assets::AddUIImageAsset_v10(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry)
{
    // Check if assetPath is a JSON file with hex format
    const std::string pathStr(assetPath);
    if (pathStr.size() > 5 && pathStr.substr(pathStr.size() - 5) == ".json")
    {
        rapidjson::Document document;
        if (UIImage_OpenFile(pak, assetPath, document))
        {
            if (document.HasMember("cpuDataHex"))
            {
                AddUIImageAsset_v10_FromHex(pak, assetGuid, assetPath, document);
                return;
            }
        }
    }

    const char* const atlasPath = JSON_GetValueRequired<const char*>(mapEntry, "atlas");
    const PakGuid_t atlasGuid = RTech::StringToGuid(atlasPath);

    // note: we error here as we can't check if it was added as a streamed texture, and uimg doesn't support texture streaming.
    if (!Texture_AutoAddTexture(pak, atlasGuid, atlasPath, true/*streaming disabled as uimg can not be streamed*/))
        Error("Atlas texture \"%s\" with GUID 0x%llX was already added as Texture asset; it can only be added through an UI image atlas asset.\n", atlasPath, atlasGuid);

    PakAsset_t& asset = pak->BeginAsset(assetGuid, assetPath);
    PakAsset_t* const atlasAsset = pak->GetAssetByGuid(atlasGuid, nullptr);

    // this really shouldn't be triggered, since the texture is either automatically added above, or a fatal error is thrown
    // there is no code path in AddTextureAsset in which the texture does not exist after the call and still continues execution
    if (!atlasAsset) [[ unlikely ]]
    {
        assert(0);
        Error("Internal failure while adding atlas texture \"%s\" with GUID 0x%llX.\n", atlasPath, assetGuid);
    }

    // make sure referenced asset is a texture for sanity
    atlasAsset->EnsureType(TYPE_TXTR);

    rapidjson::Value::ConstMemberIterator imagesIt;
    JSON_GetRequired(mapEntry, "images", JSONFieldType_e::kArray, imagesIt);

    const rapidjson::Value::ConstArray& imageArray = imagesIt->value.GetArray();
    const size_t imageArraySize = imageArray.Size();

    if (imageArraySize > MAX_UI_ATLAS_IMAGES)
        Error("UI image atlas contains too many images (max %zu, got %zu).\n", (size_t)MAX_UI_ATLAS_IMAGES, imageArraySize);

    // needs to be reversed still, not all uimg's use this! this might be
    // necessary to reverse at some point since some uimg's (especially in
    // world rui's) seem to flicker or glitch at certain view angles and the
    // only data we currently do not set is this.
    const uint16_t unkCount = (uint16_t)JSON_GetValueOrDefault(mapEntry, "unkCount", 0);

    PakPageLump_s hdrLump = pak->CreatePageLump(sizeof(UIImageAtlasHeader_t), SF_HEAD | SF_CLIENT, 8);

    UIImageAtlasHeader_t* const pHdr = reinterpret_cast<UIImageAtlasHeader_t*>(hdrLump.data);
    const TextureAssetHeader_t* const atlasHdr = reinterpret_cast<const TextureAssetHeader_t*>(atlasAsset->header);

    pHdr->width = atlasHdr->width;
    pHdr->height = atlasHdr->height;

    pHdr->widthRatio = 1.f / pHdr->width;
    pHdr->heightRatio = 1.f / pHdr->height;

    pHdr->imageCount = static_cast<uint16_t>(imageArraySize);

    // needs to be reversed still, not all uimg's use this! this might be
    // necessary to reverse at some point since some uimg's (especially in
    // world rui's) seem to flicker or glitch at certain view angles and the
    // only data we currently do not set is this.
    pHdr->unkCount = static_cast<uint16_t>(JSON_GetValueOrDefault(mapEntry, "unkCount", 0));

    pHdr->atlasGUID = atlasGuid;

    Pak_RegisterGuidRefAtOffset(atlasGuid, offsetof(UIImageAtlasHeader_t, atlasGUID), hdrLump, asset);

    const size_t imageOffsetsDataSize = sizeof(UIImageOffset) * imageArraySize;

    // ui image offset info
    PakPageLump_s offsetLump = pak->CreatePageLump(imageOffsetsDataSize, SF_CPU | SF_CLIENT, 32);
    rmem ofBuf(offsetLump.data);

    // set image offset page index and offset
    pak->AddPointer(hdrLump, offsetof(UIImageAtlasHeader_t, pImageOffsets), offsetLump, 0);

    ////////////////////
    // IMAGE OFFSETS
    // Helper to parse float, detecting string "-0" for negative zero
    auto ParseFloatNegZero = [](const rapidjson::Value& obj, const char* name, float defaultVal) -> float
    {
        rapidjson::Value::ConstMemberIterator mit = obj.FindMember(name);
        if (mit == obj.MemberEnd())
            return defaultVal;

        // Check for string literal "-0" (negative zero)
        if (mit->value.IsString())
        {
            const char* str = mit->value.GetString();
            if (strcmp(str, "-0") == 0)
            {
                // Return -0.0f with sign bit set
                uint32_t negZeroBits = 0x80000000u;
                float result;
                std::memcpy(&result, &negZeroBits, sizeof(result));
                return result;
            }
            // Try parsing as float
            char* end = nullptr;
            float f = std::strtof(str, &end);
            return f;
        }

        if (!mit->value.IsNumber())
            return defaultVal;

        if (mit->value.IsDouble() || mit->value.IsLosslessDouble())
            return static_cast<float>(mit->value.GetDouble());
        if (mit->value.IsFloat() || mit->value.IsLosslessFloat())
            return mit->value.GetFloat();
        if (mit->value.IsInt())
            return static_cast<float>(mit->value.GetInt());
        if (mit->value.IsInt64())
            return static_cast<float>(mit->value.GetInt64());
        if (mit->value.IsUint())
            return static_cast<float>(mit->value.GetUint());
        if (mit->value.IsUint64())
            return static_cast<float>(mit->value.GetUint64());

        return defaultVal;
    };

    for (const rapidjson::Value& it : imageArray)
    {
        UIImageOffset uiio;

        uiio.cropInsetLeft = ParseFloatNegZero(it, "cropInsetLeft", 0.0f);
        uiio.cropInsetTop = ParseFloatNegZero(it, "cropInsetTop", 0.0f);

        uiio.endAnchorX = ParseFloatNegZero(it, "endAnchorX", 1.0f);
        uiio.endAnchorY = ParseFloatNegZero(it, "endAnchorY", 1.0f);

        uiio.startAnchorX = ParseFloatNegZero(it, "startAnchorX", 0.0f);
        uiio.startAnchorY = ParseFloatNegZero(it, "startAnchorY", 0.0f);

        // Lower means more zoomed in.
        uiio.scaleRatioX = ParseFloatNegZero(it, "scaleRatioX", 1.0f);
        uiio.scaleRatioY = ParseFloatNegZero(it, "scaleRatioY", 1.0f);

        // [amos]: tools like TexturePacker can automatically create entire ui image atlases.
        // TexturePacker can also trim out transparent area's (exactly like how the original
        // Respawn UI image atlas textures have their transparency clipped out). The purpose
        // of this `UIImageAtlasOffset` structure is to account for this; the scaleRatioX
        // and scaleRatioY can zoom the image back out again to reconstruct the trimmed out
        // transparency in the runtime. This is very ideal for keeping the image atlas size
        // as low as possible.
        // 
        // TexturePacker exports the following data alongside the generated atlas texture:
        // {
        // 	"filename": "rui/hud/tactical_icons/pilot_tactical_particle_wall",
        // 	"frame": {"x":1921,"y":1501,"w":62,"h":80},
        // 	"rotated": false,
        // 	"trimmed": true,
        // 	"spriteSourceSize": {"x":33,"y":24,"w":62,"h":80},
        // 	"sourceSize": {"w":128,"h":128}
        // },
        // {
        // 	"filename": "rui/menu/buttons/weapon_categories/marksman",
        // 	"frame": {"x":1226,"y":1,"w":526,"h":329},
        // 	"rotated": false,
        // 	"trimmed": true,
        // 	"spriteSourceSize": {"x":138,"y":55,"w":526,"h":329},
        // 	"sourceSize": {"w":802,"h":440}
        // },
        // 
        // It keeps the source sprite size and positions and provides the actual (new) size
        // and positions of the sprite within the atlas, the idea is to figure out how to
        // compute the scale ratios and anchors based on these values.
        // 
        // For scaleRatio, this seems to get very close:
        // uiio.scaleRatioX = 1.0f + ((float)(sourceWidth - croppedWidth) / (float)croppedWidth);
        // uiio.scaleRatioY = 1.0f + ((float)(sourceHeight - croppedHeight) / (float)croppedHeight);
        // 
        // I'm not sure how to calculate the anchors correctly yet, time ran out when I
        // started to poke around with those. These need to be figured out so that the
        // images are correctly placed again on the RUI mesh within the runtime as scaling
        // does offset the image slightly (which the anchors need to correct).
        // 
        // 
        // Also, on Respawn UI image atlases, the cropInsetLeft and cropInsetTop variables
        // are sometimes set as well, these aren't very important, but probably need more
        // research as well...
        /*
        uint32_t sourcePosX;
        uint32_t sourcePosY;
        uint32_t sourceWidth;
        uint32_t sourceHeight;

        if (JSON_GetValue(it, "sourcePosX", JSONFieldType_e::kUint32, sourcePosX) &&
            JSON_GetValue(it, "sourcePosY", JSONFieldType_e::kUint32, sourcePosY) &&
            JSON_GetValue(it, "sourceWidth", JSONFieldType_e::kUint32, sourceWidth) &&
            JSON_GetValue(it, "sourceHeight", JSONFieldType_e::kUint32, sourceHeight)
            )
        {
            const uint16_t croppedWidth = (uint16_t)JSON_GetNumberRequired<uint32_t>(it, "width");
            const uint16_t croppedHeight = (uint16_t)JSON_GetNumberRequired<uint32_t>(it, "height");

            uiio.scaleRatioX = (float)sourceWidth / (float)croppedWidth;
            uiio.scaleRatioY = (float)sourceHeight / (float)croppedHeight;

            uiio.startAnchorX = (float)sourcePosX / (float)sourceWidth;
            uiio.startAnchorY = (float)sourcePosY / (float)sourceHeight;
            uiio.endAnchorX = ((float)sourcePosX + (float)croppedWidth) / (float)sourceWidth;
            uiio.endAnchorY = ((float)sourcePosY + (float)croppedHeight) / (float)sourceHeight;
        }
        else // Default scale.
        {
            uiio.endAnchorX = 1.0f;
            uiio.endAnchorY = 1.0f;

            uiio.startAnchorX = 0.0f;
            uiio.startAnchorY = 0.0f;

            uiio.scaleRatioX = 1.0f;
            uiio.scaleRatioY = 1.0f;
        }
        */
        ofBuf.write(uiio);
    }

    ////////////////////
    // UNK ARRAY
    PakPageLump_s unkLump{};
    if (unkCount > 0)
    {
        rapidjson::Value::ConstMemberIterator unkIt;
        if (!JSON_GetRequired(mapEntry, "unk", JSONFieldType_e::kArray, unkIt))
            Error("unkCount is %u but \"unk\" array not found in JSON.\n", unkCount);

        const rapidjson::Value::ConstArray& unkArray = unkIt->value.GetArray();
        if (unkArray.Size() != unkCount)
            Error("unkCount is %u but \"unk\" array has %zu elements.\n", unkCount, unkArray.Size());

        const size_t unkDataSize = sizeof(UIImageOffset) * unkCount;
        unkLump = pak->CreatePageLump(unkDataSize, SF_CPU | SF_CLIENT, 16);
        rmem unkBuf(unkLump.data);

        for (const rapidjson::Value& it : unkArray)
        {
            UIImageOffset uiio;
            uiio.cropInsetLeft = ParseFloatNegZero(it, "cropInsetLeft", 0.0f);
            uiio.cropInsetTop = ParseFloatNegZero(it, "cropInsetTop", 0.0f);
            uiio.endAnchorX = ParseFloatNegZero(it, "endAnchorX", 1.0f);
            uiio.endAnchorY = ParseFloatNegZero(it, "endAnchorY", 1.0f);
            uiio.startAnchorX = ParseFloatNegZero(it, "startAnchorX", 0.0f);
            uiio.startAnchorY = ParseFloatNegZero(it, "startAnchorY", 0.0f);
            uiio.scaleRatioX = ParseFloatNegZero(it, "scaleRatioX", 1.0f);
            uiio.scaleRatioY = ParseFloatNegZero(it, "scaleRatioY", 1.0f);
            unkBuf.write(uiio);
        }

        // set unk page index and offset
        pak->AddPointer(hdrLump, offsetof(UIImageAtlasHeader_t, unknown), unkLump, 0);
    }

    const size_t imageDimensionsDataSize = sizeof(uint16_t) * 2 * imageArraySize;
    const size_t imageHashesDataSize = (sizeof(uint32_t) + sizeof(uint32_t)) * imageArraySize;

    // note: aligned to 4 if we do not have UIImageAtlasHeader_t::unkCount
    // (which needs to be reversed still). Else this lump must reside in a
    // page that is aligned to 16.
    PakPageLump_s infoLump = pak->CreatePageLump(imageDimensionsDataSize + imageHashesDataSize, SF_CPU | SF_CLIENT, pHdr->unkCount > 0 ? 16 : 4);
    rmem ifBuf(infoLump.data);

    ///////////////////////
    // IMAGE DIMENSIONS
    // set image dimensions page index and offset
    pak->AddPointer(hdrLump, offsetof(UIImageAtlasHeader_t, pImageDimensions), infoLump, 0);

    for (const rapidjson::Value& it : imageArray)
    {
        // renderWidth/renderHeight allow specifying different render dimensions than the slice size
        // if not specified, falls back to width/height
        const uint16_t width = (uint16_t)JSON_GetValueOrDefault(it, "renderWidth", JSON_GetNumberRequired<int>(it, "width"));
        const uint16_t height = (uint16_t)JSON_GetValueOrDefault(it, "renderHeight", JSON_GetNumberRequired<int>(it, "height"));

        ifBuf.write<uint16_t>(width);
        ifBuf.write<uint16_t>(height);
    }

    // set image hashes page index and offset
    pak->AddPointer(hdrLump, offsetof(UIImageAtlasHeader_t, pImageHashes), infoLump, imageDimensionsDataSize);
    size_t stringBufSize = 0;

    if (pak->IsFlagSet(PF_KEEP_DEV))
    {
        int index = -1;

        for (const rapidjson::Value& it : imageArray)
        {
            index++;

            rapidjson::Value::ConstMemberIterator pathIt;
            JSON_GetRequired(it, "path", JSONFieldType_e::kString, pathIt);

            const size_t pathLen = pathIt->value.GetStringLength();

            if (pathLen == 0)
                Error("Image #%i has an empty name!\n", index);

            stringBufSize += pathLen + 1; // +1 for null terminator.
        }
    }

    PakPageLump_s devLump{};

    if (stringBufSize > 0)
    {
        devLump = pak->CreatePageLump(stringBufSize, SF_CPU | SF_DEV, 1);
        pak->AddPointer(hdrLump, offsetof(UIImageAtlasHeader_t, pImagesNames), devLump, 0);
    }

    uint32_t nextStringTableOffset = 0;
    int index = -1;

    /////////////////////////
    // IMAGE HASHES/NAMES
    for (const rapidjson::Value& it : imageArray)
    {
        index++;

        rapidjson::Value::ConstMemberIterator pathIt;
        JSON_GetRequired(it, "path", JSONFieldType_e::kString, pathIt);

        const size_t pathLen = pathIt->value.GetStringLength();

        if (pathLen == 0)
            Error("Image #%i has an empty name!\n", index);

        const char* const imagePath = pathIt->value.GetString();
        const uint32_t pathHash = RTech::StringToUIMGHash(imagePath);

        ifBuf.write(pathHash);

        if (devLump.data)
        {
            const size_t pathBufSize = pathLen + 1; // +1 for null terminator.
            memcpy(&devLump.data[nextStringTableOffset], imagePath, pathBufSize);

            ifBuf.write(nextStringTableOffset);
            nextStringTableOffset += (uint32_t)pathBufSize;
        }
        else
        {
            // No dev data, don't write the image path.
            ifBuf.write(0ul);
        }
    }

    // cpu data
    PakPageLump_s uvLump = pak->CreatePageLump(imageArraySize * sizeof(UIImageUV), SF_CPU | SF_TEMP | SF_CLIENT, 4);
    rmem uvBuf(uvLump.data);

    //////////////
    // IMAGE UVS
    for (const rapidjson::Value& it : imageArray)
    {
        UIImageUV uiiu;

        const float uv0x = JSON_GetNumberRequired<float>(it, "posX") / pHdr->width;
        const float uv1x = JSON_GetNumberRequired<float>(it, "width") / pHdr->width;

        Debug("X: %f -> %f\n", uv0x, uv0x + uv1x);

        const float uv0y = JSON_GetNumberRequired<float>(it, "posY") / pHdr->height;
        const float uv1y = JSON_GetNumberRequired<float>(it, "height") / pHdr->height;

        Debug("Y: %f -> %f\n", uv0y, uv0y + uv1y);

        uiiu.InitUIImageUV(uv0x, uv0y, uv1x, uv1y);
        uvBuf.write(uiiu);
    }

    asset.InitAsset(hdrLump.GetPointer(), sizeof(UIImageAtlasHeader_t), uvLump.GetPointer(), UIMG_VERSION, AssetType::UIMG);
    asset.SetHeaderPointer(hdrLump.data);

    pak->FinishAsset();
}

// Hex-based reconstruction for 1:1 UIMG from exported JSON
void Assets::AddUIImageAsset_v10_FromHex(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry)
{
    PakGuid_t atlasGuid = 0;
    rapidjson::Value::ConstMemberIterator atlasIt;

    if (JSON_GetIterator(mapEntry, "atlas", JSONFieldType_e::kString, atlasIt))
    {
        const char* const atlasPath = atlasIt->value.GetString();
        atlasGuid = RTech::StringToGuid(atlasPath);

        if (!Texture_AutoAddTexture(pak, atlasGuid, atlasPath, true))
            Error("Atlas texture \"%s\" with GUID 0x%llX was already added.\n", atlasPath, atlasGuid);
    }
    else if (JSON_GetIterator(mapEntry, "atlasGUID", JSONFieldType_e::kString, atlasIt))
    {
        const char* guidStr = atlasIt->value.GetString();
        if (guidStr[0] == '0' && (guidStr[1] == 'x' || guidStr[1] == 'X'))
            atlasGuid = strtoull(guidStr + 2, nullptr, 16);
        else
            atlasGuid = strtoull(guidStr, nullptr, 16);

        Log("Using atlas GUID reference: 0x%llX (texture must be added separately)\n", atlasGuid);
    }
    else
    {
        Error("UIMG asset requires either 'atlas' path or 'atlasGUID'.\n");
    }

    PakAsset_t& asset = pak->BeginAsset(assetGuid, assetPath);

    const int width = JSON_GetNumberOrDefault(mapEntry, "width", 0);
    const int height = JSON_GetNumberOrDefault(mapEntry, "height", 0);
    const float widthRatio = JSON_GetNumberOrDefault(mapEntry, "widthRatio", width > 0 ? 1.0f / width : 0.0f);
    const float heightRatio = JSON_GetNumberOrDefault(mapEntry, "heightRatio", height > 0 ? 1.0f / height : 0.0f);
    const int textureCount = JSON_GetNumberOrDefault(mapEntry, "textureCount", 0);
    const int unkCount = JSON_GetNumberOrDefault(mapEntry, "unkCount", 0);

    if (textureCount == 0)
        Error("UIMG asset has textureCount = 0.\n");

    std::string cpuDataHex, offsetsHex, dimensionsHex, hashTableHex, namesHex, unkHex;
    JSON_GetValue(mapEntry, "cpuDataHex", cpuDataHex);
    JSON_GetValue(mapEntry, "offsetsHex", offsetsHex);
    JSON_GetValue(mapEntry, "dimensionsHex", dimensionsHex);
    JSON_GetValue(mapEntry, "hashTableHex", hashTableHex);
    JSON_GetValue(mapEntry, "namesHex", namesHex);
    JSON_GetValue(mapEntry, "unkHex", unkHex);

    std::vector<uint8_t> cpuData = HexStringToBytes(cpuDataHex.c_str());
    std::vector<uint8_t> offsetsData = HexStringToBytes(offsetsHex.c_str());
    std::vector<uint8_t> dimensionsData = HexStringToBytes(dimensionsHex.c_str());
    std::vector<uint8_t> hashTableData = HexStringToBytes(hashTableHex.c_str());
    std::vector<uint8_t> namesData = HexStringToBytes(namesHex.c_str());
    std::vector<uint8_t> unkData = HexStringToBytes(unkHex.c_str());

    const size_t expectedCpuSize = textureCount * sizeof(UIImageUV);
    const size_t expectedOffsetsSize = textureCount * sizeof(UIImageOffset);
    const size_t expectedDimensionsSize = textureCount * 4;
    const size_t expectedHashSize = textureCount * 8;
    const size_t expectedUnkSize = unkCount * 32;

    if (cpuData.size() != expectedCpuSize)
        Warning("cpuDataHex size mismatch: expected %zu, got %zu\n", expectedCpuSize, cpuData.size());
    if (offsetsData.size() != expectedOffsetsSize)
        Warning("offsetsHex size mismatch: expected %zu, got %zu\n", expectedOffsetsSize, offsetsData.size());
    if (dimensionsData.size() != expectedDimensionsSize)
        Warning("dimensionsHex size mismatch: expected %zu, got %zu\n", expectedDimensionsSize, dimensionsData.size());
    if (hashTableData.size() != expectedHashSize)
        Warning("hashTableHex size mismatch: expected %zu, got %zu\n", expectedHashSize, hashTableData.size());
    if (unkCount > 0 && unkData.size() != expectedUnkSize)
        Warning("unkHex size mismatch: expected %zu, got %zu\n", expectedUnkSize, unkData.size());

    PakPageLump_s hdrLump = pak->CreatePageLump(sizeof(UIImageAtlasHeader_t), SF_HEAD | SF_CLIENT, 8);
    UIImageAtlasHeader_t* const pHdr = reinterpret_cast<UIImageAtlasHeader_t*>(hdrLump.data);

    pHdr->width = static_cast<uint16_t>(width);
    pHdr->height = static_cast<uint16_t>(height);
    pHdr->widthRatio = widthRatio;
    pHdr->heightRatio = heightRatio;
    pHdr->imageCount = static_cast<uint16_t>(textureCount);
    pHdr->unkCount = static_cast<uint16_t>(unkCount);
    pHdr->atlasGUID = atlasGuid;

    Pak_RegisterGuidRefAtOffset(atlasGuid, offsetof(UIImageAtlasHeader_t, atlasGUID), hdrLump, asset);

    PakPageLump_s offsetLump = pak->CreatePageLump(offsetsData.size(), SF_CPU | SF_CLIENT, 32);
    memcpy(offsetLump.data, offsetsData.data(), offsetsData.size());
    pak->AddPointer(hdrLump, offsetof(UIImageAtlasHeader_t, pImageOffsets), offsetLump, 0);

    size_t infoLumpSize = dimensionsData.size() + hashTableData.size();
    size_t unkOffset = 0;
    size_t hashOffset = dimensionsData.size();

    if (unkCount > 0 && !unkData.empty())
    {
        infoLumpSize += unkData.size();
        unkOffset = dimensionsData.size();
        hashOffset = dimensionsData.size() + unkData.size();
    }

    PakPageLump_s infoLump = pak->CreatePageLump(infoLumpSize, SF_CPU | SF_CLIENT, unkCount > 0 ? 16 : 4);

    memcpy(infoLump.data, dimensionsData.data(), dimensionsData.size());
    pak->AddPointer(hdrLump, offsetof(UIImageAtlasHeader_t, pImageDimensions), infoLump, 0);

    if (unkCount > 0 && !unkData.empty())
    {
        memcpy(infoLump.data + unkOffset, unkData.data(), unkData.size());
        pak->AddPointer(hdrLump, offsetof(UIImageAtlasHeader_t, unknown), infoLump, unkOffset);
    }

    memcpy(infoLump.data + hashOffset, hashTableData.data(), hashTableData.size());
    pak->AddPointer(hdrLump, offsetof(UIImageAtlasHeader_t, pImageHashes), infoLump, hashOffset);

    if (!namesData.empty() && pak->IsFlagSet(PF_KEEP_DEV))
    {
        PakPageLump_s namesLump = pak->CreatePageLump(namesData.size(), SF_CPU | SF_DEV, 1);
        memcpy(namesLump.data, namesData.data(), namesData.size());
        pak->AddPointer(hdrLump, offsetof(UIImageAtlasHeader_t, pImagesNames), namesLump, 0);
    }

    PakPageLump_s uvLump = pak->CreatePageLump(cpuData.size(), SF_CPU | SF_TEMP | SF_CLIENT, 4);
    memcpy(uvLump.data, cpuData.data(), cpuData.size());

    asset.InitAsset(hdrLump.GetPointer(), sizeof(UIImageAtlasHeader_t), uvLump.GetPointer(), UIMG_VERSION, AssetType::UIMG);
    asset.SetHeaderPointer(hdrLump.data);

    pak->FinishAsset();

    Log("Added UIMG (hex): %s (%dx%d, %d images)\n", assetPath, width, height, textureCount);
}

// uiia v2: 64-byte header + rawData. Self-pointers in the blob must be relocated or the load handler AVs.
void Assets::AddUIImageAtlasAsset_v2(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry)
{
    UNUSED(mapEntry);

    BinaryIO bio;
    const std::string filePath = pak->GetAssetPath() + assetPath;

    if (!bio.Open(filePath, BinaryIO::Mode_e::Read))
        Error("Failed to open uiia asset \"%s\".\n", assetPath);

    const size_t fileSize = bio.GetSize();
    if (fileSize < 8)
        Error("uiia file \"%s\" is too small (%zu bytes).\n", assetPath, fileSize);

    uint32_t fileMagic = 0, nReloc = 0;
    bio.Read(fileMagic);
    bio.Read(nReloc);
    if (fileMagic != UIIA_FILE_MAGIC)
        Error("uiia file \"%s\" has bad magic (expected %x, got %x).\n", assetPath, UIIA_FILE_MAGIC, fileMagic);

    std::vector<uint32_t> relocOffs(nReloc);
    for (uint32_t i = 0; i < nReloc; i++)
        bio.Read(relocOffs[i]);

    const size_t containerHdr = 8 + (static_cast<size_t>(nReloc) * sizeof(uint32_t));
    if (fileSize < containerHdr + UIIA_V2_HEADER_SIZE)
        Error("uiia file \"%s\" is truncated.\n", assetPath);

    const size_t rawDataSize = fileSize - containerHdr - UIIA_V2_HEADER_SIZE;

    PakAsset_t& asset = pak->BeginAsset(assetGuid, assetPath);

    PakPageLump_s hdrLump = pak->CreatePageLump(UIIA_V2_HEADER_SIZE, SF_HEAD | SF_CLIENT, 16);
    bio.Read(reinterpret_cast<uint8_t*>(hdrLump.data), UIIA_V2_HEADER_SIZE);

    PagePtr_t rawDataPtr = PagePtr_t::NullPtr();
    if (rawDataSize > 0)
    {
        PakPageLump_s rawLump = pak->CreatePageLump(rawDataSize, SF_CPU | SF_CLIENT, 16);
        bio.Read(reinterpret_cast<uint8_t*>(rawLump.data), rawDataSize);
        rawDataPtr = rawLump.GetPointer();

        for (const uint32_t off : relocOffs)
        {
            if (static_cast<size_t>(off) + sizeof(PagePtr_t) > rawDataSize)
                Error("uiia \"%s\" reloc offset %u out of range.\n", assetPath, off);

            const uint32_t targetOff = *reinterpret_cast<const uint32_t*>(&rawLump.data[off + 4]);
            pak->AddPointer(rawLump, off, rawLump, targetOff);
        }
    }
    bio.Close();

    asset.InitAsset(hdrLump.GetPointer(), UIIA_V2_HEADER_SIZE, rawDataPtr, UIIA_VERSION_V2, AssetType::UIIA);
    asset.SetHeaderPointer(hdrLump.data);

    pak->FinishAsset();
}
