//=============================================================================//
//
// Pak file builder and management class
//
//=============================================================================//

#include "pch.h"
#include "pakfile.h"
#include "assets/assets.h"
#include "utils/zstdutils.h"
#include "utils/oodle.h"

CPakFileBuilder::CPakFileBuilder(const CBuildSettings* const buildSettings, CStreamFileBuilder* const streamBuilder)
{
	m_buildSettings = buildSettings;
	m_streamBuilder = streamBuilder;
}

static std::unordered_set<PakAssetHandler_s, PakAssetHasher_s> s_pakAssetHandlers
{
	{"anir", PakAssetScope_e::kServerOnly, Assets::AddAnimRecording_v1, Assets::AddAnimRecording_v1},
	{"txtr", PakAssetScope_e::kClientOnly, Assets::AddTextureAsset_v8, Assets::AddTextureAsset_v10},
	{"txan", PakAssetScope_e::kClientOnly, nullptr, Assets::AddTextureAnimAsset_v1},
	{"uimg", PakAssetScope_e::kClientOnly, Assets::AddUIImageAsset_v10, Assets::AddUIImageAsset_v10},
	{"uiia", PakAssetScope_e::kClientOnly, nullptr, Assets::AddUIImageAtlasAsset_v2},
	{"rlcd", PakAssetScope_e::kClientOnly, Assets::AddLcdScreenEffect_v0, Assets::AddLcdScreenEffect_v0},
	{"matl", PakAssetScope_e::kClientOnly, Assets::AddMaterialAsset_v12, Assets::AddMaterialAsset_v23},
	{"mt4a", PakAssetScope_e::kClientOnly, nullptr, Assets::AddMaterialForAspectAsset_v3},
	{"shdr", PakAssetScope_e::kClientOnly, Assets::AddShaderAsset_v8, Assets::AddShaderAsset_v15},
	{"shds", PakAssetScope_e::kClientOnly, Assets::AddShaderSetAsset_v8, Assets::AddShaderSetAsset_v12},
	{"dtbl", PakAssetScope_e::kAll, Assets::AddDataTableAsset, Assets::AddDataTableAsset},
	{"stlt", PakAssetScope_e::kAll, nullptr, Assets::AddSettingsLayout_v0},
	{"stgs", PakAssetScope_e::kAll, nullptr, Assets::AddSettingsAsset_v1},
	{"mdl_", PakAssetScope_e::kAll, nullptr, Assets::AddModelAsset_v17, Assets::AddModelAsset_v9},
	{"aseq", PakAssetScope_e::kAll, nullptr, Assets::AddAnimSeqAsset_v11, Assets::AddAnimSeqAsset_v7},
	{"arig", PakAssetScope_e::kAll, nullptr, Assets::AddAnimRigAsset_v6, Assets::AddAnimRigAsset_v4},
	{"txls", PakAssetScope_e::kAll, nullptr, Assets::AddTextureListAsset_v1},
	{"txtx", PakAssetScope_e::kAll, nullptr, Assets::AddTextureExtraAsset_v2},
	{"rson", PakAssetScope_e::kAll, nullptr, Assets::AddRSONAsset_v1},
	{"Ptch", PakAssetScope_e::kAll, Assets::AddPatchAsset, Assets::AddPatchAsset},
	{"ui", PakAssetScope_e::kClientOnly, Assets::AddRuiAsset_v30, Assets::AddRuiAsset_v30},
	{"font", PakAssetScope_e::kClientOnly, Assets::AddFontAtlasAsset_v7, Assets::AddFontAtlasAsset_v7},
	{"wrap", PakAssetScope_e::kAll, nullptr, Assets::AddWrapAsset_v7},
	{"rmap", PakAssetScope_e::kAll, nullptr, Assets::AddMapAsset_v4},
	{"efct", PakAssetScope_e::kClientOnly, nullptr, Assets::AddEffectAsset_v16}
};

void CPakFileBuilder::AddJSONAsset(const PakAssetHandler_s& assetHandler, const char* const assetPath, const rapidjson::Value& file)
{
	switch (assetHandler.assetScope)
	{
	case PakAssetScope_e::kServerOnly:
		if (!IsFlagSet(PF_KEEP_SERVER))
			return;
		break;
	case PakAssetScope_e::kClientOnly:
		if (!IsFlagSet(PF_KEEP_CLIENT))
			return;
		break;
	}

	PakAssetAddFunc_t targetFunc = nullptr;
	const uint16_t fileVersion = this->m_Header.fileVersion;

	switch (fileVersion)
	{
	case 7:
	{
		targetFunc = assetHandler.func_r2;
		break;
	}
	case 8:
	{
		// S3 dedicated-server build: prefer the dedi (S3-version) writer when one
		// is registered for this type, otherwise fall back to the S21-native writer.
		targetFunc = (IsFlagSet(PF_DEDI) && assetHandler.func_r5_dedi)
			? assetHandler.func_r5_dedi
			: assetHandler.func_r5;
		break;
	}
	}

	if (targetFunc)
	{
		Debug("Adding '%s' asset \"%s\".\n", assetHandler.assetType, assetPath);

		const steady_clock::time_point start = high_resolution_clock::now();
		const PakGuid_t assetGuid = Pak_GetGuidOverridable(file, assetPath);

		targetFunc(this, assetGuid, assetPath, file);
		const steady_clock::time_point stop = high_resolution_clock::now();

		const microseconds duration = duration_cast<microseconds>(stop - start);
		Debug("...done; took %lld ms.\n", duration.count());
	}
	else
		Error("Asset type '%.4s' is not supported on pak version %hu.\n", assetHandler.assetType, fileVersion);
}

