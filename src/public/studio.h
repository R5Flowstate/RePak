#pragma once
#include "math/vector.h"
#include "material.h"

#pragma pack(push, 1)
// used for referencing a material from within a model
// pathoffset is the offset to the material's path (duh)
// guid is the material's asset guid (or 0 if it's a vmt, i think)
struct mstudiotexture_t
{
	uint32_t pathoffset;
	PakGuid_t guid;
};

// modified source engine studio mdl header struct
struct studiohdr_t
{
	int id; // Model format ID, such as "IDST" (0x49 0x44 0x53 0x54)
	int version; // Format version number, such as 48 (0x30,0x00,0x00,0x00)
	int checksum; // This has to be the same in the phy and vtx files to load!
	int sznameindex; // This has been moved from studiohdr2 to the front of the main header.
	char name[64]; // The internal name of the model, padding with null bytes.
	// Typically "my_model.mdl" will have an internal name of "my_model"
	int length; // Data size of MDL file in bytes.

	Vector3 eyeposition;	// ideal eye position

	Vector3 illumposition;	// illumination center

	Vector3 hull_min;		// ideal movement hull size
	Vector3 hull_max;

	Vector3 view_bbmin;		// clipping bounding box
	Vector3 view_bbmax;

	int flags;

	inline bool IsStaticProp() { return flags & 0x10; };

	int numbones; // bones
	int boneindex;

	int numbonecontrollers; // bone controllers
	int bonecontrollerindex;

	int numhitboxsets;
	int hitboxsetindex;

	int numlocalanim; // animations/poses
	int localanimindex; // animation descriptions

	int numlocalseq; // sequences
	int	localseqindex;

	int activitylistversion; // initialization flag - have the sequences been indexed?

	// mstudiotexture_t
	// short rpak path
	// raw textures
	int materialtypesindex;
	int numtextures; // the material limit exceeds 128, probably 256.
	int textureindex;

	inline mstudiotexture_t* pTexture(int i)
	{
		return reinterpret_cast<mstudiotexture_t*>((char*)this + textureindex) + i;
	}

	inline MaterialShaderType_e materialType(int i)
	{
		return reinterpret_cast<MaterialShaderType_e*>((char*)this + materialtypesindex)[i];
	}

	// this should always only be one, unless using vmts.
	// raw textures search paths
	int numcdtextures;
	int cdtextureindex;

	// replaceable textures tables
	int numskinref;
	int numskinfamilies;
	int skinindex;

	int numbodyparts;
	int bodypartindex;

	int numlocalattachments;
	int localattachmentindex;

	int numlocalnodes;
	int localnodeindex;
	int localnodenameindex;

	int numflexdesc;
	int flexdescindex;

	int meshindex; // SubmeshLodsOffset, might just be a mess offset

	int numflexcontrollers;
	int flexcontrollerindex;

	int numflexrules;
	int flexruleindex;

	int numikchains;
	int ikchainindex;

	// this is rui meshes
	int numruimeshes;
	int ruimeshindex;

	int numlocalposeparameters;
	int localposeparamindex;

	int surfacepropindex;

	int keyvalueindex;
	int keyvaluesize;

	int numlocalikautoplaylocks;
	int localikautoplaylockindex;

	float mass;
	int contents;

	// unused for packed models
	int numincludemodels;
	int includemodelindex;

	uint32_t virtualModel;

	int bonetablebynameindex;

	// if STUDIOHDR_FLAGS_CONSTANT_DIRECTIONAL_LIGHT_DOT is set,
	// this value is used to calculate directional components of lighting 
	// on static props
	byte constdirectionallightdot;

	// set during load of mdl data to track *desired* lod configuration (not actual)
	// the *actual* clamped root lod is found in studiohwdata
	// this is stored here as a global store to ensure the staged loading matches the rendering
	byte rootLOD;

	// set in the mdl data to specify that lod configuration should only allow first numAllowRootLODs
	// to be set as root LOD:
	//	numAllowedRootLODs = 0	means no restriction, any lod can be set as root lod.
	//	numAllowedRootLODs = N	means that lod0 - lod(N-1) can be set as root lod, but not lodN or lower.
	byte numAllowedRootLODs;

