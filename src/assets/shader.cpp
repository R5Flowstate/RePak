#include "pch.h"
#include "assets.h"
#include "public/shader.h"
#include "public/multishader.h"
#include "utils/dxutils.h"
#include "utils/jsonutils.h"

static void Shader_LoadFromMSW(CPakFileBuilder* const pak, const char* const assetPath, CMultiShaderWrapperIO::ShaderCache_t& shaderCache)
{
	const fs::path inputFilePath = pak->GetAssetPath() / fs::path(assetPath).replace_extension("msw");
	MSW_ParseFile(inputFilePath, shaderCache, MultiShaderWrapperFileType_e::SHADER);
}

template <typename ShaderAssetHeader_t>
static void Shader_CreateFromMSW(CPakFileBuilder* const pak, PakPageLump_s& cpuDataChunk, ParsedDXShaderData_t* const firstShaderData,
								const CMultiShaderWrapperIO::Shader_t* shader, PakPageLump_s& hdrChunk, ShaderAssetHeader_t* const hdr)
{
	const size_t numShaderBuffers = shader->entries.size();
	size_t totalShaderDataSize = 0;

	for (auto& it : shader->entries)
	{
		if (it.buffer == nullptr)
			continue;

		// If the shader type hasn't been found yet, parse this buffer and find out what we want to set it as.
		if (hdr->type == eShaderType::Invalid)
		{
			if (DXUtils::GetParsedShaderData(it.buffer, it.size, firstShaderData))
			{
				if (firstShaderData->foundFlags & SHDR_FOUND_RDEF)
				{
					hdr->type = static_cast<eShaderType>(firstShaderData->pakShaderType);
				}
			}
		}

		totalShaderDataSize += IALIGN(it.size, 8);
	}
	assert(totalShaderDataSize != 0);

	const int8_t entrySize = hdr->type == eShaderType::Vertex ? 24 : 16;

	// Size of the data that describes each shader bytecode buffer
	const size_t descriptorSize = numShaderBuffers * entrySize;

	const size_t shaderBufferChunkSize = descriptorSize + totalShaderDataSize;
	cpuDataChunk = pak->CreatePageLump(shaderBufferChunkSize, SF_CPU | SF_TEMP, 8);

	// Offset at which the next bytecode buffer will be written.
	// Initially starts at the end of the descriptors and then gets increased every time a buffer is written.
	size_t nextBytecodeBufferOffset = descriptorSize;

	for (size_t i = 0; i < numShaderBuffers; ++i)
	{
		const CMultiShaderWrapperIO::ShaderEntry_t& entry = shader->entries[i];

		ShaderByteCode_t* bc = reinterpret_cast<ShaderByteCode_t*>(cpuDataChunk.data + (i * entrySize));

		if (entry.buffer)
		{
			assert(entry.size > 0);

			// Register the data pointer at the byte code.
			pak->AddPointer(cpuDataChunk, (i * entrySize) + offsetof(ShaderByteCode_t, data), cpuDataChunk, nextBytecodeBufferOffset);
			bc->dataSize = entry.size;

			if (hdr->type == eShaderType::Vertex)
			{
				pak->AddPointer(cpuDataChunk, (i * entrySize) + offsetof(ShaderByteCode_t, inputSignatureBlob), cpuDataChunk, nextBytecodeBufferOffset);
				bc->inputSignatureBlobSize = bc->dataSize;
			}

			memcpy_s(cpuDataChunk.data + nextBytecodeBufferOffset, entry.size, entry.buffer, entry.size);
			nextBytecodeBufferOffset += IALIGN(entry.size, 8);
		}
		else
		{
			// Null shader
			if (entry.refIndex == UINT16_MAX)
			{
				// technically, these shaders can actually have a pointer to an empty dx bytecode buffer (with no actual bytecode from what i can tell?)
				// but the logic of the function that reads them should mean that the pointer is never accessed if entry.size is <= 0
			}
			else // Shader entry references another shader instead of defining its own buffer
			{
				bc->dataSize = ~entry.refIndex;
			}
		}
	}

	// unk_10 is the HW-shader array: v15 stride 24. Under-reserve overlaps shaderInputFlags
	// and the engine reads a stomped shader pointer as a vertex format. Two QWORDs of input flags follow.
	const size_t hwShaderStride = std::is_same_v<ShaderAssetHeader_t, ShaderAssetHeader_v15_t> ? 24 : 16;
	const size_t inputFlagsDataSize = numShaderBuffers * (2 * sizeof(uint64_t));
	const size_t reservedDataSize = numShaderBuffers * hwShaderStride;
	PakPageLump_s shaderInfoChunk = pak->CreatePageLump(reservedDataSize + inputFlagsDataSize, SF_CPU, 1);

	pak->AddPointer(hdrChunk, offsetof(ShaderAssetHeader_t, unk_10), shaderInfoChunk, 0);
	pak->AddPointer(hdrChunk, offsetof(ShaderAssetHeader_t, shaderInputFlags), shaderInfoChunk, reservedDataSize);

	uint64_t* const inputFlags = reinterpret_cast<uint64_t*>(shaderInfoChunk.data + reservedDataSize);
	size_t i = 0;

	// vertex shaders seem to have data every 8 bytes, unlike (seemingly) every other shader that only uses 8 out of every 16 bytes
	for (auto& it : shader->entries)
	{
		inputFlags[i] = it.flags[0];
		inputFlags[i+1] = it.flags[1];

		i+=2;
	}
}