//-----------------------------------------------------------------------------
// purpose: installs asset types and their callbacks
//-----------------------------------------------------------------------------
void CPakFileBuilder::AddAsset(const rapidjson::Value& file)
{
	// Get shared properties "_type" and "_path"
	const char* const assetType = JSON_GetValueOrDefault(file, "_type", static_cast<const char*>(nullptr));
	const char* const assetPath = JSON_GetValueOrDefault(file, "_path", static_cast<const char*>(nullptr));

	if (!assetType)
		Error("No type provided for asset \"%s\".\n", assetPath ? assetPath : "(unknown)");

	if (!assetPath)
		Error("No path provided for an asset of type '%.4s'.\n", assetType);

	g_currentAsset = assetPath;

	const auto it = s_pakAssetHandlers.find({ assetType });

	if (it == s_pakAssetHandlers.end())
		Error("Asset '%s' uses unknown asset type '%.4s'.\n", assetPath, assetType);
	else
		AddJSONAsset(*it, assetPath, file);

	g_currentAsset = nullptr;
}

//-----------------------------------------------------------------------------
// purpose: adds page pointer to the pak file
//-----------------------------------------------------------------------------
void CPakFileBuilder::AddPointer(PakPageLump_s& pointerLump, const size_t pointerOffset,
	const PakPageLump_s& dataLump, const size_t dataOffset)
{
	m_pagePointers.push_back(pointerLump.GetPointer(pointerOffset));

	// Set the pointer field in the struct to the page index and page offset.
	char* const pointerField = &pointerLump.data[pointerOffset];
	*reinterpret_cast<PagePtr_t*>(pointerField) = dataLump.GetPointer(dataOffset);
}

void CPakFileBuilder::AddPointer(PakPageLump_s& pointerLump, const size_t pointerOffset)
{
	m_pagePointers.push_back(pointerLump.GetPointer(pointerOffset));
}

//-----------------------------------------------------------------------------
// purpose: adds new starpak file path to be used by the rpak
//-----------------------------------------------------------------------------
int64_t CPakFileBuilder::AddStreamingFileReference(const char* const path, const bool mandatory)
{
	std::vector<std::string>& vec = mandatory ? m_mandatoryStreamFilePaths : m_optionalStreamFilePaths;
	const int64_t count = static_cast<int64_t>(vec.size());

	for (int64_t index = 0; index < count; index++)
	{
		const std::string& it = vec[index];

		if (it.compare(path) == 0)
			return index;
	}

	// Check if we don't overflow the maximum the runtime supports per set.
	// mandatory and optional are separate sets.
	const size_t newSize = vec.size() + 1;
	const size_t maxSize = GetMaxStreamingFileHandlesPerSet();

	if (newSize > maxSize)
	{
		const char* const streamSetName = Pak_StreamSetToName(mandatory ? PakStreamSet_e::STREAMING_SET_MANDATORY : PakStreamSet_e::STREAMING_SET_OPTIONAL);

		Error("Out of room while adding %s streaming file \"%s\"; runtime has a limit of %zu, got %zu.\n",
			streamSetName, path, maxSize, newSize);
	}

	vec.emplace_back(path);
	return count;
}

//-----------------------------------------------------------------------------
// purpose: adds new starpak data entry
//-----------------------------------------------------------------------------
PakStreamSetEntry_s CPakFileBuilder::AddStreamingDataEntry(const int64_t size, const uint8_t* const data, const PakStreamSet_e set)
{
	const size_t pageAligned = IALIGN(size, STARPAK_DATABLOCK_ALIGNMENT);
	const size_t windowRemainder = pageAligned - size;

	if (windowRemainder > 0)
	{
		// Code bug, data must always be provided with size aligned to
		// STARPAK_DATABLOCK_ALIGNMENT as otherwise the de-duplication
		// code will not work. starpak data is always page aligned. We
		// will still continue to store the data as it will get padded
		// out in the stream file builder within the file itself.
		Warning("%s: didn't receive enough window for page alignment! [%zu < %zu].\n",
			__FUNCTION__, size, pageAligned);
		assert(0);
	}

	StreamAddEntryResults_s results;
	m_streamBuilder->AddStreamingDataEntry(size, data, set, results);

	PakStreamSetEntry_s block;

	block.streamOffset = results.dataOffset;
	block.streamIndex = AddStreamingFileReference(results.streamFile, set == STREAMING_SET_MANDATORY);

	return block;
}

void CPakFileBuilder::SetVersion(const uint16_t version)
{
	if (!Pak_IsVersionSupported(version))
		Error("Unsupported pak file version %hu.\n", version);

	m_Header.fileVersion = version;
}

//-----------------------------------------------------------------------------
// purpose: writes header to file stream
//-----------------------------------------------------------------------------
void CPakFileBuilder::WriteHeader(BinaryIO& io)
{
	m_Header.memSlabCount = m_pageBuilder.GetSlabCount();
	m_Header.memPageCount = m_pageBuilder.GetPageCount();

	assert(m_pagePointers.size() <= UINT32_MAX);
	m_Header.pointerCount = static_cast<uint32_t>(m_pagePointers.size());

	const uint16_t version = m_Header.fileVersion;

	io.Write(m_Header.magic);
	io.Write(m_Header.fileVersion);
	io.Write(m_Header.flags);
	io.Write(m_Header.fileTime);
	io.Write(m_Header.unk0);
	io.Write(m_Header.compressedSize);

	if (version == 8)
		io.Write(m_Header.embeddedStarpakOffset);

	io.Write(m_Header.unk1);
	io.Write(m_Header.decompressedSize);

	if (version == 8)
		io.Write(m_Header.embeddedStarpakSize);

	io.Write(m_Header.unk2);
	io.Write(m_Header.starpakPathsSize);

	if (version == 8)
		io.Write(m_Header.optStarpakPathsSize);

	io.Write(m_Header.memSlabCount);
	io.Write(m_Header.memPageCount);
	io.Write(m_Header.patchIndex);

	if (version == 8)
		io.Write(m_Header.alignment);

	io.Write(m_Header.pointerCount);
	io.Write(m_Header.assetCount);
	io.Write(m_Header.usesCount);
	io.Write(m_Header.dependentsCount);

	if (version == 7)
	{
		io.Write(m_Header.unk7count);
		io.Write(m_Header.unk8count);
	}
	else if (version == 8)
		io.Write(m_Header.unk3);
}

