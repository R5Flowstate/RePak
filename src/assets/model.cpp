#include "pch.h"
#include "assets.h"
#include "public/studio.h"
#include "public/material.h"

char* Model_ReadRMDLFile(const std::string& path, const uint64_t alignment = 64)
{
    BinaryIO modelFile;

    if (!modelFile.Open(path, BinaryIO::Mode_e::Read))
        Error("Failed to open model file \"%s\".\n", path.c_str());

    const size_t fileSize = modelFile.GetSize();

    if (fileSize < sizeof(studiohdr_t))
        Error("Invalid model file \"%s\"; must be at least %zu bytes, found %zu.\n", path.c_str(), sizeof(studiohdr_t), fileSize);

    char* const buf = new char[IALIGN(fileSize, alignment)];
    modelFile.Read(buf, fileSize);

    studiohdr_t* const pHdr = reinterpret_cast<studiohdr_t*>(buf);

    if (pHdr->id != 'TSDI') // "IDST"
        Error("Invalid model file \"%s\"; expected magic %x, found %x.\n", path.c_str(), 'TSDI', pHdr->id);

    if (pHdr->version != 54)
        Error("Invalid model file \"%s\"; expected version %i, found %i.\n", path.c_str(), 54, pHdr->version);

    if (pHdr->length > fileSize)
        Error("Invalid model file \"%s\"; studiohdr->length(%zu) > fileSize(%zu).\n", path.c_str(), (size_t)pHdr->length, fileSize);

    return buf;
}

static char* Model_ReadVGFile(const std::string& path, int64_t* const pFileSize, size_t* const pFileSizePageAligned)
{
    BinaryIO vgFile;

    if (!vgFile.Open(path, BinaryIO::Mode_e::Read))
        Error("Failed to open vertex group file \"%s\".\n", path.c_str());

    const int64_t fileSize = vgFile.GetSize();

    if (fileSize < sizeof(VertexGroupHeader_t))
        Error("Invalid vertex group file \"%s\"; must be at least %zu bytes, found %zu.\n", path.c_str(), sizeof(VertexGroupHeader_t), fileSize);

    // note(amos): need to align it to STARPAK_DATABLOCK_ALIGNMENT since the
    // actual VG is also aligned to this value in the starpak, and the table at
    // the end of the starpak (see struct PakStreamSetAssetEntry_s in starpak.h
    // ), that we use for data deduplication, stores the asset's size aligned
    // so in order to yield the same hash we need to hash the data page aligned.
    const size_t fileSizePageAligned = IALIGN(fileSize, STARPAK_DATABLOCK_ALIGNMENT);

    char* const buf = new char[fileSizePageAligned];
    vgFile.Read(buf, fileSize);

    const size_t remainder = fileSizePageAligned - fileSize;

    // Null the rest since this will affect the hash result.
    if (remainder > 0)
        memset(&buf[fileSize], 0, remainder);

    VertexGroupHeader_t* const pHdr = reinterpret_cast<VertexGroupHeader_t*>(buf);

    if (pHdr->id != 'GVt0') // "0tVG"
        Error("Invalid vertex group file \"%s\"; expected magic %x, found %x.\n", path.c_str(), 'GVt0', pHdr->id);

    // not sure if this is actually version but i've also never seen it != 1
    if (pHdr->version != 1)
        Error("Invalid vertex group file \"%s\"; expected version %i, found %i.\n", path.c_str(), 1, pHdr->version);

    *pFileSize = fileSize;
    *pFileSizePageAligned = fileSizePageAligned;

    return buf;
}

