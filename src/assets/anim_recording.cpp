#include "pch.h"
#include "assets.h"
#include "public/anim_recording.h"

static void AnimRecording_ParseFromANIR(const char* const assetPath, BinaryIO& bio, AnimRecordingFileHeader_s& hdr, size_t& totalBufSize)
{
	if (!bio.Open(assetPath, BinaryIO::Mode_e::Read))
		Error("Failed to open animation recording file \"%s\".\n", assetPath);

	const size_t fileSize = bio.GetSize();

	if (bio.GetSize() <= sizeof(AnimRecordingFileHeader_s))
		Error("Animation recording file \"%s\" appears truncated (%zu <= %zu).\n", assetPath, fileSize, sizeof(AnimRecordingFileHeader_s));

	bio.Read(hdr);

	if (hdr.magic != ANIR_FILE_MAGIC)
		Error("Attempted to load an invalid animation recording file (expected magic %x, got %x).\n", ANIR_FILE_MAGIC, hdr.magic);

	if (hdr.fileVersion != ANIR_FILE_VERSION)
		Error("Attempted to load an unsupported animation recording file (expected file version %x, got %x).\n", ANIR_FILE_VERSION, hdr.fileVersion);

	if (hdr.assetVersion != 1 && hdr.assetVersion != 2)
		Error("Attempted to load an unsupported animation recording file (expected asset version 1 or 2, got %x).\n", hdr.assetVersion);

	const int maxElements = hdr.assetVersion == 1 ? ANIR_MAX_ELEMENTS_V1 : ANIR_MAX_ELEMENTS_V2;

	if (hdr.numElements > maxElements)
		Error("Animation recording file \"%s\" has too many elements (max %d, got %d).\n", assetPath, maxElements, hdr.numElements);

	if (hdr.numSequences > ANIR_MAX_SEQUENCES)
		Error("Animation recording file \"%s\" has too many sequences (max %d, got %d).\n", assetPath, ANIR_MAX_SEQUENCES, hdr.numSequences);

	if (hdr.numRecordedFrames == 0)
		Error("Animation recording file \"%s\" has 0 frames.\n", assetPath);

	if (hdr.numRecordedFrames > ANIR_MAX_RECORDED_FRAMES)
		Error("Animation recording file \"%s\" has too many frames (max %d, got %d).\n", assetPath, ANIR_MAX_RECORDED_FRAMES, hdr.numRecordedFrames);

	// NOTE: the overlay count can be 0, so only check for max here, which is equal to frames.
	if (hdr.numRecordedOverlays > ANIR_MAX_RECORDED_FRAMES)
		Error("Animation recording file \"%s\" has too many overlays (max %d, got %d).\n", assetPath, ANIR_MAX_RECORDED_FRAMES, hdr.numRecordedOverlays);

	const size_t stringBufSize = IALIGN4(hdr.stringBufSize);

	const size_t animFramesBufSize = hdr.numRecordedFrames * sizeof(AnimRecordingFrame_s);
	const size_t animOverlaysBufSize = hdr.numRecordedOverlays * sizeof(AnimRecordingOverlay_s);

	totalBufSize = stringBufSize + animFramesBufSize + animOverlaysBufSize;
}

