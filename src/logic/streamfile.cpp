//=============================================================================//
//
// Pak streaming file build manager
//
//=============================================================================//
#include <pch.h>
#include "streamfile.h"
#include <sys/stat.h>

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CStreamFileBuilder::CStreamFileBuilder(const CBuildSettings* const buildSettings)
{
	m_buildSettings = buildSettings;
}

//-----------------------------------------------------------------------------
// Purpose: parse and initialize
//-----------------------------------------------------------------------------
void CStreamFileBuilder::Init(const js::Document& doc, const bool useOptional)
{
	rapidjson::Value::ConstMemberIterator mandatoryIt;

	if (JSON_GetIterator(doc, "streamFileMandatory", JSONFieldType_e::kString, mandatoryIt))
	{
		m_mandatoryStreamFileName.assign(mandatoryIt->value.GetString(), mandatoryIt->value.GetStringLength());
		Utils::FixSlashes(m_mandatoryStreamFileName);
	}

	rapidjson::Value::ConstMemberIterator optionalIt;

	if (useOptional && JSON_GetIterator(doc, "streamFileOptional", JSONFieldType_e::kString, optionalIt))
	{
		m_optionalStreamFileName.assign(optionalIt->value.GetString(), optionalIt->value.GetStringLength());
		Utils::FixSlashes(m_optionalStreamFileName);
	}

	rapidjson::Value::ConstMemberIterator streamCacheIt;

	if (JSON_GetIterator(doc, "streamCache", JSONFieldType_e::kString, streamCacheIt))
	{
		fs::path streamCacheDirFs(std::move(std::string(streamCacheIt->value.GetString(), streamCacheIt->value.GetStringLength())));
		std::string streamCacheDirStr = streamCacheDirFs.parent_path().string();

		Utils::ResolvePath(streamCacheDirStr, m_buildSettings->GetBuildMapPath());
		streamCacheDirStr.append(streamCacheDirFs.filename().string());

		Log("Loading cache from streaming map file \"%s\".\n", streamCacheDirStr.c_str());
		m_streamCache.ParseMap(streamCacheDirStr.c_str());

		rapidjson::Value::ConstMemberIterator filterIt;

		if (JSON_GetIterator(doc, "streamCacheFilter", JSONFieldType_e::kArray, filterIt))
		{
			const rapidjson::Value::ConstArray filterArray = filterIt->value.GetArray();
			int filterIdx = -1;

			for (const rapidjson::Value& filtered : filterArray)
			{
				filterIdx++;

				if (!JSON_IsOfType(filtered, JSONFieldType_e::kString))
				{
					Error("Element #%i in array \"%s\" must be a %s.\n",
						filterIdx, "streamCacheFilter", JSON_TypeToString(JSONFieldType_e::kString));
				}

				m_streamCache.AddStreamFileToFilter(filtered.GetString(), filtered.GetStringLength());
			}

			if (!filterArray.Empty())
			{
				if (!m_mandatoryStreamFileName.empty())
					m_streamCache.AddStreamFileToFilter(m_mandatoryStreamFileName);

				if (!m_optionalStreamFileName.empty())
					m_streamCache.AddStreamFileToFilter(m_optionalStreamFileName);
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: shutdown
//-----------------------------------------------------------------------------
void CStreamFileBuilder::Shutdown()
{
	FinishStreamFileStream(STREAMING_SET_MANDATORY);
	FinishStreamFileStream(STREAMING_SET_OPTIONAL);

	const std::string& streamFile = !m_mandatoryStreamFileName.empty()
		? m_mandatoryStreamFileName 
		: m_optionalStreamFileName;

	if (!streamFile.empty())
	{
		const char* streamFileName = Utils::ExtractFileName(streamFile);
		std::string fullFilePath = m_buildSettings->GetOutputPath();

		fullFilePath.append(streamFileName);
		fullFilePath = Utils::ChangeExtension(fullFilePath, ".starmap");

		BinaryIO newCache;

		if (newCache.Open(fullFilePath, BinaryIO::Mode_e::Write))
		{
			m_streamCache.WriteCacheFileToIOStream(newCache);
			Log("Saved cache to streaming map file \"%s\".\n", fullFilePath.c_str());
		}
		else
			Warning("Failed to save cache to streaming map file \"%s\".\n", fullFilePath.c_str());
	}
}

//-----------------------------------------------------------------------------
// Reopen a starpak for append. Put cursor must stay 4096-aligned: the packed
// stream offset overwrites the low 12 bits with the starpak index.
//-----------------------------------------------------------------------------
void CStreamFileBuilder::AdoptExistingStreamFile(BinaryIO& out, const std::string& fullFilePath,
	const PakStreamSet_e set, const int64_t fileSize)
{
	const char* const setName = Pak_StreamSetToName(set);

	if (fileSize < static_cast<int64_t>(STARPAK_DATABLOCK_ALIGNMENT + sizeof(size_t)))
		Error("Existing %s streaming file \"%s\" is too small to append to (%lld bytes).\n",
			setName, fullFilePath.c_str(), (long long)fileSize);

	out.SeekGet(fileSize - sizeof(size_t));
	const size_t entryCount = out.Read<size_t>();

	const int64_t tableSize = static_cast<int64_t>(entryCount) * sizeof(PakStreamSetAssetEntry_s);
	const int64_t tableStart = fileSize - static_cast<int64_t>(sizeof(size_t)) - tableSize;

	if (entryCount == 0 || tableStart < STARPAK_DATABLOCK_ALIGNMENT || tableStart % STARPAK_DATABLOCK_ALIGNMENT != 0)
		Error("Existing %s streaming file \"%s\" has an unreadable entry table (%zu entries, table at %lld).\n",
			setName, fullFilePath.c_str(), entryCount, (long long)tableStart);

	std::vector<PakStreamSetAssetEntry_s>& dataBlockDescs = set == STREAMING_SET_MANDATORY
		? m_mandatoryStreamingDataBlocks
		: m_optionalStreamingDataBlocks;

	dataBlockDescs.resize(entryCount);

	out.SeekGet(tableStart);
	out.Read(dataBlockDescs.data(), static_cast<size_t>(tableSize));

	for (const PakStreamSetAssetEntry_s& desc : dataBlockDescs)
	{
		if (desc.offset < STARPAK_DATABLOCK_ALIGNMENT || desc.size <= 0
			|| desc.offset % STARPAK_DATABLOCK_ALIGNMENT != 0
			|| desc.size % STARPAK_DATABLOCK_ALIGNMENT != 0
			|| desc.offset + desc.size > tableStart)
		{
			Error("Existing %s streaming file \"%s\" declares an invalid data block (offset %lld, size %lld).\n",
				setName, fullFilePath.c_str(), (long long)desc.offset, (long long)desc.size);
		}
	}

	// New blocks overwrite the stale table; FinishStreamFileStream rewrites it
	// in full, old entries included.
	out.SeekPut(tableStart);

	Log("Appending %s streaming file \"%s\" (%zu existing assets, %lld bytes of data).\n",
		setName, fullFilePath.c_str(), entryCount, (long long)tableStart);
}

//-----------------------------------------------------------------------------
// Purpose: creates the stream file stream and sets the header up
//-----------------------------------------------------------------------------
void CStreamFileBuilder::CreateStreamFileStream(const std::string& streamFilePath, const PakStreamSet_e set)
{
	BinaryIO& out = set == STREAMING_SET_MANDATORY ? m_mandatoryStreamFile : m_optionalStreamFile;

	if (out.IsWritable())
		return; // Already opened.

	const char* streamFileName = Utils::ExtractFileName(streamFilePath);

	std::string fullFilePath = m_buildSettings->GetOutputPath();
	fullFilePath.append(streamFileName);

	// A cache miss on a NEW streamed asset must not truncate the existing
	// starpak -- reused assets still point at the old offsets.
	struct _stat64 st {};
	if (_stat64(fullFilePath.c_str(), &st) == 0 && st.st_size >= STARPAK_DATABLOCK_ALIGNMENT)
	{
		if (!out.Open(fullFilePath, BinaryIO::Mode_e::ReadWrite))
			Error("Failed to open %s streaming file \"%s\" for append.\n", Pak_StreamSetToName(set), fullFilePath.c_str());

		AdoptExistingStreamFile(out, fullFilePath, set, st.st_size);
		return;
	}

	if (!out.Open(fullFilePath, BinaryIO::Mode_e::Write))
		Error("Failed to open %s streaming file \"%s\".\n", Pak_StreamSetToName(set), fullFilePath.c_str());

	Log("Opened %s streaming file stream \"%s\".\n", Pak_StreamSetToName(set), fullFilePath.c_str());

	const PakStreamSetFileHeader_s srpkHeader{ STARPAK_MAGIC, STARPAK_VERSION };
	out.Write(srpkHeader);

	char initialPadding[STARPAK_DATABLOCK_ALIGNMENT - sizeof(PakStreamSetFileHeader_s)];
	memset(initialPadding, STARPAK_DATABLOCK_ALIGNMENT_PADDING, sizeof(initialPadding));

	out.Write(initialPadding, sizeof(initialPadding));
}

//-----------------------------------------------------------------------------
// Purpose: writes the sorts table and finishes the stream file stream
//-----------------------------------------------------------------------------
void CStreamFileBuilder::FinishStreamFileStream(const PakStreamSet_e set)
{
	const bool isMandatory = set == STREAMING_SET_MANDATORY;
	BinaryIO& out = isMandatory ? m_mandatoryStreamFile : m_optionalStreamFile;

	if (!out.IsWritable())
		return; // Never opened.

	// starpaks have a table of sorts at the end of the file, containing the offsets and data sizes for every data block
	const auto& vecData = isMandatory ? m_mandatoryStreamingDataBlocks : m_optionalStreamingDataBlocks;

	for (const PakStreamSetAssetEntry_s& it : vecData)
		out.Write(it);

	const size_t entryCount = isMandatory ? GetMandatoryStreamingAssetCount() : GetOptionalStreamingAssetCount();
	out.Write(entryCount);

	const std::string& streamFileName = isMandatory ? m_mandatoryStreamFileName : m_optionalStreamFileName;

	Log("Built %s streaming file \"%s\" with %zu assets, totaling %zd bytes.\n",
		Pak_StreamSetToName(set), streamFileName.c_str(), entryCount, (ssize_t)out.TellPut());

	out.Close();
}

//-----------------------------------------------------------------------------
// purpose: adds new starpak data entry
//-----------------------------------------------------------------------------
bool CStreamFileBuilder::AddStreamingDataEntry(const int64_t size, const uint8_t* const data,
	const PakStreamSet_e set, StreamAddEntryResults_s& outResults)
{
	const bool isMandatory = set == STREAMING_SET_MANDATORY;
	const std::string& newStarPak = isMandatory ? m_mandatoryStreamFileName : m_optionalStreamFileName;

	StreamCacheFindParams_s params = m_streamCache.CreateParams(data, size, newStarPak.c_str());
	StreamCacheFindResult_s result;

	if (m_streamCache.Find(params, result, !isMandatory))
	{
		outResults.streamFile = result.fileEntry->streamFilePath.c_str();
		outResults.pathIndex = result.dataEntry->pathIndex;
		outResults.dataOffset = result.dataEntry->dataOffset;

		return false; // Data wasn't added, but mapped to existing data.
	}

	BinaryIO& out = isMandatory ? m_mandatoryStreamFile : m_optionalStreamFile;

	if (!out.IsWritable())
		CreateStreamFileStream(newStarPak, set);

	if (!out.IsWritable())
		Error("Attempted to write %s streaming asset without a stream file handle.\n", Pak_StreamSetToName(set));

	// The block offset must be 4096-aligned; PakAsset_t::GetPackedStreamOffset
	// stores the starpak index in its low 12 bits.
	const int64_t cursor = out.TellPut();
	const int64_t dataOffset = IALIGN(cursor, (int64_t)STARPAK_DATABLOCK_ALIGNMENT);

	if (dataOffset > cursor)
		out.Pad(static_cast<size_t>(dataOffset - cursor));

	assert(dataOffset >= STARPAK_DATABLOCK_ALIGNMENT);

	out.Write(data, size);
	const int64_t paddedSize = IALIGN(size, STARPAK_DATABLOCK_ALIGNMENT);

	// starpak data is aligned to 4096 bytes, pad the remainder out for the next asset.
	if (paddedSize > size)
	{
		const size_t paddingRemainder = paddedSize - size;
		out.Pad(paddingRemainder);
	}

	std::vector<PakStreamSetAssetEntry_s>& dataBlockDescs = isMandatory ? m_mandatoryStreamingDataBlocks : m_optionalStreamingDataBlocks;
	PakStreamSetAssetEntry_s& desc = dataBlockDescs.emplace_back();

	desc.offset = dataOffset;
	desc.size = paddedSize;

	outResults.streamFile = newStarPak.c_str();
	outResults.dataOffset = dataOffset;

	m_streamCache.Add(params, dataOffset, !isMandatory);
	return true;
}
