#include "pch.h"
#include "oodle.h"
#include "utils/logger.h"
#include <windows.h>

// Oodle is proprietary middleware and is not vendored. Every entry point is
// resolved at runtime from an oo2core DLL: OODLE_DLL if set, otherwise the
// first of the names below that loads.
namespace
{
	typedef intptr_t OO_SINTa;

	constexpr int kCompressorKraken = 8;
	constexpr int kSeekChunkLen = 1 << 18; // minimum independent chunk the format allows

	// OodleLZ_CompressOptions is copied wholesale from the encoder's own
	// defaults and treated as opaque; only these two fields are written.
	constexpr size_t kCompressOptionsSize = 0x58;
	constexpr size_t kOptSeekChunkReset = 0x08;
	constexpr size_t kOptSeekChunkLen = 0x0C;

	constexpr size_t kConfigValuesSize = 0x1C;
	constexpr size_t kCfgBackwardCompatMajor = 0x14;

	typedef OO_SINTa(*pfnCompress)(int, const void*, OO_SINTa, void*, int, const void*, const void*, const void*, void*, OO_SINTa);
	typedef OO_SINTa(*pfnDecompress)(const void*, OO_SINTa, void*, OO_SINTa, int, int, int, void*, OO_SINTa, void*, void*, void*, OO_SINTa, int);
	typedef OO_SINTa(*pfnBufferSizeNeeded)(int, OO_SINTa);
	typedef const void* (*pfnOptionsGetDefault)(int, int);
	typedef void (*pfnOptionsValidate)(void*);
	typedef void (*pfnGetConfigValues)(void*);
	typedef void (*pfnSetConfigValues)(const void*);

	struct OodleApi
	{
		pfnCompress compress = nullptr;
		pfnDecompress decompress = nullptr;
		pfnBufferSizeNeeded bufferSizeNeeded = nullptr;
		pfnOptionsGetDefault optionsGetDefault = nullptr;
		pfnOptionsValidate optionsValidate = nullptr;
		pfnGetConfigValues getConfigValues = nullptr;
		pfnSetConfigValues setConfigValues = nullptr;
		bool loaded = false;
	};

	const char* const s_dllNames[] = {
		"oo2core_9_win64.dll",
		"oo2core_8_win64.dll",
		"oo2core_7_win64.dll",
		"oo2core_6_win64.dll",
		"oo2core_5_win64.dll",
	};

	const OodleApi& GetApi()
	{
		static OodleApi api;
		static bool tried = false;

		if (tried)
			return api;

		tried = true;

		HMODULE hMod = nullptr;
		const char* const envName = getenv("OODLE_DLL");

		if (envName && *envName)
		{
			hMod = LoadLibraryA(envName);

			if (!hMod)
				Warning("OODLE_DLL is set to \"%s\" but it could not be loaded.\n", envName);
		}

		for (size_t i = 0; !hMod && i < ARRAYSIZE(s_dllNames); i++)
			hMod = LoadLibraryA(s_dllNames[i]);

		if (!hMod)
		{
			Warning("No oo2core DLL found; Oodle compression and decompression are unavailable. "
				"Set OODLE_DLL to one, or place it next to repak.exe.\n");
			return api;
		}

		api.compress = reinterpret_cast<pfnCompress>(GetProcAddress(hMod, "OodleLZ_Compress"));
		api.decompress = reinterpret_cast<pfnDecompress>(GetProcAddress(hMod, "OodleLZ_Decompress"));
		api.bufferSizeNeeded = reinterpret_cast<pfnBufferSizeNeeded>(GetProcAddress(hMod, "OodleLZ_GetCompressedBufferSizeNeeded"));
		api.optionsGetDefault = reinterpret_cast<pfnOptionsGetDefault>(GetProcAddress(hMod, "OodleLZ_CompressOptions_GetDefault"));
		api.optionsValidate = reinterpret_cast<pfnOptionsValidate>(GetProcAddress(hMod, "OodleLZ_CompressOptions_Validate"));
		api.getConfigValues = reinterpret_cast<pfnGetConfigValues>(GetProcAddress(hMod, "Oodle_GetConfigValues"));
		api.setConfigValues = reinterpret_cast<pfnSetConfigValues>(GetProcAddress(hMod, "Oodle_SetConfigValues"));

		if (!api.compress || !api.decompress || !api.bufferSizeNeeded || !api.optionsGetDefault)
		{
			Warning("An oo2core DLL loaded but is missing required exports; Oodle is unavailable.\n");
			return api;
		}

		api.loaded = true;
		return api;
	}
}

size_t Oodle::GetCompressedBufferSizeNeeded(const size_t rawLen)
{
	const OodleApi& api = GetApi();

	if (!api.loaded)
		return 0;

	return static_cast<size_t>(api.bufferSizeNeeded(kCompressorKraken, static_cast<OO_SINTa>(rawLen)));
}

size_t Oodle::Compress(const uint8_t* const src, const size_t rawLen, uint8_t* const dst, const int level, const int backwardCompatMajor)
{
	const OodleApi& api = GetApi();

	if (!api.loaded)
		return 0;

	if (backwardCompatMajor > 0)
	{
		if (api.getConfigValues && api.setConfigValues)
		{
			uint8_t cfg[kConfigValuesSize];
			api.getConfigValues(cfg);
			*reinterpret_cast<int32_t*>(cfg + kCfgBackwardCompatMajor) = backwardCompatMajor;
			api.setConfigValues(cfg);
		}
		else
			Warning("This oo2core DLL does not export the config functions; ignoring the backward-compat major version.\n");
	}

	int compressor = kCompressorKraken;
	const char* const compEnv = getenv("OODLE_COMPRESSOR");

	if (compEnv && *compEnv)
		compressor = atoi(compEnv);

	uint8_t opts[kCompressOptionsSize];
	memcpy(opts, api.optionsGetDefault(compressor, level), sizeof(opts));

	// Independent seek chunks: the runtime decodes a region chunk-by-chunk, and
	// a single dependent stream walks its decoder off the end of the region.
	*reinterpret_cast<int32_t*>(opts + kOptSeekChunkReset) = 1;
	*reinterpret_cast<int32_t*>(opts + kOptSeekChunkLen) = kSeekChunkLen;

	if (api.optionsValidate)
		api.optionsValidate(opts);

	const OO_SINTa result = api.compress(
		compressor,
		src, static_cast<OO_SINTa>(rawLen),
		dst,
		level,
		opts, nullptr, nullptr, nullptr, 0);

	if (result <= 0)
		return 0;

	return static_cast<size_t>(result);
}

bool Oodle::Decompress(const uint8_t* const src, const size_t compLen, uint8_t* const dst, const size_t rawLen)
{
	const OodleApi& api = GetApi();

	if (!api.loaded)
		return false;

	const OO_SINTa result = api.decompress(
		src, static_cast<OO_SINTa>(compLen),
		dst, static_cast<OO_SINTa>(rawLen),
		0,  // OodleLZ_FuzzSafe_No
		0,  // OodleLZ_CheckCRC_No
		0,  // OodleLZ_Verbosity_None
		nullptr, 0, nullptr, nullptr, nullptr, 0,
		3); // OodleLZ_Decode_Unthreaded

	return static_cast<size_t>(result) == rawLen;
}