	byte unused;

	float fadedistance;

	float gathersize; // what. from r5r struct

	int numunk_v54_early;
	int unkindex_v54_early;

	int unk_v54[2];

	// this is in all shipped models, probably part of their asset bakery. it should be 0x2CC.
	int mayaindex; // doesn't actually need to be written pretty sure, only four bytes when not present.

	int numsrcbonetransform;
	int srcbonetransformindex;

	int	illumpositionattachmentindex;

	int linearboneindex;

	int m_nBoneFlexDriverCount; // unsure if that's what it is in apex
	int m_nBoneFlexDriverIndex;

	int unk1_v54[7];

	// always "" or "Titan"
	int unkstringindex;

	// this is now used for combined files in rpak, vtx, vvd, and vvc are all combined while vphy is separate.
	// the indexes are added to the offset in the rpak mdl_ header.
	// vphy isn't vphy, looks like a heavily modified vphy.
	int vtxindex; // VTX
	int vvdindex; // VVD / IDSV
	int vvcindex; // VVC / IDCV 
	int vphyindex; // VPHY / IVPS

	int vtxsize; // mesh strip data size
	int vvdsize; // vertex data data size
	int vvcsize; // vertex color data size
	int vphysize; // still used in models using vg

	// unk2_v54[3] is the chunk after following unkindex2's chunk
	int unk2_v54[3]; // the same four unks in v53 I think, the first index being unused now probably

	int unkindex3; // index to chunk after string block

	// likely related to AABB
	Vector3 mins; // min/max for Something
	Vector3 maxs; // seem to be the same as hull size

	int unk3_v54[3];

	int bvh4index; // chunk before unkindex3 sometimes

	short unk4_v54[2]; // same as unk3_v54_v121

	// new in apex for verts that have more than three weights
	int vvwindex; // vertex weight 
	int vvwsize;
};

struct mstudioevent_t
{
	float cycle;
	int	event;
	int type; // this will be 0 if old style I'd imagine
	char options[256];
	int szeventindex;
};

struct mstudioseqdesc_t
{
	inline const mstudioevent_t* pEvent(int i) const
	{
		return reinterpret_cast<const mstudioevent_t*>((char*)this + eventindex) + i;
	}

	int baseptr;

	int	szlabelindex;

	int szactivitynameindex;

	int flags; // looping/non-looping flags

	int activity; // initialized at loadtime to game DLL values
	int actweight;

	int numevents;
	int eventindex;

	Vector3 bbmin; // per sequence bounding box
	Vector3 bbmax;

	int numblends;

	// Index into array of shorts which is groupsize[0] x groupsize[1] in length
	int animindexindex;

	int movementindex; // [blend] float array for blended movement
	int groupsize[2];
	int paramindex[2]; // X, Y, Z, XR, YR, ZR
	float paramstart[2]; // local (0..1) starting value
	float paramend[2]; // local (0..1) ending value
	int paramparent;

	float fadeintime; // ideal cross fate in time (0.2 default)
	float fadeouttime; // ideal cross fade out time (0.2 default)

	int localentrynode; // transition node at entry
	int localexitnode; // transition node at exit
	int nodeflags; // transition rules

	float entryphase; // used to match entry gait
	float exitphase; // used to match exit gait

	float lastframe; // frame that should generation EndOfSequence

	int nextseq; // auto advancing sequences
	int pose; // index of delta animation between end and nextseq

	int numikrules;

	int numautolayers;
	int autolayerindex;

	int weightlistindex;

	int posekeyindex;

	int numiklocks;
	int iklockindex;

	// Key values
	int keyvalueindex;
	int keyvaluesize;

	int cycleposeindex; // index of pose parameter to use as cycle index

	int activitymodifierindex;
	int numactivitymodifiers;

	int unk;
	int unk1;

	int unkindex;

	int unk2;
};

struct mstudioautolayer_t
{
	PakGuid_t guid; // hashed aseq guid asset

	short iSequence;
	short iPose;