static PakGuid_t* Model_AddAnimRigRefs(uint32_t* const animrigCount, const rapidjson::Value& mapEntry)
{
    rapidjson::Value::ConstMemberIterator it;

    if (!JSON_GetIterator(mapEntry, "$animrigs", JSONFieldType_e::kArray, it))
        return nullptr;

    const rapidjson::Value::ConstArray animrigs = it->value.GetArray();

    if (animrigs.Empty())
        return nullptr;

    const size_t numAnimrigs = animrigs.Size();
    PakGuid_t* const guidBuf = new PakGuid_t[numAnimrigs];

    int i = -1;
    for (const auto& animrig : animrigs)
    {
        i++;
        const PakGuid_t guid = Pak_ParseGuid(animrig);

        if (!guid)
            Error("Unable to parse animrig #%i.\n", i);

        guidBuf[i] = guid;
    }

    (*animrigCount) = static_cast<uint32_t>(animrigs.Size());
    return guidBuf;
}

static void Model_AllocateIntermediateDataChunk(CPakFileBuilder* const pak, PakPageLump_s& hdrChunk, ModelAssetHeader_t* const pHdr,
    PakGuid_t* const animrigRefs, const uint32_t animrigCount, PakGuid_t* const sequenceRefs, const uint32_t sequenceCount, 
    const char* const assetPath, PakAsset_t& asset)
{
    // the model name is aligned to 1 byte, but the guid ref block is aligned
    // to 8, we have to pad the name buffer to align the guid ref block. if
    // we have no guid ref blocks, the entire lump will be aligned to 1 byte.
    const size_t modelNameBufLen = strlen(assetPath) + 1;
    const size_t alignedNameBufLen = IALIGN8(modelNameBufLen);

    const size_t animRigRefsBufLen = animrigCount * sizeof(PakGuid_t);
    const size_t sequenceRefsBufLen = sequenceCount * sizeof(PakGuid_t);

    const bool hasGuidRefs = animrigRefs || sequenceRefs;

    PakPageLump_s intermediateChunk = pak->CreatePageLump(alignedNameBufLen + animRigRefsBufLen + sequenceRefsBufLen, SF_CPU, hasGuidRefs ? 8 : 1);
    memcpy(intermediateChunk.data, assetPath, modelNameBufLen); // Write the null-terminated asset path to the chunk buffer.

    pak->AddPointer(hdrChunk, offsetof(ModelAssetHeader_t, pName), intermediateChunk, 0);

    if (hasGuidRefs)
    {
        asset.ExpandGuidBuf(animrigCount + sequenceCount);

        if (animrigRefs)
        {
            const size_t base = alignedNameBufLen;

            memcpy(&intermediateChunk.data[base], animrigRefs, animRigRefsBufLen);
            delete[] animrigRefs;

            pHdr->animRigCount = animrigCount;
            pak->AddPointer(hdrChunk, offsetof(ModelAssetHeader_t, pAnimRigs), intermediateChunk, base);

            for (uint32_t i = 0; i < animrigCount; ++i)
            {
                const size_t offset = base + (i * sizeof(PakGuid_t));
                const PakGuid_t guid = *reinterpret_cast<PakGuid_t*>(&intermediateChunk.data[offset]);

                Pak_RegisterGuidRefAtOffset(guid, offset, intermediateChunk, asset);
            }
        }

        if (sequenceRefs)
        {
            const size_t base = alignedNameBufLen + animRigRefsBufLen;

            memcpy(&intermediateChunk.data[base], sequenceRefs, sequenceRefsBufLen);
            delete[] sequenceRefs;

            pHdr->sequenceCount = sequenceCount;
            pak->AddPointer(hdrChunk, offsetof(ModelAssetHeader_t, pSequences), intermediateChunk, base);

            for (uint32_t i = 0; i < sequenceCount; ++i)
            {
                const size_t offset = base + (i * sizeof(PakGuid_t));
                const PakGuid_t guid = *reinterpret_cast<PakGuid_t*>(&intermediateChunk.data[offset]);

                Pak_RegisterGuidRefAtOffset(guid, offset, intermediateChunk, asset);
            }
        }
    }
}

