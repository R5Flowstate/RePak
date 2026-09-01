#pragma once

enum class eShaderType : uint8_t
{
    Pixel,
    Vertex,
    Geometry,
    Hull,
    Domain,
    Compute,
    Invalid = 0xFF,
};

static const char* s_dxShaderTypeNames[] = {
    "PIXEL",
    "VERTEX",
    "GEOMETRY",
    "HULL",
    "DOMAIN",
    "COMPUTE",
};

static const char* s_dxShaderTypeShortNames[] = {
    "ps",
    "vs",
    "gs",
    "hs",
    "ds",
    "cs",
};

FORCEINLINE eShaderType GetShaderTypeByName(const std::string& name)
{
    for (int i = 0; i < ARRAYSIZE(s_dxShaderTypeNames); ++i)
    {
        if (!_stricmp(s_dxShaderTypeNames[i], name.c_str()))
            return static_cast<eShaderType>(i);
    }

    return eShaderType::Invalid;
}

struct ShaderAssetHeader_v8_t
{
    PagePtr_t name; // const char*
    eShaderType type;

    char unk_9[3];
    int numShaderBuffers; // some count of sorts

    PagePtr_t unk_10; // void*
    PagePtr_t shaderInputFlags; // int64*
};

struct ShaderAssetHeader_v12_t
{
    PagePtr_t name; // const char*
    eShaderType type;

    char shaderFeatures[7];

    PagePtr_t unk_10; // void*
    PagePtr_t shaderInputFlags; // int64*
};

// S21 shader header (56 bytes). Same CPU/bytecode layout as v12; the reserved
// working-data pointer moved to +0x18 and the linkage/input-flags pointer to
// +0x20, plus a trailing costInfo slot. subheaderSize=56; only +0x18 and +0x20
// are relocated pointers; +0x28=0; +0x30=0xFFFFFFFF sentinel;
// delta(reservedData -> linkage) == numShaderBuffers*24.
struct ShaderAssetHeader_v15_t
{
    PagePtr_t name; // const char*

    eShaderType type; // shaderType

    // envType: MTLENVTYPE_CUSTOM_COUNT_N (1..17) == literal shader-buffer count;
    // MTLENVTYPE_MTLENVOPT (0xFF/-1) == derive the count from envOptScales[0..3]
    // + isReference. These also drive draw-time permutation selection, so the
    // writer preserves them verbatim from the MSW.
    uint8_t envType;
    uint8_t envOptScales[6];
    uint8_t isReference;          // doubles the derived count when nonzero (MSW drops this byte)
    uint8_t shaderIterationMode;
    uint8_t numCustomDefines;
    uint8_t useCombinatoricDefines;
    uint8_t _padding[4];

    PagePtr_t unk_10;             // +0x18 reserved working-data ptr
    PagePtr_t shaderInputFlags;   // +0x20 linkage (fmtIn, fmtOut)

    uint64_t unk_28;
    uint64_t costInfo;            // +0x30 cost-info ptr (data pak = 0xFFFFFFFF sentinel)
};
static_assert(sizeof(ShaderAssetHeader_v15_t) == 56);
static_assert(offsetof(ShaderAssetHeader_v15_t, envType) == 0x09);
static_assert(offsetof(ShaderAssetHeader_v15_t, isReference) == 0x10);
static_assert(offsetof(ShaderAssetHeader_v15_t, unk_10) == 0x18);
static_assert(offsetof(ShaderAssetHeader_v15_t, shaderInputFlags) == 0x20);

struct ShaderByteCode_t
{
    PagePtr_t data;
    uint32_t dataSize;

    uint32_t inputSignatureBlobSize;

    // only exists in vertex shaders. shader writing code handles this by forcing the size to be 16 bytes (instead of 24)
    // on non-vertex shaders
    PagePtr_t inputSignatureBlob;
};

static_assert(sizeof(ShaderByteCode_t) == 0x18);