//-----------------------------------------------------------------------------
// purpose: writes assets to file stream
//-----------------------------------------------------------------------------
void CPakFileBuilder::WriteAssetDescriptors(BinaryIO& io)
{
	for (PakAsset_t& it : m_assets)
	{
		io.Write(it.guid);
		io.Write(it.unk0);
		io.Write(it.headPtr.index);
		io.Write(it.headPtr.offset);
		io.Write(it.cpuPtr.index);
		io.Write(it.cpuPtr.offset);
		io.Write(it.GetPackedStreamOffset());

		if (this->m_Header.fileVersion == 8)
			io.Write(it.GetPackedOptStreamOffset());

		assert(it.pageEnd <= UINT16_MAX);
		uint16_t pageEnd = static_cast<uint16_t>(it.pageEnd);
		io.Write(pageEnd);

		io.Write(it.internalDependencyCount);
		io.Write(it.dependentsIndex);
		io.Write(it.usesIndex);
		io.Write(it.dependentsCount);
		io.Write(it.usesCount);
		io.Write(it.headDataSize);
		io.Write(it.version);
		io.Write(it.id);

		it.SetPublicData<void*>(nullptr);
	}

	assert(m_assets.size() <= UINT32_MAX);
	// update header asset count with the assets we've just written
	this->m_Header.assetCount = static_cast<uint32_t>(m_assets.size());
}

//-----------------------------------------------------------------------------
// purpose: writes starpak paths to file stream
// returns: total length of written path vector
//-----------------------------------------------------------------------------
size_t CPakFileBuilder::WriteStarpakPaths(BinaryIO& out, const PakStreamSet_e set)
{
	const auto& vecPaths = set == STREAMING_SET_MANDATORY ? m_mandatoryStreamFilePaths : m_optionalStreamFilePaths;
	return Utils::WriteStringVector(out, vecPaths);
}

//-----------------------------------------------------------------------------
// purpose: writes pak descriptors to file stream
//-----------------------------------------------------------------------------
void CPakFileBuilder::WritePagePointers(BinaryIO& out)
{
	// pointers must be written in order otherwise the runtime crashes as the
	// decoding depends on their order.
	std::sort(m_pagePointers.begin(), m_pagePointers.end());

	for (const PagePtr_t& ptr : m_pagePointers)
		out.Write(ptr);
}

void CPakFileBuilder::WriteAssetUses(BinaryIO& out)
{
	for (const PakAsset_t& it : m_assets)
	{
		for (const PakGuidRef_s& ref : it._uses)
			out.Write(ref.ptr);
	}
}

void CPakFileBuilder::WriteAssetDependents(BinaryIO& out)
{
	for (const PakAsset_t& it : m_assets)
	{
		for (const unsigned int dependent : it._dependents)
			out.Write(dependent);
	}
}