static void Model_InternalAddVertexGroupData(CPakFileBuilder* const pak, PakPageLump_s* const hdrChunk, ModelAssetHeader_t* const modelHdr, studiohdr_t* const studiohdr, const std::string& rmdlFilePath, PakStreamSetEntry_s& de)
{
    modelHdr->totalVertexDataSize = studiohdr->vtxsize + studiohdr->vvdsize + studiohdr->vvcsize + studiohdr->vvwsize;

    ///--------------------
    // Add VG data
    // VG is a "fake" file extension that's used to store model streaming data (name came from the magic '0tVG')
    // this data is a combined mutated version of the data from .vtx and .vvd in regular source models
    const std::string vgFilePath = Utils::ChangeExtension(rmdlFilePath, ".vg");

    int64_t vgFileSize = 0; size_t vgSizeAligned = 0;
    char* const vgBuf = Model_ReadVGFile(vgFilePath, &vgFileSize, &vgSizeAligned);

    de = pak->AddStreamingDataEntry(vgSizeAligned, (uint8_t*)vgBuf, STREAMING_SET_MANDATORY);

    assert(vgSizeAligned <= UINT32_MAX);
    modelHdr->streamedVertexDataSize = static_cast<uint32_t>(vgSizeAligned);

    // static props must have their vertex group data copied as permanent data in the pak file.
    if (studiohdr->IsStaticProp())
    {
        PakPageLump_s vgLump = pak->CreatePageLump(vgFileSize, SF_CPU | SF_TEMP | SF_CLIENT, 1, vgBuf);
        pak->AddPointer(*hdrChunk, offsetof(ModelAssetHeader_t, pStaticPropVtxCache), vgLump, 0);
    }
    else
        delete[] vgBuf;
}

static void Model_InternalHandleMaterials(CPakFileBuilder* const pak, const rapidjson::Value& mapEntry, 
    PakAsset_t& asset, studiohdr_t* const studiohdr, PakPageLump_s& dataChunk)
{
    // Material Overrides Handling
    rapidjson::Value::ConstMemberIterator materialsIt;

    // todo(amos): do we even want material overrides? shouldn't these need to
    // be fixed in the studiomdl itself? there are reports of this causing many
    // errors as the game tries to read the path from the mdl itself which this
    // loop below doesn't update.
    const bool hasMaterialOverrides = JSON_GetIterator(mapEntry, "$materials", JSONFieldType_e::kArray, materialsIt);
    const rapidjson::Value* materialOverrides = hasMaterialOverrides ? &materialsIt->value : nullptr;

    // handle material overrides register all material guids
    for (int i = 0; i < studiohdr->numtextures; ++i)
    {
        mstudiotexture_t* const tex = studiohdr->pTexture(i);

        if (hasMaterialOverrides)
        {
            rapidjson::Value::ConstArray materialArray = materialOverrides->GetArray();

            if (materialArray.Size() > i)
            {
                const PakGuid_t guid = Pak_ParseGuid(materialArray[i]);

                if (!guid)
                    Error("Unable to parse material #%i.\n", i);

                tex->guid = guid;
            }
        }

        const size_t pos = (char*)tex - dataChunk.data;
        const size_t offset = pos + offsetof(mstudiotexture_t, guid);

        Pak_RegisterGuidRefAtOffset(tex->guid, offset, dataChunk, asset);
        const PakAsset_t* const internalAsset = pak->GetAssetByGuid(tex->guid);

        if (internalAsset)
        {
            // make sure referenced asset is a material for sanity
            internalAsset->EnsureType(TYPE_MATL);
            MaterialShaderType_e expectedMaterialType;

            // note(amos): `studiohdr->materialtypesindex` can be 0, in this case
            // the engine sets the material shader type for the given model to
            // `SKNC` if the model has more than 1 bone, else it sets it to `RGDC`.
            // Engine fallback when materialtypesindex is 0.
            if (studiohdr->materialtypesindex > 0)
                expectedMaterialType = studiohdr->materialType(i);
            else
            {
                expectedMaterialType = (studiohdr->numbones > 1)
                    ? expectedMaterialType = MaterialShaderType_e::SKNC
                    : expectedMaterialType = MaterialShaderType_e::RGDC;
            }

            // model assets don't exist on r2 so we can be sure that this is a v8 pak (and therefore has v15 materials).
            const MaterialAssetHeader_v15_t* const matlHdr = reinterpret_cast<const MaterialAssetHeader_v15_t*>(internalAsset->header);
            const MaterialShaderType_e foundMaterialType = matlHdr->materialType;

            if (foundMaterialType != expectedMaterialType)
            {
                Error("Unexpected shader type for material in slot #%i, expected '%s', found '%s'.\n",
                    i, s_materialShaderTypeNames[expectedMaterialType], s_materialShaderTypeNames[foundMaterialType]);
            }
        }
    }
}