// page chunk structure and order:
// - header HEAD        (align=8)
// - data   CPU         (align=4)
template <typename Header>
static void AnimRecording_InternalAddAnimRecording(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath,
	BinaryIO& bio, const AnimRecordingFileHeader_s& fileHdr, const size_t cpuBufSize)
{
	PakAsset_t& asset = pak->BeginAsset(assetGuid, assetPath);

	PakPageLump_s hdrLump = pak->CreatePageLump(sizeof(Header), SF_HEAD | SF_SERVER, 8);
	Header* const pHdr = reinterpret_cast<Header*>(hdrLump.data);

	pHdr->startPos = fileHdr.startPos;
	pHdr->startAngles = fileHdr.startAngles;

	pHdr->numRecordedFrames = fileHdr.numRecordedFrames;
	pHdr->numRecordedOverlays = fileHdr.numRecordedOverlays;

	// Only used by the runtime when these assets get recorded,
	// needs to be -1 in the pak file.
	pHdr->runtimeSlotIndex = -1;
	pHdr->runtimeSlotIndexSign = -1;

	pHdr->animRecordingId = fileHdr.animRecordingId;

	// Anim recordings inside pak files must be marked as persistent,
	// else code will try to free it.
	pHdr->isPersistent = true;
	pHdr->runtimeRefCounter = 0;

	size_t cpuBufIt = 0;
	PakPageLump_s cpuLump = pak->CreatePageLump(cpuBufSize, SF_CPU | SF_SERVER, 4);

	for (int i = 0; i < fileHdr.numElements; i++)
	{
		std::string poseParamName;

		if (!bio.ReadString(poseParamName))
			Error("Failed to read pose parameter name #%i\n", i);

		const size_t stringBufLen = poseParamName.length() + 1;
		memcpy(&cpuLump.data[cpuBufIt], poseParamName.c_str(), stringBufLen);

		pak->AddPointer(hdrLump, offsetof(Header, poseParamNames) + i * sizeof(PagePtr_t), cpuLump, cpuBufIt);
		cpuBufIt += stringBufLen;
	}

	for (int i = 0; i < fileHdr.numElements; i++)
	{
		bio.Read(pHdr->poseParamValues[i]);
	}

	for (int i = 0; i < fileHdr.numSequences; i++)
	{
		std::string sequenceName;

		if (!bio.ReadString(sequenceName))
			Error("Failed to read animation sequence name #%i\n", i);

		const size_t stringBufLen = sequenceName.length() + 1;
		memcpy(&cpuLump.data[cpuBufIt], sequenceName.c_str(), stringBufLen);

		pak->AddPointer(hdrLump, offsetof(Header, animSequences) + i * sizeof(PagePtr_t), cpuLump, cpuBufIt);
		cpuBufIt += stringBufLen;
	}

	// Now the frames and overlays are getting written out, these must be aligned
	// to 4 bytes, so align the current buffer iterator out. The extra size taken
	// by this alignment is being accounted for in AnimRecording_ParseFromANIR().
	cpuBufIt = IALIGN4(cpuBufIt);
	pak->AddPointer(hdrLump, offsetof(Header, recordedFrames), cpuLump, cpuBufIt);

	for (int i = 0; i < fileHdr.numRecordedFrames; i++)
	{
		bio.Read(&cpuLump.data[cpuBufIt], sizeof(AnimRecordingFrame_s));
		cpuBufIt += sizeof(AnimRecordingFrame_s);
	}

	if (fileHdr.numRecordedOverlays > 0)
	{
		pak->AddPointer(hdrLump, offsetof(Header, recordedOverlays), cpuLump, cpuBufIt);

		for (int i = 0; i < fileHdr.numRecordedOverlays; i++)
		{
			bio.Read(&cpuLump.data[cpuBufIt], sizeof(AnimRecordingOverlay_s));
			cpuBufIt += sizeof(AnimRecordingOverlay_s);
		}
	}

	asset.InitAsset(hdrLump.GetPointer(), sizeof(Header), PagePtr_t::NullPtr(), fileHdr.assetVersion, AssetType::ANIR);
	asset.SetHeaderPointer(hdrLump.data);

	pak->FinishAsset();
}

// The .anir file records its asset version, so one handler writes both layouts.
void Assets::AddAnimRecording_v1(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& /*mapEntry*/)
{
	const std::string anirPath = Utils::ChangeExtension(pak->GetAssetPath() + assetPath, "anir");

	BinaryIO bio;
	AnimRecordingFileHeader_s fileHdr; size_t cpuBufSize;

	AnimRecording_ParseFromANIR(anirPath.c_str(), bio, fileHdr, cpuBufSize);

	if (fileHdr.assetVersion == 1)
		AnimRecording_InternalAddAnimRecording<AnimRecordingAssetHeader_v1_s>(pak, assetGuid, assetPath, bio, fileHdr, cpuBufSize);
	else
		AnimRecording_InternalAddAnimRecording<AnimRecordingAssetHeader_v2_s>(pak, assetGuid, assetPath, bio, fileHdr, cpuBufSize);
}