	int flags;
	float start; // beginning of influence
	float peak;	 // start of full influence
	float tail;	 // end of full influence
	float end;	 // end of all influence
};

struct AnimSeqAssetHeader_t
{
	PagePtr_t data; // pointer to raw rseq.
	PagePtr_t szname; // pointer to debug name, placed before raw rseq normally.

	// this can point to a group of guids and not one singular one.
	PagePtr_t pModels;
	uint32_t modelCount;

	uint32_t padding_0; // aligns next member to 8 bytes

	PagePtr_t pSettings; // points to an array of settings asset guids
	uint32_t settingsCount;

	uint32_t padding_1; // aligns full struct to 8 bytes
};

// S21 animseq header (64 bytes). Same 64B v11/v12 layout
// (subheaderSize=64; only data/szname relocated for dep-less seqs). v11 differs from
// v7 (48B): the dep pointers moved to +0x20/0x28/0x30 with u16 counts, and an
// effectAssets pointer + runtime fields were added.
struct AnimSeqAssetHeader_v11_t
{
	PagePtr_t data;   // +0x00 raw rseq (seqdesc)
	PagePtr_t szname; // +0x08 debug name (placed before the rseq blob)
	PagePtr_t rt;     // +0x10 runtime (null on disk)

	uint16_t numModels;         // +0x18 numPropModels
	uint16_t numAnimWindows;    // +0x1A
	uint32_t streamableDataSize;// +0x1C

	PagePtr_t pModels;           // +0x20 propModels (GUID array)
	PagePtr_t pEffects;          // +0x28 effectAssets (GUID array)
	PagePtr_t pSettings;         // +0x30 animWindowSettings (GUID array)
	PagePtr_t streamableDataTempMem; // +0x38 runtime (null on disk)
};
static_assert(sizeof(AnimSeqAssetHeader_v11_t) == 64);

// v16 "compressed" studio seqdesc, used by S21 v11 aseq .rseq blobs. Unlike the
// v7/v8 mstudioseqdesc_t (all 32-bit indices), v16 packs offsets into uint16 fields
// expanded via this packing: even values are byte offsets as-is; an odd value v
// means ((v & 0xFFFE) << 4). Taken from RSX studio_r5_v16.h.
static inline int Studio_FixOffset_v16(const uint16_t off)
{
	return static_cast<int>(static_cast<int>(off & 0xFFFE) << (4 * (off & 1)));
}

struct mstudioevent_v16_t
{
	float cycle;            // +0x00
	int event;             // +0x04
	int type;              // +0x08
	int unk_C;             // +0x0C
	uint16_t optionsindex; // +0x10 -> options string via FIX_OFFSET
	uint16_t szeventindex; // +0x12
};
static_assert(sizeof(mstudioevent_v16_t) == 0x14);

struct mstudioautolayer_v8_t
{
	PakGuid_t sequence; // +0x00 hashed aseq guid -> needs a guid descriptor
	int iPose;          // +0x08
	int flags;          // +0x0C
	float start;        // +0x10
	float peak;         // +0x14
	float tail;         // +0x18
	float end;          // +0x1C
};
static_assert(sizeof(mstudioautolayer_v8_t) == 0x20);

struct mstudioseqdesc_v16_t
{
	uint16_t szlabelindex;          // +0x00
	uint16_t szactivitynameindex;   // +0x02
	int flags;                      // +0x04
	uint16_t activity;              // +0x08
	uint16_t actweight;             // +0x0A
	uint16_t numevents;             // +0x0C
	uint16_t eventindex;            // +0x0E
	Vector3 bbmin;                  // +0x10
	Vector3 bbmax;                  // +0x1C
	uint16_t numblends;             // +0x28
	uint16_t animindexindex;        // +0x2A
	short paramindex[2];            // +0x2C
	float paramstart[2];            // +0x30
	float paramend[2];              // +0x38
	float fadeintime;               // +0x40
	float fadeouttime;              // +0x44
	uint16_t localentrynode;        // +0x48
	uint16_t localexitnode;         // +0x4A
	uint16_t numikrules;            // +0x4C
	uint16_t numautolayers;         // +0x4E
	uint16_t autolayerindex;        // +0x50
	uint16_t weightlistindex;       // +0x52
	uint8_t groupsize[2];           // +0x54
	uint16_t posekeyindex;          // +0x56
	uint16_t numiklocks;            // +0x58
	uint16_t iklockindex;           // +0x5A
	uint16_t unk_5C;                // +0x5C
	uint16_t cycleposeindex;        // +0x5E
	uint16_t activitymodifierindex; // +0x60
	uint16_t numactivitymodifiers;  // +0x62
	int ikResetMask;                // +0x64
	int unk_68;                     // +0x68
	uint16_t weightFixupOffset;     // +0x6C
	uint16_t weightFixupCount;      // +0x6E