extern PakGuid_t* AnimSeq_AutoAddSequenceRefs(CPakFileBuilder* const pak, uint32_t* const sequenceCount, const rapidjson::Value& mapEntry);

// page chunk structure and order:
// - header        HEAD        (align=8)
// - intermediate  CPU         (align=1?8) name, animrig refs then animseqs refs. aligned to 1 if we don't have any refs.
// - vphysics      TEMP        (align=1)
// - vertex groups TEMP_CLIENT (align=1)
// - rmdl          CPU         (align=64) 64 bit aligned because collision data is loaded with aligned SIMD instructions.
void Assets::AddModelAsset_v9(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry)
{
    // deal with dependencies first; auto-add all animation sequences.
    uint32_t sequenceCount = 0;
    PakGuid_t* const sequenceRefs = AnimSeq_AutoAddSequenceRefs(pak, &sequenceCount, mapEntry);

    // this function only creates the arig guid refs, it does not auto-add.
    uint32_t animrigCount = 0;
    PakGuid_t* const animrigRefs = Model_AddAnimRigRefs(&animrigCount, mapEntry);

    // from here we start with creating lumps for the target model asset.
    PakAsset_t& asset = pak->BeginAsset(assetGuid, assetPath);

    PakPageLump_s hdrChunk = pak->CreatePageLump(sizeof(ModelAssetHeader_t), SF_HEAD, 8);
    ModelAssetHeader_t* const pHdr = reinterpret_cast<ModelAssetHeader_t*>(hdrChunk.data);

    //
    // Name, Anim Rigs and Animseqs, these all share 1 data chunk.
    //
    Model_AllocateIntermediateDataChunk(pak, hdrChunk, pHdr, animrigRefs, animrigCount, sequenceRefs, sequenceCount, assetPath, asset);

    const std::string rmdlFilePath = pak->GetAssetPath() + assetPath;

    char* const rmdlBuf = Model_ReadRMDLFile(rmdlFilePath);
    studiohdr_t* const studiohdr = reinterpret_cast<studiohdr_t*>(rmdlBuf);

    //
    // Physics
    //
    const bool physicsRequired = studiohdr->vphysize != 0;

    BinaryIO phyInput;
    const std::string physicsFile = Utils::ChangeExtension(rmdlFilePath, ".phy");

    if (phyInput.Open(physicsFile, BinaryIO::Mode_e::Read))
    {
        const size_t phyFileSize = phyInput.GetSize();

        // If it exists, but is 0, then the file is truncated/corrupt.
        // Still report the error even if physicsRequired is false as
        // this is an indication there's more wrong.
        if (!phyFileSize)
            Error("Physics file \"%s\" appears truncated.\n", physicsFile.c_str());

        if (physicsRequired && (studiohdr->vphysize != phyFileSize))
            Error("Physics file \"%s\" has a size of %zu, but the model expected a size of %zu.\n", physicsFile.c_str(), phyFileSize, (size_t)studiohdr->vphysize);

        // Permanent (SF_CPU, not SF_CPU|SF_TEMP): the dedi builds the model's
        // vcollide from this phy on an async load-callback job, and the transient
        // TEMP slab is freed before that job runs -> empty vcollide -> every
        // prop_physics fails "No physics object". Server collision data must persist.
        PakPageLump_s phyChunk = pak->CreatePageLump(phyFileSize, SF_CPU, 1);
        phyInput.Read(phyChunk.data, phyFileSize);

        pak->AddPointer(hdrChunk, offsetof(ModelAssetHeader_t, pPhyData), phyChunk, 0);
    }
    else if (physicsRequired)
        Error("Failed to open physics file \"%s\".\n", physicsFile.c_str());

    //
    // Starpak
    //
    PakStreamSetEntry_s streamedVg;

    const bool keepClientOnly = pak->IsFlagSet(PF_KEEP_CLIENT);

    if (keepClientOnly)
        Model_InternalAddVertexGroupData(pak, &hdrChunk, pHdr, studiohdr, rmdlFilePath, streamedVg);

    // the last chunk is the actual data chunk that contains the rmdl
    PakPageLump_s dataChunk = pak->CreatePageLump(studiohdr->length, SF_CPU, 64, rmdlBuf);
    pak->AddPointer(hdrChunk, offsetof(ModelAssetHeader_t, pData), dataChunk, 0);

    if (keepClientOnly)
        Model_InternalHandleMaterials(pak, mapEntry, asset, studiohdr, dataChunk);

    asset.InitAsset(hdrChunk.GetPointer(), sizeof(ModelAssetHeader_t), PagePtr_t::NullPtr(), RMDL_VERSION, AssetType::RMDL, streamedVg.streamOffset, streamedVg.streamIndex);
    asset.SetHeaderPointer(hdrChunk.data);

    pak->FinishAsset();
}