static void Shader_SetupHeader(ShaderAssetHeader_v8_t* const hdr, const CMultiShaderWrapperIO::Shader_t* const shader)
{
	// Set to invalid so we can update it when a buffer is found, and detect that it has been set.
	hdr->type = eShaderType::Invalid;
	hdr->numShaderBuffers = static_cast<int>(shader->entries.size());
}

static void Shader_SetupHeader(ShaderAssetHeader_v12_t* const hdr, const CMultiShaderWrapperIO::Shader_t* const shader)
{
	// Set to invalid so we can update it when a buffer is found, and detect that it has been set.
	hdr->type = eShaderType::Invalid;
	memcpy_s(hdr->shaderFeatures, sizeof(hdr->shaderFeatures), shader->features, sizeof(shader->features));
}

static void Shader_SetupHeader(ShaderAssetHeader_v15_t* const hdr, const CMultiShaderWrapperIO::Shader_t* const shader)
{
	// Set to invalid so we can update it when a buffer is found, and detect that it has been set.
	hdr->type = eShaderType::Invalid;

	// Copy the 7 MSW feature bytes verbatim into envType + envOptScales.
	// These preserve the real env-permutation descriptors used at draw time.
	static_assert(offsetof(ShaderAssetHeader_v15_t, envOptScales) == offsetof(ShaderAssetHeader_v15_t, envType) + 1);
	memcpy_s(&hdr->envType, sizeof(hdr->envType) + sizeof(hdr->envOptScales), shader->features, sizeof(shader->features));

	// Reconcile the bytecode-buffer count:
	//   envType == MTLENVTYPE_CUSTOM_COUNT_N (1..17) -> count = N (literal)
	//   envType == MTLENVTYPE_MTLENVOPT   (0xFF/-1)  -> count derived from
	//       base = (envOptScales[0]!=0)+1; *2 per nonzero envOptScales[1..3]
	// MSW carries the count (numShaderDescriptors) but NOT the byte at +0x10
	// ("isReference"), so envType/envOptScales are kept as-is and +0x10 is reconstructed.
	const size_t n = shader->entries.size();
	assert(n > 0 && n < 0x7F);
	hdr->isReference = 0;

	if (hdr->envType == 0xFF) // MTLENVTYPE_MTLENVOPT
	{
		// +0x10 is a fourth count-doubler. Write it so the engine derives exactly n buffers;
		// a phantom walk past the array AVs in CreateVertexShader on DXBC bytes.
		size_t base = (hdr->envOptScales[0] != 0) + 1;
		if (hdr->envOptScales[1]) base *= 2;
		if (hdr->envOptScales[2]) base *= 2;
		if (hdr->envOptScales[3]) base *= 2;

		if (n == base)
		{
			hdr->isReference = 0;       // engine count = base = n
		}
		else if (n == base * 2)
		{
			// Engine count = base*2 = n. The VALUE is the debug-permutation index mask:
			// the S21 loader skips creating entries where
			// (isReference & entryIndex) != 0, i.e. the stripped/size-only debug half.
			// Respawn-genuine S21 paks use exactly n/2 (the top index bit); the old
			// constant 4 only masked correctly for 8-entry shaders.
			hdr->isReference = static_cast<uint8_t>(n / 2);
		}
		else
		{
			// Feature bytes inconsistent with the count (e.g. zero/lossy MSW export):
			// fall back to the engine's literal-count form (valid MTLENVTYPE_CUSTOM_COUNT_N).
			Warning("Shader v15: envOptScales derive %zu != count %zu; falling back to literal count.\n", base, n);
			hdr->envType = static_cast<uint8_t>(n);
			memset(hdr->envOptScales, 0, sizeof(hdr->envOptScales));
			hdr->isReference = 0;
		}
	}
	else if (hdr->envType != n)
	{
		// envType present but inconsistent (incl. envType==0 from a lossy MSW export):
		// force the literal-count form so the engine parses the right buffer count.
		Warning("Shader v15: envType %u != count %zu; forcing literal count.\n", hdr->envType, n);
		hdr->envType = static_cast<uint8_t>(n);
		memset(hdr->envOptScales, 0, sizeof(hdr->envOptScales));
	}

	// Defensive post-condition: re-derive the engine's buffer count from the FINAL header
	// fields and require it to equal n. A mismatch is the over-/under-iteration crash
	// waiting to happen -- fail loud at pack time instead of at shader create.
	{
		size_t engineCount;
		if (hdr->envType == 0xFF)
		{
			engineCount = (hdr->envOptScales[0] != 0) + 1;
			if (hdr->envOptScales[1]) engineCount *= 2;
			if (hdr->envOptScales[2]) engineCount *= 2;
			if (hdr->envOptScales[3]) engineCount *= 2;
			if (hdr->isReference)      engineCount *= 2;
		}
		else
		{
			engineCount = hdr->envType;
		}
		if (engineCount != n)
			Error("Shader v15: packed buffer-count %zu != actual %zu (envType=0x%02X isRef=%u); engine would over-/under-iterate.\n",
				engineCount, n, hdr->envType, hdr->isReference);
	}

	// Trailing fields: +0x28 = 0, +0x30 = 0xFFFFFFFF sentinel (not a pointer).
	hdr->unk_28 = 0;
	hdr->costInfo = 0xFFFFFFFFull;
}