	inline const mstudioevent_v16_t* pEvent(const int i) const
	{
		return reinterpret_cast<const mstudioevent_v16_t*>(reinterpret_cast<const char*>(this) + Studio_FixOffset_v16(eventindex)) + i;
	}
	inline const char* pEventOptions(const mstudioevent_v16_t* const ev) const
	{
		return reinterpret_cast<const char*>(ev) + Studio_FixOffset_v16(ev->optionsindex);
	}
	inline const mstudioautolayer_v8_t* pAutoLayer(const int i) const
	{
		return reinterpret_cast<const mstudioautolayer_v8_t*>(reinterpret_cast<const char*>(this) + Studio_FixOffset_v16(autolayerindex)) + i;
	}
};
static_assert(sizeof(mstudioseqdesc_v16_t) == 0x70);

// size: 0x78 (120 bytes)
struct ModelAssetHeader_t
{
	// IDST data
	// .rmdl
	PagePtr_t pData;
	uint64_t Padding = 0;

	// model path
	// e.g. mdl/vehicle/goblin_dropship/goblin_dropship.rmdl
	PagePtr_t pName;
	uint64_t Padding2 = 0;

	// .phy
	PagePtr_t pPhyData;
	uint64_t Padding3 = 0;

	// preload cache data for static props
	PagePtr_t pStaticPropVtxCache;

	// pointer to data for the model's arig guid(s?)
	PagePtr_t pAnimRigs;

	// this is a guess based on the above ptr's data. i think this is == to the number of guids at where the ptr points to
	uint32_t animRigCount = 0;

	// size of the data kept in starpak
	uint32_t totalVertexDataSize = 0; // full size of the vtx, vvd, vvc and vvw combined.
	uint32_t streamedVertexDataSize = 0; // full size of the starpak entry, aligned to 4096.

	uint64_t Padding6 = 0;
	uint64_t Padding7 = 0;
	uint64_t Padding8 = 0;

	// number of anim sequences directly associated with this model
	uint32_t sequenceCount = 0;
	PagePtr_t pSequences;

	uint64_t Padding9 = 0;
};
static_assert(sizeof(ModelAssetHeader_t) == 120);

//-----------------------------------------------------------------------------
// Apex Season 16+ model family (used by mdl_ v16/v17/v18/v19). The pak-asset
// header is the SAME for all of them (ModelAssetHeader_v16_t); only the embedded
// .rmdl studiohdr differs (studiohdr_v16_t vs studiohdr_v17_t). S21 = v17.
//-----------------------------------------------------------------------------

// v16+ studiohdr offsets are compact uint16's; this expands them. The LSB is a
// x16 scale flag: even -> literal offset, odd -> (o & 0xFFFE) << 4.
#define STUDIO_FIX_OFFSET(o) (static_cast<int>(static_cast<int>((o) & 0xFFFE) << (4 * ((o) & 1))))

// in v16+, a model's "texture" entry is just the material's asset guid.
struct mstudiotexture_v16_t
{
	PakGuid_t guid;
};

