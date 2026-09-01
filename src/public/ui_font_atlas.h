#pragma once
#include "public/rpak.h"

#pragma pack(push, 1)

// Proportion entry - controls character scaling
struct UIFontProportion_v7_t
{
    float scaleBounds;
    float scaleSize;
};
static_assert(sizeof(UIFontProportion_v7_t) == 8);

// Texture/glyph entry - UV and position data for a character
struct UIFontTexture_v7_t
{
    float unk_0;
    uint16_t unk_4;
    uint8_t unk_6;
    uint8_t proportionIndex;
    float posBaseX;
    float posBaseY;
    float posMinX;
    float posMinY;
    float posMaxX;
    float posMaxY;
};
static_assert(sizeof(UIFontTexture_v7_t) == 32);

// Font header - defines a single font within the atlas
struct UIFontHeader_v7_t
{
    PagePtr_t name;              // 0x00
    uint16_t fontIndex;          // 0x08
    uint16_t numProportions;     // 0x0A
    uint16_t numGlyphChunks;     // 0x0C
    uint16_t numUnicodeChunks;   // 0x0E
    int32_t glyphIndex;          // 0x10
    int32_t unicodeIndex;        // 0x14
    uint32_t numTextures;        // 0x18
    float proportionScaleX;      // 0x1C
    float proportionScaleY;      // 0x20
    float unk_24[2];             // 0x24
    uint32_t textureIndex;       // 0x2C
    PagePtr_t unicodeChunks;     // 0x30
    PagePtr_t unicodeChunksIndex;// 0x38
    PagePtr_t unicodeChunksMask; // 0x40
    PagePtr_t proportions;       // 0x48
    PagePtr_t textures;          // 0x50
    PagePtr_t unk_58;            // 0x58
};
static_assert(sizeof(UIFontHeader_v7_t) == 0x60);

// Asset header for font atlas
struct UIFontAtlasHeader_v7_t
{
    uint16_t fontCount;      // 0x00
    uint16_t unk_2;          // 0x02
    uint16_t width;          // 0x04
    uint16_t height;         // 0x06
    float widthRatio;        // 0x08
    float heightRatio;       // 0x0C
    PagePtr_t fonts;         // 0x10
    PagePtr_t unk_18;        // 0x18
    PakGuid_t atlasGuid;     // 0x20
};
static_assert(sizeof(UIFontAtlasHeader_v7_t) == 0x28);

#pragma pack(pop)