//-----------------------------------------------------------------------------
// purpose: counts the number of internal dependencies for each asset and sets
// them dependent from another. internal dependencies reside in the same pak!
//-----------------------------------------------------------------------------
void CPakFileBuilder::GenerateInternalDependencies()
{
	for (size_t i = 0; i < m_assets.size(); i++)
	{
		PakAsset_t& it = m_assets[i];
		std::set<PakGuid_t> processed;

		for (const PakGuidRef_s& ref : it._uses)
		{
			// an asset can use a dependency more than once, but we should only
			// increment the dependency counter once per unique dependency!
			if (!processed.insert(ref.guid).second)
				continue;

			PakAsset_t* const dependency = GetAssetByGuid(ref.guid, nullptr, true);

			if (dependency)
			{
				dependency->AddDependent(i);

				// Check for overflow - internalDependencyCount is a short (max 32767)
				if (it.internalDependencyCount >= SHRT_MAX)
					Error("Asset \"%s\" has too many internal dependencies (max %hi).\n", it.name.c_str(), SHRT_MAX);
				it.internalDependencyCount++;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// purpose: 
//-----------------------------------------------------------------------------
void CPakFileBuilder::GenerateAssetUses()
{
	size_t totalUsesCount = 0;

	for (PakAsset_t& it : m_assets)
	{
		const size_t numUses = it._uses.size();

		if (numUses > 0)
		{
			assert(numUses <= UINT32_MAX);

			it.usesIndex = static_cast<uint32_t>(totalUsesCount);
			it.usesCount = static_cast<uint32_t>(numUses);

			// pointers must be sorted, same principle as WritePagePointers.
			std::sort(it._uses.begin(), it._uses.end());
			totalUsesCount += numUses;
		}
	}

	m_Header.usesCount = static_cast<uint32_t>(totalUsesCount);
}

//-----------------------------------------------------------------------------
// purpose: populates file relations vector with combined asset relation data
//-----------------------------------------------------------------------------
void CPakFileBuilder::GenerateAssetDependents()
{
	size_t totalDependentsCount = 0;

	for (PakAsset_t& it : m_assets)
	{
		const size_t numDependents = it._dependents.size();

		if (numDependents > 0)
		{
			assert(numDependents <= UINT32_MAX);

			it.dependentsIndex = static_cast<uint32_t>(totalDependentsCount);
			it.dependentsCount = static_cast<uint32_t>(numDependents);

			totalDependentsCount += numDependents;
		}
	}

	m_Header.dependentsCount = static_cast<uint32_t>(totalDependentsCount);
}

PakPageLump_s CPakFileBuilder::CreatePageLump(const size_t size, const int flags, const int alignment, void* const buf)
{
	return m_pageBuilder.CreatePageLump(static_cast<int>(size), flags, alignment, buf);
}

void CPakFileBuilder::EnsurePageCapacity(const int flags, const int alignment, const int requiredSize)
{
	m_pageBuilder.EnsurePageCapacity(flags, alignment, requiredSize);
}

//-----------------------------------------------------------------------------
// purpose:
// returns:
//-----------------------------------------------------------------------------
PakAsset_t* CPakFileBuilder::GetAssetByGuid(const PakGuid_t guid, size_t* const idx /*= nullptr*/, const bool silent /*= false*/)
{
	const auto it = m_assetGuidMap.find(guid);
	if (it != m_assetGuidMap.end())
	{
		const size_t index = it->second;
		if (idx)
			*idx = index;
		return &m_assets[index];
	}
	if (!silent)
		Debug("Failed to find asset with guid %llX.\n", guid);

	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: initialize pak encoder context
// 
// note(amos): unlike the pak file header, the zstd frame header needs to know
// the uncompressed size without the file header.
//-----------------------------------------------------------------------------
static bool Pak_InitEncoderContext(ZSTD_CCtx* const cctx, const size_t uncompressedBlockSize, const int compressLevel, const int workerCount)
{
	ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
	size_t result = ZSTD_CCtx_setPledgedSrcSize(cctx, uncompressedBlockSize);

	if (ZSTD_isError(result))
	{
		Warning("Failed to set pledged source size %zu: [%s].\n", uncompressedBlockSize, ZSTD_getErrorName(result));
		return false;
	}

	result = ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, compressLevel);

	if (ZSTD_isError(result))
	{
		Warning("Failed to set compression level %i: [%s].\n", compressLevel, ZSTD_getErrorName(result));
		return false;
	}

	result = ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, workerCount);

	if (ZSTD_isError(result))
	{
		Warning("Failed to set worker count %i: [%s].\n", workerCount, ZSTD_getErrorName(result));
		return false;
	}

	return true;
}

static ZSTDEncoder_s s_zstdPakEncoder;

//-----------------------------------------------------------------------------
// Purpose: stream encode pak file with given level and worker count
// TODO: support Oodle stream to stream compress
//-----------------------------------------------------------------------------
static bool Pak_StreamToStreamEncode(BinaryIO& inStream, BinaryIO& outStream, const size_t headerSize, const int compressLevel, const int workerCount)
{
	// only the data past the main header gets compressed.
	const size_t decodedFrameSize = (static_cast<size_t>(inStream.GetSize()) - headerSize);

	if (!decodedFrameSize)
	{
		Warning("%s: pak file contains no data to be compressed.\n", __FUNCTION__);
		return false;
	}

	if (!Pak_InitEncoderContext(&s_zstdPakEncoder.cctx, decodedFrameSize, compressLevel, workerCount))
	{
		return false;
	}

	const size_t buffInSize = ZSTD_CStreamInSize();
	std::unique_ptr<uint8_t[]> buffInPtr(new uint8_t[buffInSize]);

	if (!buffInPtr)
	{
		Warning("%s: failed to allocate input stream buffer of size %zu.\n", __FUNCTION__, buffInSize);
		return false;
	}

	const size_t buffOutSize = ZSTD_CStreamOutSize();
	std::unique_ptr<uint8_t[]> buffOutPtr(new uint8_t[buffOutSize]);

	if (!buffOutPtr)
	{
		Warning("%s: failed to allocate output stream buffer of size %zu.\n", __FUNCTION__, buffOutSize);
		return false;
	}

	void* const buffIn = buffInPtr.get();
	void* const buffOut = buffOutPtr.get();

	inStream.SeekGet(headerSize);
	outStream.SeekPut(headerSize);

	size_t bytesLeft = decodedFrameSize;
	size_t totalBytesRead = 0;

	while (bytesLeft)
	{
		const bool lastChunk = (bytesLeft < buffInSize);
		const size_t numBytesToRead = lastChunk ? bytesLeft : buffInSize;

		inStream.Read(reinterpret_cast<uint8_t*>(buffIn), numBytesToRead);
		bytesLeft -= numBytesToRead;
		totalBytesRead += numBytesToRead;

		// Update progress for compression
		Utils::ProgressPrint(totalBytesRead, decodedFrameSize, "Compressing: ");

		ZSTD_EndDirective const mode = lastChunk ? ZSTD_e_end : ZSTD_e_continue;
		ZSTD_inBuffer inputFrame = { buffIn, numBytesToRead, 0 };

		bool finished;
		do {
			ZSTD_outBuffer outputFrame = { buffOut, buffOutSize, 0 };
			size_t const remaining = ZSTD_compressStream2(&s_zstdPakEncoder.cctx, &outputFrame, &inputFrame, mode);

			if (ZSTD_isError(remaining))
			{
				Warning("Failed to compress stream at %zd to stream at %zd: [%s].\n",
					inStream.TellGet(), outStream.TellPut(), ZSTD_getErrorName(remaining));

				return false;
			}

			outStream.Write(reinterpret_cast<uint8_t*>(buffOut), outputFrame.pos);

			finished = lastChunk ? (remaining == 0) : (inputFrame.pos == inputFrame.size);
		} while (!finished);
	}

	Utils::ProgressComplete();
	return true;
}

static bool Pak_InitDecoderContext(ZSTD_DCtx* const dctx)
{
	ZSTD_DCtx_reset(dctx, ZSTD_reset_session_only);
	return true;
}

static ZSTDDecoder_s s_zstdPakDecoder;

static bool Pak_StreamToStreamDecode(BinaryIO& inStream, BinaryIO& outStream, const size_t headerSize)
{
	// only the data past the main header gets compressed.
	const size_t encodedFrameSize = (static_cast<size_t>(inStream.GetSize()) - headerSize);

	if (!encodedFrameSize)
	{
		Warning("%s: pak file contains no data to be decompressed.\n", __FUNCTION__);
		return false;
	}

	if (!Pak_InitDecoderContext(&s_zstdPakDecoder.dctx))
	{
		return false;
	}

	const size_t buffInSize = ZSTD_DStreamInSize();
	std::unique_ptr<uint8_t[]> buffInPtr(new uint8_t[buffInSize]);

	if (!buffInPtr)
	{
		Warning("%s: failed to allocate input stream buffer of size %zu.\n", __FUNCTION__, buffInSize);
		return false;
	}

	const size_t buffOutSize = ZSTD_DStreamOutSize();
	std::unique_ptr<uint8_t[]> buffOutPtr(new uint8_t[buffOutSize]);

	if (!buffOutPtr)
	{
		Warning("%s: failed to allocate output stream buffer of size %zu.\n", __FUNCTION__, buffOutSize);
		return false;
	}

	void* const buffIn = buffInPtr.get();
	void* const buffOut = buffOutPtr.get();

	inStream.SeekGet(headerSize);
	outStream.SeekPut(headerSize);

	size_t bytesLeft = encodedFrameSize;
	size_t totalBytesRead = 0;
	size_t lastRet = 0;

	while (bytesLeft)
	{
		const bool lastChunk = (bytesLeft < buffInSize);
		const size_t numBytesToRead = lastChunk ? bytesLeft : buffInSize;

		inStream.Read(reinterpret_cast<uint8_t*>(buffIn), numBytesToRead);
		bytesLeft -= numBytesToRead;
		totalBytesRead += numBytesToRead;

		// Update progress for decompression
		Utils::ProgressPrint(totalBytesRead, encodedFrameSize, "Decompressing: ");

		ZSTD_inBuffer inputFrame = { buffIn, numBytesToRead, 0 };

		while (inputFrame.pos < inputFrame.size) {
			ZSTD_outBuffer outputFrame = { buffOut, buffOutSize, 0 };
			size_t const ret = ZSTD_decompressStream(&s_zstdPakDecoder.dctx, &outputFrame, &inputFrame);

			if (ZSTD_isError(ret))
			{
				Error("Failed to decompress stream at %zd to stream at %zd: [%s].\n",
					inStream.TellGet(), outStream.TellPut(), ZSTD_getErrorName(ret));

				return false;
			}

			outStream.Write(reinterpret_cast<uint8_t*>(buffOut), outputFrame.pos);
			lastRet = ret;
		}
	}

	if (lastRet != 0) {

		Error("Failed to decompress; reached EOF before end of stream! (%zu).\n", lastRet);
		return false;
	}

	Utils::ProgressComplete();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: stream encode pak file to new stream and swap old stream with new
//-----------------------------------------------------------------------------
size_t Pak_EncodeStreamAndSwap(BinaryIO& io, const int compressLevel, const int workerCount, const uint16_t pakVersion, const char* const pakPath)
{
	Log("*** encoding pak file \"%s\" with compress level %i and %i workers.\n", pakPath, compressLevel, workerCount);
	const steady_clock::time_point start = high_resolution_clock::now();

	BinaryIO outCompressed;
	std::string outCompressedPath = pakPath;

	outCompressedPath.append("_encoded");

	if (!outCompressed.Open(outCompressedPath, BinaryIO::Mode_e::Write))
	{
		Warning("Failed to open output pak file \"%s\" for compression.\n", outCompressedPath.c_str());
		return 0;
	}

	const size_t decompressedSize = (size_t)io.GetSize();

	if (!Pak_StreamToStreamEncode(io, outCompressed, Pak_GetHeaderSize(pakVersion), compressLevel, workerCount))
		return 0;

	const size_t compressedSize = outCompressed.TellPut();

	if (!Util_ReplaceStream(io, outCompressed, pakPath, outCompressedPath.c_str()))
		return 0;

	const size_t reopenedPakSize = io.GetSize();

	if (reopenedPakSize != compressedSize)
	{
		Error("Reopened pak file \"%s\" appears malformed; compressed size: %zu expected: %zu.\n",
			pakPath, reopenedPakSize, compressedSize);
	}

	const steady_clock::time_point stop = high_resolution_clock::now();
	const microseconds duration = duration_cast<microseconds>(stop - start);

	Log("*** finished pak file encoding; took %lld ms (%zu bytes -- %.1f%% ratio).\n",
		duration.count(), compressedSize, 100.0 * (decompressedSize - compressedSize) / decompressedSize);

	return compressedSize;
}

//-----------------------------------------------------------------------------
// Purpose: stream decode pak file to new stream and swap old stream with new
// TODO: support RTech and Oodle in stream wise manner as well
//-----------------------------------------------------------------------------
size_t Pak_DecodeStreamAndSwap(BinaryIO& io, const uint16_t pakVersion, const char* const pakPath)
{
	Log("*** decoding pak file \"%s\".\n", pakPath);
	const steady_clock::time_point start = high_resolution_clock::now();

	BinaryIO outDecompressed;
	std::string outDecompressedPath = pakPath;

	outDecompressedPath.append("_decoded");

	if (!outDecompressed.Open(outDecompressedPath, BinaryIO::Mode_e::Write))
	{
		Warning("Failed to open output pak file \"%s\" for decompression.\n", outDecompressedPath.c_str());
		return 0;
	}

	const size_t compressedSize = (size_t)io.GetSize();

	if (!Pak_StreamToStreamDecode(io, outDecompressed, Pak_GetHeaderSize(pakVersion)))
		return 0;

	const size_t decompressedSize = outDecompressed.TellPut();

	if (!Util_ReplaceStream(io, outDecompressed, pakPath, outDecompressedPath.c_str()))
		return 0;

	const size_t reopenedPakSize = io.GetSize();

	if (reopenedPakSize != decompressedSize)
	{
		Error("Reopened pak file \"%s\" appears malformed; decompressed size: %zu expected: %zu.\n",
			pakPath, reopenedPakSize, decompressedSize);
	}

	const steady_clock::time_point stop = high_resolution_clock::now();
	const microseconds duration = duration_cast<microseconds>(stop - start);

	Log("*** finished pak file decoding; took %lld ms (%zu bytes -- %.1f%% ratio).\n",
		duration.count(), decompressedSize, 100.0 * (decompressedSize - compressedSize) / decompressedSize);
	return decompressedSize;
}

//-----------------------------------------------------------------------------
// Purpose: one-shot oodle (kraken) encode of the post-header region, matching
// the raw OodleLZ stream format the apex/s21 runtime decodes. returns the size
// of the compressed region only (excluding the header), which is what the
// runtime stores in PakHdr_t::compressedSize for oodle-encoded paks.
//-----------------------------------------------------------------------------
size_t Pak_OodleEncodeStreamAndSwap(BinaryIO& io, const int compressLevel, const int backwardCompatMajor, const uint16_t pakVersion, const char* const pakPath)
{
	Log("*** encoding pak file \"%s\" with Oodle (Kraken) compress level %i (bwCompat=%i).\n", pakPath, compressLevel, backwardCompatMajor);
	const steady_clock::time_point start = high_resolution_clock::now();

	const size_t headerSize = Pak_GetHeaderSize(pakVersion);
	const size_t totalSize = static_cast<size_t>(io.GetSize());

	if (totalSize <= headerSize)
	{
		Warning("%s: pak file contains no data to be compressed.\n", __FUNCTION__);
		return 0;
	}

	const size_t rawRegion = totalSize - headerSize;

	std::unique_ptr<uint8_t[]> rawBuf(new uint8_t[rawRegion]);
	io.SeekGet(static_cast<std::streamoff>(headerSize));
	io.Read(rawBuf.get(), rawRegion);

	const size_t bound = Oodle::GetCompressedBufferSizeNeeded(rawRegion);
	std::unique_ptr<uint8_t[]> cmpBuf(new uint8_t[bound]);

	const size_t cmpLen = Oodle::Compress(rawBuf.get(), rawRegion, cmpBuf.get(), compressLevel, backwardCompatMajor);

	if (!cmpLen)
	{
		Error("Oodle compression failed for pak file \"%s\".\n", pakPath);
		return 0;
	}

	BinaryIO outCompressed;
	std::string outCompressedPath = pakPath;
	outCompressedPath.append("_encoded");

	if (!outCompressed.Open(outCompressedPath, BinaryIO::Mode_e::Write))
	{
		Warning("Failed to open output pak file \"%s\" for compression.\n", outCompressedPath.c_str());
		return 0;
	}

	// the header gap is overwritten by the caller with the updated header.
	outCompressed.SeekPut(static_cast<std::streamoff>(headerSize));
	outCompressed.Write(cmpBuf.get(), cmpLen);

	if (!Util_ReplaceStream(io, outCompressed, pakPath, outCompressedPath.c_str()))
		return 0;

	const steady_clock::time_point stop = high_resolution_clock::now();
	const microseconds duration = duration_cast<microseconds>(stop - start);

	Log("*** finished pak file encoding; took %lld ms (%zu bytes -- %.1f%% ratio).\n",
		duration.count(), cmpLen, 100.0 * (rawRegion - cmpLen) / rawRegion);

	return cmpLen;
}

//-----------------------------------------------------------------------------
// Purpose: one-shot oodle decode counterpart of Pak_OodleEncodeStreamAndSwap.
// rawRegionSize is the decompressed post-header size (decompressedSize - headerSize).
//-----------------------------------------------------------------------------
size_t Pak_OodleDecodeStreamAndSwap(BinaryIO& io, const uint16_t pakVersion, const char* const pakPath, const size_t rawRegionSize)
{
	Log("*** decoding pak file \"%s\" using Oodle.\n", pakPath);
	const steady_clock::time_point start = high_resolution_clock::now();

	const size_t headerSize = Pak_GetHeaderSize(pakVersion);
	const size_t totalSize = static_cast<size_t>(io.GetSize());

	if (totalSize <= headerSize)
	{
		Warning("%s: pak file contains no data to be decompressed.\n", __FUNCTION__);
		return 0;
	}

	const size_t compRegion = totalSize - headerSize;

	std::unique_ptr<uint8_t[]> cmpBuf(new uint8_t[compRegion]);
	io.SeekGet(static_cast<std::streamoff>(headerSize));
	io.Read(cmpBuf.get(), compRegion);

	std::unique_ptr<uint8_t[]> rawBuf(new uint8_t[rawRegionSize]);

	if (!Oodle::Decompress(cmpBuf.get(), compRegion, rawBuf.get(), rawRegionSize))
	{
		Error("Oodle decompression failed for pak file \"%s\".\n", pakPath);
		return 0;
	}

	BinaryIO outDecompressed;
	std::string outDecompressedPath = pakPath;
	outDecompressedPath.append("_decoded");

	if (!outDecompressed.Open(outDecompressedPath, BinaryIO::Mode_e::Write))
	{
		Warning("Failed to open output pak file \"%s\" for decompression.\n", outDecompressedPath.c_str());
		return 0;
	}

	outDecompressed.SeekPut(static_cast<std::streamoff>(headerSize));
	outDecompressed.Write(rawBuf.get(), rawRegionSize);

	if (!Util_ReplaceStream(io, outDecompressed, pakPath, outDecompressedPath.c_str()))
		return 0;

	const steady_clock::time_point stop = high_resolution_clock::now();
	const microseconds duration = duration_cast<microseconds>(stop - start);

	Log("*** finished pak file decoding; took %lld ms (%zu bytes).\n",
		duration.count(), headerSize + rawRegionSize);

	return headerSize + rawRegionSize;
}

static void SplitPakPathBlob(const char* const buf, const size_t len, std::vector<std::string>& out)
{
	size_t i = 0;
	while (i < len)
	{
		const char* const s = buf + i;
		const size_t n = strnlen(s, len - i);
		if (n)
			out.emplace_back(s, n);
		i += n + 1;
	}
}

static void UnpackStreamOff(const int64_t packed, int64_t& off, int64_t& idx)
{
	if (packed < 0)
	{
		off = -1;
		idx = -1;
		return;
	}

	idx = packed & 0xFFF;
	off = packed & ~0xFFFLL;
	if (off == 0 && idx == 0)
	{
		off = -1;
		idx = -1;
	}
}

void CPakFileBuilder::LoadStreamReuseMap()
{
	m_streamReuse.clear();
	m_reuseMandPaths.clear();
	m_reuseOptPaths.clear();
	m_streamReuseHits = 0;

	std::string path = m_pakFilePath;
	BinaryIO in;
	if (!in.Open(path.c_str(), BinaryIO::Mode_e::Read) || in.GetSize() < PAK_HEADER_SIZE_V8)
	{
		in.Close();
		path = m_pakFilePath + ".pre_cbremap";
		if (!in.Open(path.c_str(), BinaryIO::Mode_e::Read) || in.GetSize() < PAK_HEADER_SIZE_V8)
			return;
	}

	unsigned int magic = 0;
	uint16_t version = 0;
	in.Read(magic);
	in.Read(version);
	if (magic != 0x6b615052 || version != 8)
		return;

	in.SeekGet(0x48);
	const uint16_t sp = in.Read<uint16_t>();
	const uint16_t opt = in.Read<uint16_t>();
	const uint16_t slabs = in.Read<uint16_t>();
	const uint16_t pc = in.Read<uint16_t>();
	in.SeekGet(0x54);
	const uint32_t ptrc = in.Read<uint32_t>();
	const uint32_t ac = in.Read<uint32_t>();
	if (ac == 0 || ac > 200000)
		return;

	const size_t pagesOff = (size_t)PAK_HEADER_SIZE_V8 + sp + opt + (size_t)slabs * 16;
	const size_t descOff = pagesOff + (size_t)pc * 12;
	const size_t assetsOff = descOff + (size_t)ptrc * 8;
	if (assetsOff + (size_t)ac * 80 > (size_t)in.GetSize())
		return;

	if (sp)
	{
		std::vector<char> blob(sp);
		in.SeekGet(PAK_HEADER_SIZE_V8);
		in.Read(blob.data(), sp);
		SplitPakPathBlob(blob.data(), sp, m_reuseMandPaths);
	}
	if (opt)
	{
		std::vector<char> blob(opt);
		in.SeekGet(PAK_HEADER_SIZE_V8 + sp);
		in.Read(blob.data(), opt);
		SplitPakPathBlob(blob.data(), opt, m_reuseOptPaths);
	}

	in.SeekGet(static_cast<std::streamoff>(assetsOff));
	for (uint32_t i = 0; i < ac; i++)
	{
		const PakGuid_t guid = in.Read<PakGuid_t>();
		in.SeekGet(8 + 16, std::ios::cur); // unk0 + two PagePtr_t
		const int64_t packedMand = in.Read<int64_t>();
		const int64_t packedOpt = in.Read<int64_t>();
		in.SeekGet(80 - 8 - 8 - 16 - 8 - 8, std::ios::cur); // rest of 80B entry

		StreamReuse_s e;
		UnpackStreamOff(packedMand, e.mandOff, e.mandIdx);
		UnpackStreamOff(packedOpt, e.optOff, e.optIdx);
		if (e.mandOff < 0 && e.optOff < 0)
			continue;
		m_streamReuse[guid] = e;
	}

	Log("Loaded %zu stream-reuse entries from \"%s\" (%zu mand paths, %zu opt paths).\n",
		m_streamReuse.size(), path.c_str(), m_reuseMandPaths.size(), m_reuseOptPaths.size());
}

bool CPakFileBuilder::TryReuseStreaming(const PakGuid_t guid, PakStreamSetEntry_s* const mand, PakStreamSetEntry_s* const opt)
{
	const auto it = m_streamReuse.find(guid);
	if (it == m_streamReuse.end())
		return false;

	const StreamReuse_s& e = it->second;
	bool used = false;

	if (mand && e.mandOff >= 0)
	{
		if (e.mandIdx < 0 || static_cast<size_t>(e.mandIdx) >= m_reuseMandPaths.size())
			return false;
		mand->streamOffset = e.mandOff;
		mand->streamIndex = AddStreamingFileReference(m_reuseMandPaths[static_cast<size_t>(e.mandIdx)].c_str(), true);
		used = true;
	}

	if (opt && e.optOff >= 0)
	{
		if (e.optIdx < 0 || static_cast<size_t>(e.optIdx) >= m_reuseOptPaths.size())
			return false;
		opt->streamOffset = e.optOff;
		opt->streamIndex = AddStreamingFileReference(m_reuseOptPaths[static_cast<size_t>(e.optIdx)].c_str(), false);
		used = true;
	}

	if (used)
		m_streamReuseHits++;
	return used;
}

//-----------------------------------------------------------------------------
// purpose: builds rpak and starpak from input map file
//-----------------------------------------------------------------------------
void CPakFileBuilder::BuildFromMap(const js::Document& doc)
{
	// determine source asset directory from map file
	m_assetPath = JSON_GetValueRequired<const char*>(doc, "assetsDir");

	Utils::AppendSlash(m_assetPath);
	Utils::ResolvePath(m_assetPath, m_buildSettings->GetBuildMapPath());

	this->SetVersion(static_cast<uint16_t>(m_buildSettings->GetPakVersion()));

	// base pak-header flags. The S21 native loader requires bit 0x20 (and the
	// shipping client paks set 0x2C on perm / 0x24 on temp); without it the pak is
	// rejected ("pak load failed"). Compression flags are OR'd in later by the
	// '-compress' step, so this is just the base.
	m_Header.flags = static_cast<uint16_t>(JSON_GetValueOrDefault(doc, "headerFlags", 0));

	const char* const pakName = JSON_GetValueOrDefault(doc, "name", DEFAULT_RPAK_NAME);

	// print parsed settings
	Debug("Build Settings:\n");
	Debug("File Version: %i\n", GetVersion());
	Debug("File Name: %s.rpak\n", pakName);
	Debug("Asset Directory: %s\n", m_assetPath.c_str());

	// set build path
	SetPath(std::string(m_buildSettings->GetOutputPath()) + pakName + ".rpak");
	LoadStreamReuseMap();

	// create file stream from path created above
	BinaryIO out;
	if (!out.Open(m_pakFilePath, BinaryIO::Mode_e::ReadWriteCreate))
		Error("Failed to open output pak file \"%s\".\n", m_pakFilePath.c_str());

	Log("*** building pak file \"%s\".\n", m_pakFilePath.c_str());

	// Skip the header data at first so we can come back and fill it in when we have all of the info
	out.Pad(GetVersion() >= 8 ? PAK_HEADER_SIZE_V8 : PAK_HEADER_SIZE_V6);

	rapidjson::Value::ConstMemberIterator filesIt;

	if (JSON_GetIterator(doc, "files", JSONFieldType_e::kArray, filesIt))
	{
		const auto& filesArray = filesIt->value.GetArray();
		const size_t totalFiles = filesArray.Size();

		// First pass: collect all asset GUIDs so we can validate references
		for (const auto& file : filesArray)
		{
			const char* const assetPath = JSON_GetValueOrDefault(file, "_path", static_cast<const char*>(nullptr));
			if (assetPath)
			{
				const PakGuid_t assetGuid = Pak_GetGuidOverridable(file, assetPath);
				m_knownAssetGuids.insert(assetGuid);
			}
		}

		// Second pass: actually process the assets
		size_t currentFile = 0;
		int lastProgressPercent = -1;

		for (const auto& file : filesArray)
		{
			currentFile++;

			// Throttle progress printing - only print when percentage changes
			const int currentProgressPercent = (int)((float)currentFile / totalFiles * 100);
			if (currentProgressPercent != lastProgressPercent)
			{
				Utils::ProgressPrint(currentFile, totalFiles, "Building assets: ");
				lastProgressPercent = currentProgressPercent;
			}

			AddAsset(file);
		}
		Utils::ProgressComplete();
	}

	{
		// write string vectors for starpak paths and get the total length of each vector
		size_t starpakPathsLength = WriteStarpakPaths(out, STREAMING_SET_MANDATORY);
		size_t optStarpakPathsLength = WriteStarpakPaths(out, STREAMING_SET_OPTIONAL);
		const size_t combinedPathsLength = starpakPathsLength + optStarpakPathsLength;

		const size_t aligned = IALIGN8(combinedPathsLength);
		const int8_t padBytes = static_cast<int8_t>(aligned - combinedPathsLength);

		// align starpak paths to 
		if (optStarpakPathsLength != 0)
			optStarpakPathsLength += padBytes;
		else
			starpakPathsLength += padBytes;

		out.Seek(padBytes, std::ios::end);
		SetStarpakPathsSize(static_cast<uint16_t>(starpakPathsLength), static_cast<uint16_t>(optStarpakPathsLength));
	}

	GenerateInternalDependencies();

	// Generate data for asset dependencies and dependents
	GenerateAssetUses();
	GenerateAssetDependents();

	// Pad all of the slabs so that they fit the alignment padding of all contained pages
	m_pageBuilder.PadSlabSizeForPageAlignment();

	// Write header info for slab headers and page headers
	m_pageBuilder.WriteSlabHeaders(out);
	m_pageBuilder.WritePageHeaders(out);

	WritePagePointers(out);
	WriteAssetDescriptors(out);

	WriteAssetUses(out);
	WriteAssetDependents(out);

	// now the actual paged data
	m_pageBuilder.WritePageData(out);

	// We are done building the data of the pack, this is the actual size.
	const size_t decompressedFileSize = out.GetSize();

	const int compressLevel = JSON_GetValueOrDefault(doc, "compressLevel", 0);

	size_t compressedFileSize = 0;
	if (compressLevel > 0 && decompressedFileSize > Pak_GetHeaderSize(m_Header.fileVersion))
	{
		const int workerCount = JSON_GetValueOrDefault(doc, "compressWorkers", 0);
		compressedFileSize = Pak_EncodeStreamAndSwap(out, compressLevel, workerCount, GetVersion(), m_pakFilePath.c_str());

		// set the header flags indicating this pak is compressed using zstandard.
		m_Header.flags |= PAK_HEADER_FLAGS_ZSTD_ENCODED;
	}

	this->SetCompressedSize(compressedFileSize == 0 ? decompressedFileSize : compressedFileSize);
	this->SetDecompressedSize(decompressedFileSize);

	FILETIME headerFileTime = Utils::GetSystemFileTime();
	rapidjson::Value::ConstMemberIterator buildDateIt;
	if (JSON_GetIterator(doc, "buildDate", buildDateIt))
	{
		const rapidjson::Value& buildDate = buildDateIt->value;
		bool parsedBuildDate = false;

		if (buildDate.IsString())
			parsedBuildDate = Utils::TryParseIso8601UtcToFileTime(buildDate.GetString(), buildDate.GetStringLength(), headerFileTime);
		else if (buildDate.IsInt64())
			parsedBuildDate = Utils::TryParseUnixTimestampToFileTime(buildDate.GetInt64(), headerFileTime);
		else if (buildDate.IsUint64())
			parsedBuildDate = Utils::TryParseUnixTimestampToFileTime(buildDate.GetUint64(), headerFileTime);
		else
			Error("\"buildDate\" must be a string or integer value.\n");

		if (!parsedBuildDate)
			Error("Failed to parse \"buildDate\". Use ISO 8601 UTC (e.g. \"2025-11-26T15:04:05Z\") or Unix epoch seconds (>= 0).\n");
	}

	// set header descriptors
	SetFileTime(headerFileTime);

	// If this is set and we have "example.rpak", the runtime will load the
	// library `example.dll` during the load of `example.rpak`, from the same
	// directory the pak is being loaded from. The loading of the library
	// happens before the individual assets are being loaded and parsed.
	if (JSON_GetValueOrDefault(doc, "hasDynamicLibrary", false))
		m_Header.flags |= PAK_HEADER_FLAGS_HAS_MODULE;

	// Go back to the start of the file since now we can write the header successfully
	out.SeekPut(0);

	this->WriteHeader(out);

	Log("*** built pak file \"%s\" with %zu assets, totaling %zd bytes (stream-reuse hits %zu).\n",
		m_pakFilePath.c_str(), GetAssetCount(), out.GetSize(), m_streamReuseHits);
	out.Close();
}