// Per-LOD-group streaming descriptor inside a v17 studiohdr's groupHeaderOffset table.
// The S21 client walks this table with a 16-byte stride.
// rmdlconv's own copy of this struct (studio_r5_v16.h/v19.h) declares dataCompression
// as a 4-byte int, making it 20 bytes -- that definition is wrong; it is never used to
// reinterpret table entries (rmdlconv only byte-copies the region), so it never corrupted
// output, but do NOT reuse it as a reference for this struct.
struct studio_hw_groupdata_t
{
	int dataOffset;				// +0x00 offset into the model's streamed .vg data
	int dataSizeCompressed;		// +0x04 -> becomes the FS_CheckAsyncRequest "count"
	int dataSizeDecompressed;		// +0x08
	uint8_t dataCompression;		// +0x0C compressionType_t; 0 == raw/uncompressed
	uint8_t lodIndex;				// +0x0D
	uint8_t lodCount;				// +0x0E
	uint8_t lodMap;					// +0x0F
};
static_assert(sizeof(studio_hw_groupdata_t) == 16);

// Embedded .rmdl header for v17 (the model `data` blob begins with this). Only
// the leading fields needed by the packer are declared; everything past
// textureindex stays opaque in the verbatim blob. Offsets verified vs RSX
// (studio_r5_v16.h studiohdr_v17_t) and real S21 district rmdl bytes.
struct studiohdr_v17_t
{
	int flags;						// +0x00
	int checksum;					// +0x04
	uint16_t sznameindex;			// +0x08
	char name[33];					// +0x0A
	uint8_t surfacepropLookup;		// +0x2B
	float mass;						// +0x2C
	int contents;					// +0x30
	uint16_t hitboxsetindex;		// +0x34
	uint8_t numhitboxsets;			// +0x36
	uint8_t illumpositionattachmentindex; // +0x37
	Vector3 illumposition;			// +0x38
	Vector3 hull_min;				// +0x44
	Vector3 hull_max;				// +0x50
	Vector3 view_bbmin;				// +0x5C
	Vector3 view_bbmax;				// +0x68
	uint16_t boneCount;				// +0x74
	uint16_t boneHdrOffset;			// +0x76
	uint16_t boneDataOffset;		// +0x78
	uint16_t numlocalseq;			// +0x7A
	uint16_t localseqindex;			// +0x7C
	uint16_t unk_7E[2];				// +0x7E
	char activitylistversion;		// +0x82
	uint8_t numlocalattachments;	// +0x83
	uint16_t localattachmentindex;	// +0x84
	uint16_t numlocalnodes;			// +0x86
	uint16_t localnodenameindex;	// +0x88
	uint16_t localNodeDataOffset;	// +0x8A
	uint16_t numikchains;			// +0x8C
	uint16_t ikchainindex;			// +0x8E
	uint16_t numtextures;			// +0x90
	uint16_t textureindex;			// +0x92

	inline mstudiotexture_v16_t* pTexture(int i)
	{
		return reinterpret_cast<mstudiotexture_v16_t*>((char*)this + STUDIO_FIX_OFFSET(textureindex)) + i;
	}

	uint16_t numskinref;			// +0x94
	uint16_t numskinfamilies;		// +0x96
	uint16_t skinindex;				// +0x98
	uint16_t numbodyparts;			// +0x9A
	uint16_t bodypartindex;			// +0x9C
	uint16_t uiPanelCount;			// +0x9E
	uint16_t uiPanelOffset;			// +0xA0
	uint16_t numlocalposeparameters; // +0xA2
	uint16_t localposeparamindex;	// +0xA4
	uint16_t surfacepropindex;		// +0xA6
	uint16_t keyvalueindex;			// +0xA8
	uint16_t virtualModel;			// +0xAA
	uint16_t meshCount;				// +0xAC
	uint16_t bonetablebynameindex;	// +0xAE
	uint16_t boneStateOffset;		// +0xB0
	uint16_t boneStateCount;		// +0xB2
	uint16_t groupHeaderOffset;		// +0xB4 -> studio_hw_groupdata_t[groupHeaderCount], stride 16
	uint16_t groupHeaderCount;		// +0xB6