//=============================================================================
// mdl_ v17: ModelAssetHeader_v16_t + CPU page; .rmdl is the studiohdr_v17 blob.
// Optional .phy / .vg_static / .vg. Material GUIDs sit at studiohdr->textureindex.
//=============================================================================

static char* Model_ReadRMDLFile_v17(const std::string& path, size_t* const pFileSize, const uint64_t alignment = 64)
{
    BinaryIO modelFile;

    if (!modelFile.Open(path, BinaryIO::Mode_e::Read))
        Error("Failed to open model file \"%s\".\n", path.c_str());

    const size_t fileSize = modelFile.GetSize();

    if (fileSize < sizeof(studiohdr_v17_t))
        Error("Invalid v17 model file \"%s\"; must be at least %zu bytes, found %zu.\n", path.c_str(), sizeof(studiohdr_v17_t), fileSize);

    char* const buf = new char[IALIGN(fileSize, alignment)];
    modelFile.Read(buf, fileSize);

    *pFileSize = fileSize;
    return buf;
}

// Converted v19.1->v17 studiohdrs copy the LOD-group table verbatim, so they still declare
// compressed VG sizes while RSX's .vg is decompressed. Rewrite the table to the raw layout.
//
// Fix: rewrite every group to dataCompression=0, dataSizeCompressed=dataSizeDecompressed, and
// dataOffset = the cumulative sum of prior groups' dataSizeDecompressed (matching how the raw,
// concatenated .vg/.vg_static payload is actually laid out). Unconditional and origin-agnostic:
// a genuine S21-native model already satisfies this exactly, so every group is a byte-identical
// no-op there.
static void Model_FixStaleCompressedVgGroups(studiohdr_v17_t* const studiohdr, const size_t rmdlSize, const char* const assetPath)
{
    if (studiohdr->groupHeaderOffset == 0 || studiohdr->groupHeaderCount == 0)
        return;

    studio_hw_groupdata_t* const groups = studiohdr->pLODGroup(0);
    const size_t tableAbs = reinterpret_cast<char*>(groups) - reinterpret_cast<char*>(studiohdr);
    const size_t tableSize = static_cast<size_t>(studiohdr->groupHeaderCount) * sizeof(studio_hw_groupdata_t);

    if (tableAbs + tableSize > rmdlSize)
    {
        Warning("Model \"%s\" has an out-of-bounds LOD group table (offset %zu, count %u, filesize %zu); skipping VG compression fixup.\n",
            assetPath, tableAbs, studiohdr->groupHeaderCount, rmdlSize);
        return;
    }

    int runningOffset = 0;
    int patchedCount = 0;

    for (uint16_t i = 0; i < studiohdr->groupHeaderCount; ++i)
    {
        studio_hw_groupdata_t* const group = &groups[i];

        if (group->dataCompression != 0 || group->dataSizeCompressed != group->dataSizeDecompressed)
            patchedCount++;

        group->dataOffset = runningOffset;
        group->dataSizeCompressed = group->dataSizeDecompressed;
        group->dataCompression = 0;

        runningOffset += group->dataSizeDecompressed;
    }

    if (patchedCount > 0)
    {
        Warning("Model \"%s\": %d/%u LOD group(s) declared stale compressed VG offsets; rewritten to match the raw streamed payload.\n",
            assetPath, patchedCount, studiohdr->groupHeaderCount);
    }
}