template<typename ShaderAssetHeader_t>
static void Shader_InternalAddShader(CPakFileBuilder* const pak, const char* const assetPath, const CMultiShaderWrapperIO::Shader_t* const shader, 
									const PakGuid_t shaderGuid, const int assetVersion)
{
	PakAsset_t& asset = pak->BeginAsset(shaderGuid, assetPath);
	PakPageLump_s hdrChunk = pak->CreatePageLump(sizeof(ShaderAssetHeader_t), SF_HEAD, 8);

	ShaderAssetHeader_t* const hdr = reinterpret_cast<ShaderAssetHeader_t*>(hdrChunk.data);
	Shader_SetupHeader(hdr, shader);

	if (pak->IsFlagSet(PF_KEEP_DEV))
	{
		const char* const targetName = shader->name.length() > 0 ? shader->name.c_str() : assetPath;

		char pathStem[PAK_MAX_STEM_PATH];
		const size_t stemLen = Pak_ExtractAssetStem(targetName, pathStem, sizeof(pathStem), "shader");

		if (stemLen > 0)
		{
			PakPageLump_s nameChunk = pak->CreatePageLump(stemLen + 1, SF_CPU | SF_DEV, 1);
			memcpy(nameChunk.data, pathStem, stemLen + 1);

			pak->AddPointer(hdrChunk, offsetof(ShaderAssetHeader_t, name), nameChunk, 0);
		}
	}

	ParsedDXShaderData_t* const shaderData = new ParsedDXShaderData_t;
	PakPageLump_s dataChunk;

	Shader_CreateFromMSW(pak, dataChunk, shaderData, shader, hdrChunk, hdr);
	// =======================================

	asset.InitAsset(
		hdrChunk.GetPointer(), sizeof(ShaderAssetHeader_t),
		dataChunk.GetPointer(), assetVersion, AssetType::SHDR);

	asset.SetHeaderPointer(hdrChunk.data);
	asset.SetPublicData(shaderData);

	pak->FinishAsset();
}

static void Shader_AddShaderV8(CPakFileBuilder* const pak, const char* const assetPath, const CMultiShaderWrapperIO::Shader_t* const shader, const PakGuid_t shaderGuid)
{
	Shader_InternalAddShader<ShaderAssetHeader_v8_t>(pak, assetPath, shader, shaderGuid, 8);
}

static void Shader_AddShaderV12(CPakFileBuilder* const pak, const char* const assetPath, const CMultiShaderWrapperIO::Shader_t* const shader, const PakGuid_t shaderGuid)
{
	Shader_InternalAddShader<ShaderAssetHeader_v12_t>(pak, assetPath, shader, shaderGuid, 12);
}