	inline studio_hw_groupdata_t* pLODGroup(int i)
	{
		// FIELD-RELATIVE: groupHeaderOffset is measured from the field's OWN position
		// (offsetof + FIX_OFFSET), not the header base. Matches rmdlconv's v170
		// accessor (studio_r5_v16.h pLODGroup). The bare STUDIO_FIX_OFFSET (header-base)
		// form pointed 0xB4 bytes too low, so Model_FixStaleCompressedVgGroups patched
		// a garbage region and left the stale compressed group table intact -> the
		// engine issued a dataSizeCompressed-length read against raw VG bytes ->
		// "FS_CheckAsyncRequest returned error" streaming fatal.
		// (textureindex, by contrast, IS header-base -- do not unify these two.)
		return reinterpret_cast<studio_hw_groupdata_t*>((char*)this + offsetof(studiohdr_v17_t, groupHeaderOffset) + STUDIO_FIX_OFFSET(groupHeaderOffset)) + i;
	}
};
static_assert(offsetof(studiohdr_v17_t, view_bbmin) == 0x5C);
static_assert(offsetof(studiohdr_v17_t, numtextures) == 0x90);
static_assert(offsetof(studiohdr_v17_t, textureindex) == 0x92);
static_assert(offsetof(studiohdr_v17_t, groupHeaderOffset) == 0xB4);
static_assert(offsetof(studiohdr_v17_t, groupHeaderCount) == 0xB6);

// size: 0x60 (96 bytes). pak-asset header for v16/v17/v18/v19.
struct ModelAssetHeader_v16_t
{
	PagePtr_t pData;				// +0x00 -> studiohdr/rmdl blob
	PagePtr_t pName;				// +0x08 -> model path string
	char gap_10[8];					// +0x10
	PagePtr_t pStaticPropVtxCache;	// +0x18 -> baked (permanent) VG for static props
	PagePtr_t pAnimRigs;			// +0x20 -> arig guid array
	uint32_t numAnimRigs;			// +0x28
	uint32_t streamingDataSize;		// +0x2C -> size of VG data post-baking
	Vector3 bbox_min;				// +0x30
	Vector3 bbox_max;				// +0x3C
	uint16_t gap_48;				// +0x48
	uint16_t numAnimSeqs;			// +0x4A
	char gap_4C[4];					// +0x4C
	PagePtr_t pSequences;			// +0x50 -> aseq guid array
	char gap_58[8];					// +0x58
};
static_assert(sizeof(ModelAssetHeader_v16_t) == 96);

// CPU-page data for v16+ models (referenced via the asset's cpu pointer).
struct ModelAssetCPU_v16_t
{
	PagePtr_t pPhysics;				// +0x00 -> .phy data
	int dataSizePhys;				// +0x08
	int dataSizeModel;				// +0x0C
};
static_assert(sizeof(ModelAssetCPU_v16_t) == 16);

struct VertexGroupHeader_t
{
	int id;		        // 0x47567430	'0tVG'
	int version;	    // 0x1
	int unk;	        // Usually 0
	int dataSize;	    // Total size of data + header in starpak

	__int64 boneStateChangeOffset; // offset to bone remap buffer
	__int64 numBoneStateChanges;   // number of "bone remaps" (size: 1)

	__int64 meshOffset;            // offset to mesh buffer
	__int64 numMeshes;             // number of meshes (size: 0x48)

	__int64 indexOffset;           // offset to index buffer
	__int64 numIndices;            // number of indices (size: 2 (uint16_t))

	__int64 vertOffset;            // offset to vertex buffer
	__int64 vertDataSize;          // number of bytes in vertex buffer

	__int64 externalWeightOffset;  // offset to extended weights buffer
	__int64 externalWeightsSize;   // number of bytes in extended weights buffer

	// there is one for every LOD mesh
	// i.e, unknownCount == lod.meshCount for all LODs
	__int64 unknownOffset;         // offset to buffer
	__int64 numUnknown;            // count (size: 0x30)

	__int64 lodOffset;             // offset to LOD buffer
	__int64 numLODs;               // number of LODs (size: 0x8)

	__int64 legacyWeightOffset;	   // seems to be an offset into the "external weights" buffer for this mesh
	__int64 numLegacyWeights;      // seems to be the number of "external weights" that this mesh uses

	__int64 stripOffset;           // offset to strips buffer
	__int64 numStrips;             // number of strips (size: 0x23)

	int unused[16];
};
#pragma pack(pop)