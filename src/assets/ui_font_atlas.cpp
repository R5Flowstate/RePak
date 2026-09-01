#include "pch.h"
#include "assets.h"
#include "public/ui_font_atlas.h"

extern bool Texture_AutoAddTexture(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const bool forceDisableStreaming);

static std::vector<uint8_t> DecodeHexString(const char* hexStr, size_t expectedSize)
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

static PakGuid_t ParseHexGuid(const char* hexStr)
{
    if (hexStr[0] == '0' && (hexStr[1] == 'x' || hexStr[1] == 'X'))
        hexStr += 2;

    return strtoull(hexStr, nullptr, 16);
}

static void FontAtlas_OpenFile(CPakFileBuilder* const pak, const char* const assetPath, rapidjson::Document& document)
{
    const std::string fileName = pak->GetAssetPath() + assetPath;

    if (!JSON_ParseFromFile(fileName.c_str(), "font atlas asset", document, true))
        Error("Failed to open font atlas asset \"%s\".\n", fileName.c_str());
}

void Assets::AddFontAtlasAsset_v7(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& /*mapEntry*/)
{
    rapidjson::Document document;
    FontAtlas_OpenFile(pak, assetPath, document);

    const char* const atlasPath = JSON_GetValueRequired<const char*>(document, "atlas");

    PakGuid_t atlasGuid;
    rapidjson::Value::ConstMemberIterator atlasGuidIt;
    if (JSON_GetIterator(document, "atlasGuid", JSONFieldType_e::kString, atlasGuidIt))
    {
        atlasGuid = ParseHexGuid(atlasGuidIt->value.GetString());
    }
    else
    {
        atlasGuid = RTech::StringToGuid(atlasPath);
    }

    if (!Texture_AutoAddTexture(pak, atlasGuid, atlasPath, true))
        Error("Atlas texture \"%s\" with GUID 0x%llX was already added; it can only be added through a font atlas asset.\n", atlasPath, atlasGuid);

    PakAsset_t& asset = pak->BeginAsset(assetGuid, assetPath);
    PakAsset_t* const atlasAsset = pak->GetAssetByGuid(atlasGuid, nullptr);

    if (!atlasAsset) [[ unlikely ]]
    {
        assert(0);
        Error("Internal failure while adding atlas texture \"%s\" with GUID 0x%llX.\n", atlasPath, atlasGuid);
    }

    atlasAsset->EnsureType(TYPE_TXTR);

    const uint16_t width = static_cast<uint16_t>(JSON_GetNumberRequired<int>(document, "width"));
    const uint16_t height = static_cast<uint16_t>(JSON_GetNumberRequired<int>(document, "height"));
    const uint16_t fontCount = static_cast<uint16_t>(JSON_GetNumberRequired<int>(document, "fontCount"));

    const char* const cpuDataHex = JSON_GetValueRequired<const char*>(document, "cpuDataHex");
    const size_t cpuDataSize = JSON_GetNumberRequired<size_t>(document, "cpuDataSize");

    std::vector<uint8_t> cpuData = DecodeHexString(cpuDataHex, cpuDataSize);

    if (cpuData.size() != cpuDataSize)
        Error("CPU data size mismatch: expected %zu, got %zu\n", cpuDataSize, cpuData.size());

    const uint16_t unk_2 = static_cast<uint16_t>(JSON_GetNumberOrDefault(document, "unk_2", 0));
    const int64_t unk_18Offset = JSON_GetNumberOrDefault(document, "unk_18Offset", static_cast<int64_t>(-1));

    rapidjson::Value::ConstMemberIterator fixupsIt;
    JSON_GetRequired(document, "fontFixups", JSONFieldType_e::kArray, fixupsIt);
    const rapidjson::Value::ConstArray& fixupsArray = fixupsIt->value.GetArray();

    if (fixupsArray.Size() != fontCount)
        Error("Font fixups count mismatch: expected %u, got %u\n", fontCount, fixupsArray.Size());

    PakPageLump_s hdrLump = pak->CreatePageLump(sizeof(UIFontAtlasHeader_v7_t), SF_HEAD | SF_CLIENT, 8);
    UIFontAtlasHeader_v7_t* const pHdr = reinterpret_cast<UIFontAtlasHeader_v7_t*>(hdrLump.data);

    pHdr->fontCount = fontCount;
    pHdr->unk_2 = unk_2;
    pHdr->width = width;
    pHdr->height = height;
    pHdr->widthRatio = 1.0f / static_cast<float>(width);
    pHdr->heightRatio = 1.0f / static_cast<float>(height);
    pHdr->atlasGuid = atlasGuid;

    Pak_RegisterGuidRefAtOffset(atlasGuid, offsetof(UIFontAtlasHeader_v7_t, atlasGuid), hdrLump, asset);

    PakPageLump_s cpuLump = pak->CreatePageLump(cpuDataSize, SF_CPU | SF_CLIENT, 8);
    memcpy(cpuLump.data, cpuData.data(), cpuDataSize);

    pak->AddPointer(hdrLump, offsetof(UIFontAtlasHeader_v7_t, fonts), cpuLump, 0);

    if (unk_18Offset >= 0)
        pak->AddPointer(hdrLump, offsetof(UIFontAtlasHeader_v7_t, unk_18), cpuLump, static_cast<size_t>(unk_18Offset));

    for (rapidjson::SizeType i = 0; i < fixupsArray.Size(); i++)
    {
        const rapidjson::Value& fixup = fixupsArray[i];

        const size_t fontOffset = JSON_GetNumberRequired<size_t>(fixup, "fontOffset");
        const int64_t nameOffset = JSON_GetNumberRequired<int64_t>(fixup, "nameOffset");
        const int64_t unicodeChunksOffset = JSON_GetNumberRequired<int64_t>(fixup, "unicodeChunksOffset");
        const int64_t unicodeChunksIndexOffset = JSON_GetNumberRequired<int64_t>(fixup, "unicodeChunksIndexOffset");
        const int64_t unicodeChunksMaskOffset = JSON_GetNumberRequired<int64_t>(fixup, "unicodeChunksMaskOffset");
        const int64_t proportionsOffset = JSON_GetNumberRequired<int64_t>(fixup, "proportionsOffset");
        const int64_t texturesOffset = JSON_GetNumberRequired<int64_t>(fixup, "texturesOffset");
        const int64_t unk58Offset = JSON_GetNumberRequired<int64_t>(fixup, "unk_58_Offset");

        const size_t nameFieldOffset = fontOffset + offsetof(UIFontHeader_v7_t, name);
        const size_t chunksFieldOffset = fontOffset + offsetof(UIFontHeader_v7_t, unicodeChunks);
        const size_t indexFieldOffset = fontOffset + offsetof(UIFontHeader_v7_t, unicodeChunksIndex);
        const size_t maskFieldOffset = fontOffset + offsetof(UIFontHeader_v7_t, unicodeChunksMask);
        const size_t propsFieldOffset = fontOffset + offsetof(UIFontHeader_v7_t, proportions);
        const size_t texFieldOffset = fontOffset + offsetof(UIFontHeader_v7_t, textures);
        const size_t unk58FieldOffset = fontOffset + offsetof(UIFontHeader_v7_t, unk_58);

        if (nameOffset >= 0)
            pak->AddPointer(cpuLump, nameFieldOffset, cpuLump, static_cast<size_t>(nameOffset));

        if (unicodeChunksOffset >= 0)
            pak->AddPointer(cpuLump, chunksFieldOffset, cpuLump, static_cast<size_t>(unicodeChunksOffset));

        if (unicodeChunksIndexOffset >= 0)
            pak->AddPointer(cpuLump, indexFieldOffset, cpuLump, static_cast<size_t>(unicodeChunksIndexOffset));

        if (unicodeChunksMaskOffset >= 0)
            pak->AddPointer(cpuLump, maskFieldOffset, cpuLump, static_cast<size_t>(unicodeChunksMaskOffset));

        if (proportionsOffset >= 0)
            pak->AddPointer(cpuLump, propsFieldOffset, cpuLump, static_cast<size_t>(proportionsOffset));

        if (texturesOffset >= 0)
            pak->AddPointer(cpuLump, texFieldOffset, cpuLump, static_cast<size_t>(texturesOffset));

        if (unk58Offset >= 0)
            pak->AddPointer(cpuLump, unk58FieldOffset, cpuLump, static_cast<size_t>(unk58Offset));
    }

    // Process generic pointer fixups if present
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

    asset.InitAsset(hdrLump.GetPointer(), sizeof(UIFontAtlasHeader_v7_t), PagePtr_t::NullPtr(), FONT_VERSION, AssetType::FONT);
    asset.SetHeaderPointer(hdrLump.data);

    pak->FinishAsset();
}