static void Shader_AddShaderV15(CPakFileBuilder* const pak, const char* const assetPath, const CMultiShaderWrapperIO::Shader_t* const shader, const PakGuid_t shaderGuid)
{
	Shader_InternalAddShader<ShaderAssetHeader_v15_t>(pak, assetPath, shader, shaderGuid, 15);
}

// Child/reference shader (v15): no cpu bytecode of its own. It carries the PARENT
// shader's guid in the unk_10 slot (+0x18), which the engine resolves at load via
// FindAssetByGUID to share the parent's bytecode. Verified against the shipping district
// children (type=SHADER_TYPE_INVALID(9), shaderIterationMode=1, no relocations,
// costInfo=0xFFFFFFFF sentinel). Not expressible via MSW, so packed from the manifest.
static void Shader_AddChildShaderV15(CPakFileBuilder* const pak, const char* const assetPath, const PakGuid_t shaderGuid, const PakGuid_t parentGuid)
{
	PakAsset_t& asset = pak->BeginAsset(shaderGuid, assetPath);
	PakPageLump_s hdrChunk = pak->CreatePageLump(sizeof(ShaderAssetHeader_v15_t), SF_HEAD, 8);

	ShaderAssetHeader_v15_t* const hdr = reinterpret_cast<ShaderAssetHeader_v15_t*>(hdrChunk.data);

	hdr->type = static_cast<eShaderType>(9); // SHADER_TYPE_INVALID
	hdr->shaderIterationMode = 1;
	hdr->costInfo = 0xFFFFFFFFull;

	// write the raw parent guid into the unk_10 slot, then register it as a guid
	// dependency (raw guid ref, not a page pointer -- matches shaderset VS/PS refs).
	memcpy(&hdr->unk_10, &parentGuid, sizeof(PakGuid_t));
	Pak_RegisterGuidRefAtOffset(parentGuid, offsetof(ShaderAssetHeader_v15_t, unk_10), hdrChunk, asset);

	asset.InitAsset(
		hdrChunk.GetPointer(), sizeof(ShaderAssetHeader_v15_t),
		PagePtr_t::NullPtr(), 15, AssetType::SHDR);
	asset.SetHeaderPointer(hdrChunk.data);

	pak->FinishAsset();
}

bool Shader_AutoAddShader(CPakFileBuilder* const pak, const char* const assetPath, const CMultiShaderWrapperIO::Shader_t* const shader, const PakGuid_t shaderGuid, const int shaderAssetVersion)
{
	PakAsset_t* const existingAsset = pak->GetAssetByGuid(shaderGuid, nullptr, true);

	if (existingAsset)
		return false;

	Debug("Auto-adding 'shdr' asset \"%s\".\n", assetPath);

	switch (shaderAssetVersion)
	{
	case 8:  Shader_AddShaderV8(pak, assetPath, shader, shaderGuid); break;
	case 15: Shader_AddShaderV15(pak, assetPath, shader, shaderGuid); break;
	default: Shader_AddShaderV12(pak, assetPath, shader, shaderGuid); break;
	}

	return true;
}

void Assets::AddShaderAsset_v8(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry)
{
	UNUSED(mapEntry);
	CMultiShaderWrapperIO::ShaderCache_t cache = {};

	Shader_LoadFromMSW(pak, assetPath, cache);
	Shader_AddShaderV8(pak, assetPath, cache.shader, assetGuid);
}

void Assets::AddShaderAsset_v12(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry)
{
	UNUSED(mapEntry);
	CMultiShaderWrapperIO::ShaderCache_t cache = {};

	Shader_LoadFromMSW(pak, assetPath, cache);
	Shader_AddShaderV12(pak, assetPath, cache.shader, assetGuid);
}

void Assets::AddShaderAsset_v15(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry)
{
	// Child/reference shaders have no bytecode (no MSW) -- the manifest supplies a
	// "$parentShader" guid instead, and we emit a header-only child shader.
	const PakGuid_t parentShader = JSON_GetValueOrDefault(mapEntry, "$parentShader", static_cast<PakGuid_t>(0));
	if (parentShader != 0)
	{
		Shader_AddChildShaderV15(pak, assetPath, assetGuid, parentShader);
		return;
	}

	CMultiShaderWrapperIO::ShaderCache_t cache = {};

	Shader_LoadFromMSW(pak, assetPath, cache);
	Shader_AddShaderV15(pak, assetPath, cache.shader, assetGuid);
}