void Assets::AddModelAsset_v17(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry)
{
    // anim refs come from the manifest entry (populated from the .rson sidecar):
    // $sequences auto-adds aseqs; $animrigs only creates the guid refs.
    uint32_t sequenceCount = 0;
    PakGuid_t* const sequenceRefs = AnimSeq_AutoAddSequenceRefs(pak, &sequenceCount, mapEntry);

    uint32_t animrigCount = 0;
    PakGuid_t* const animrigRefs = Model_AddAnimRigRefs(&animrigCount, mapEntry);

    PakAsset_t& asset = pak->BeginAsset(assetGuid, assetPath);

    PakPageLump_s hdrLump = pak->CreatePageLump(sizeof(ModelAssetHeader_v16_t), SF_HEAD, 8);
    ModelAssetHeader_v16_t* const pHdr = reinterpret_cast<ModelAssetHeader_v16_t*>(hdrLump.data);

    //
    // Name + anim rig/seq guid refs share one CPU lump. Name is 1-aligned, the
    // guid ref block is 8-aligned, so the name is padded out to 8 first.
    //
    {
        const size_t nameBufLen = strlen(assetPath) + 1;
        const size_t alignedNameBufLen = IALIGN8(nameBufLen);
        const size_t animRigRefsBufLen = animrigCount * sizeof(PakGuid_t);
        const size_t sequenceRefsBufLen = sequenceCount * sizeof(PakGuid_t);
        const bool hasGuidRefs = animrigRefs || sequenceRefs;

        PakPageLump_s nameLump = pak->CreatePageLump(alignedNameBufLen + animRigRefsBufLen + sequenceRefsBufLen, SF_CPU, hasGuidRefs ? 8 : 1);
        memcpy(nameLump.data, assetPath, nameBufLen);
        pak->AddPointer(hdrLump, offsetof(ModelAssetHeader_v16_t, pName), nameLump, 0);

        if (hasGuidRefs)
        {
            asset.ExpandGuidBuf(animrigCount + sequenceCount);

            if (animrigRefs)
            {
                const size_t base = alignedNameBufLen;
                memcpy(&nameLump.data[base], animrigRefs, animRigRefsBufLen);
                delete[] animrigRefs;

                pHdr->numAnimRigs = animrigCount;
                pak->AddPointer(hdrLump, offsetof(ModelAssetHeader_v16_t, pAnimRigs), nameLump, base);

                for (uint32_t i = 0; i < animrigCount; ++i)
                {
                    const size_t offset = base + (i * sizeof(PakGuid_t));
                    const PakGuid_t guid = *reinterpret_cast<PakGuid_t*>(&nameLump.data[offset]);
                    Pak_RegisterGuidRefAtOffset(guid, offset, nameLump, asset);
                }
            }

            if (sequenceRefs)
            {
                const size_t base = alignedNameBufLen + animRigRefsBufLen;
                memcpy(&nameLump.data[base], sequenceRefs, sequenceRefsBufLen);
                delete[] sequenceRefs;

                pHdr->numAnimSeqs = static_cast<uint16_t>(sequenceCount);
                pak->AddPointer(hdrLump, offsetof(ModelAssetHeader_v16_t, pSequences), nameLump, base);

                for (uint32_t i = 0; i < sequenceCount; ++i)
                {
                    const size_t offset = base + (i * sizeof(PakGuid_t));
                    const PakGuid_t guid = *reinterpret_cast<PakGuid_t*>(&nameLump.data[offset]);
                    Pak_RegisterGuidRefAtOffset(guid, offset, nameLump, asset);
                }
            }
        }
    }

    const std::string rmdlFilePath = pak->GetAssetPath() + assetPath;

    size_t rmdlSize = 0;
    char* const rmdlBuf = Model_ReadRMDLFile_v17(rmdlFilePath, &rmdlSize);
    studiohdr_v17_t* const studiohdr = reinterpret_cast<studiohdr_v17_t*>(rmdlBuf);

    Model_FixStaleCompressedVgGroups(studiohdr, rmdlSize, assetPath);

    // +0x30/+0x3C stay zero. S21 v17 ships them zero; the engine culls the studiohdr hull.
    // Newer versions store (min, EXTENT) here -- copying a max into +0x3C is read as a size.
    pHdr->bbox_min = Vector3(0.0f, 0.0f, 0.0f);
    pHdr->bbox_max = Vector3(0.0f, 0.0f, 0.0f);

    //
    // CPU page: physics pointer + sizes. Referenced via the asset's cpu pointer.
    //
    PakPageLump_s cpuLump = pak->CreatePageLump(sizeof(ModelAssetCPU_v16_t), SF_CPU, 8);
    ModelAssetCPU_v16_t* const pCpu = reinterpret_cast<ModelAssetCPU_v16_t*>(cpuLump.data);
    pCpu->dataSizeModel = static_cast<int>(rmdlSize);

    //
    // Physics (.phy)
    //
    BinaryIO phyInput;
    const std::string physicsFile = Utils::ChangeExtension(rmdlFilePath, ".phy");
    if (phyInput.Open(physicsFile, BinaryIO::Mode_e::Read))
    {
        const size_t phyFileSize = phyInput.GetSize();
        if (!phyFileSize)
            Error("Physics file \"%s\" appears truncated.\n", physicsFile.c_str());

        PakPageLump_s phyChunk = pak->CreatePageLump(phyFileSize, SF_CPU | SF_TEMP, 1);
        phyInput.Read(phyChunk.data, phyFileSize);

        pak->AddPointer(cpuLump, offsetof(ModelAssetCPU_v16_t, pPhysics), phyChunk, 0);
        pCpu->dataSizePhys = static_cast<int>(phyFileSize);
    }

    //
    // Vertex-group data. STATIC props bake a PERMANENT cache (.vg_static); ANIMATED/dynamic models
    // (no .vg_static) STREAM the .vg from the starpak. The previous writer only did the permanent
    // case + hardcoded InitAsset(-1,-1), so animated models had NO VG anywhere -> the runtime's
    // streamed-VG read failed: "FS_CheckAsyncRequest returned error for model ...".
    // NOTE: RSX exports .vg/.vg_static in a raw (non-"0tVG") format, so the bytes are streamed
    // verbatim (no GVt0 validation -- Model_ReadVGFile's magic check does NOT apply here; the
    // permanent path reads raw the same way). streamingDataSize(+0x2C) = raw VG size either way.
    //
    PakStreamSetEntry_s streamedVg;

    // Stream .vg for every model -- starpakOff=-1 fails FS_CheckAsyncRequest. Bytes are raw; do not run the GVt0 magic check.
    {
        const std::string vgFile = Utils::ChangeExtension(rmdlFilePath, ".vg");
        if (pak->TryReuseStreaming(assetGuid, &streamedVg, nullptr) && streamedVg.streamOffset >= 0)
        {
            BinaryIO vgInput;
            if (vgInput.Open(vgFile, BinaryIO::Mode_e::Read))
            {
                const size_t vgSize = vgInput.GetSize();
                if (vgSize)
                    pHdr->streamingDataSize = static_cast<uint32_t>(vgSize);
            }
        }
        else
        {
            BinaryIO vgInput;
            if (vgInput.Open(vgFile, BinaryIO::Mode_e::Read))
            {
                const size_t vgSize = vgInput.GetSize();
                if (vgSize)
                {
                    // align + zero-pad to the starpak block size so the dedup hash matches.
                    const size_t vgSizeAligned = IALIGN(vgSize, STARPAK_DATABLOCK_ALIGNMENT);
                    char* const vgBuf = new char[vgSizeAligned];
                    vgInput.Read(vgBuf, vgSize);
                    if (vgSizeAligned > vgSize)
                        memset(&vgBuf[vgSize], 0, vgSizeAligned - vgSize);
                    streamedVg = pak->AddStreamingDataEntry(vgSizeAligned, (uint8_t*)vgBuf, STREAMING_SET_MANDATORY);
                    pHdr->streamingDataSize = static_cast<uint32_t>(vgSize);
                    delete[] vgBuf;
                }
            }
        }
    }

    // Static props ALSO bake a PERMANENT .vg_static (pStaticPropVtxCache); it sets 0x2C to the
    // .vg_static size (matches kral's static-prop 0x2C byte-for-byte).
    BinaryIO vgStaticInput;
    const std::string vgStaticFile = Utils::ChangeExtension(rmdlFilePath, ".vg_static");
    if (vgStaticInput.Open(vgStaticFile, BinaryIO::Mode_e::Read))
    {
        const size_t vgStaticSize = vgStaticInput.GetSize();
        if (vgStaticSize)
        {
            PakPageLump_s vgLump = pak->CreatePageLump(vgStaticSize, SF_CPU | SF_TEMP | SF_CLIENT, 1);
            vgStaticInput.Read(vgLump.data, vgStaticSize);
            pak->AddPointer(hdrLump, offsetof(ModelAssetHeader_v16_t, pStaticPropVtxCache), vgLump, 0);
            pHdr->streamingDataSize = static_cast<uint32_t>(vgStaticSize);
        }
    }

    //
    // Data blob (.rmdl): the verbatim studiohdr_v17 buffer, 64-aligned because
    // collision (bvh) data is read with aligned SIMD.
    //
    PakPageLump_s dataLump = pak->CreatePageLump(rmdlSize, SF_CPU, 64, rmdlBuf);
    pak->AddPointer(hdrLump, offsetof(ModelAssetHeader_v16_t, pData), dataLump, 0);

    //
    // Register the model's material guids (inline u64 array in the data blob).
    //
    for (int i = 0; i < studiohdr->numtextures; ++i)
    {
        mstudiotexture_v16_t* const tex = studiohdr->pTexture(i);
        const size_t pos = reinterpret_cast<char*>(tex) - dataLump.data;
        const size_t offset = pos + offsetof(mstudiotexture_v16_t, guid);

        Pak_RegisterGuidRefAtOffset(tex->guid, offset, dataLump, asset);
    }

    asset.InitAsset(hdrLump.GetPointer(), sizeof(ModelAssetHeader_v16_t), cpuLump.GetPointer(), RMDL_VERSION_V17, AssetType::RMDL, streamedVg.streamOffset, streamedVg.streamIndex);
    asset.SetHeaderPointer(hdrLump.data);

    pak->FinishAsset();
}