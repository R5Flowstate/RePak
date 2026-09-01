#pragma once
#include "logic/pakfile.h"

// asset versions
#define PTCH_VERSION 1
#define ANIR_VERSION 1
#define TXTR_VERSION 8
#define TXAN_VERSION 1
#define TXLS_VERSION 1
#define UIMG_VERSION 10
#define RLCD_VERSION 0
//#define DTBL_VERSION 1
#define STLT_VERSION 0
#define STGS_VERSION 1
#define RMDL_VERSION 10
#define RMDL_VERSION_V17 17 // S21-native model asset version (v16+ header family)
#define ARIG_VERSION 4
#define ARIG_VERSION_V6 6 // S21-native
#define ASEQ_VERSION 7
#define ASEQ_VERSION_V11 11 // S21-native
#define TXTX_VERSION_V2 2 // S21-native
#define UIIA_VERSION_V2 2 // S21-native
#define UIIA_V2_HEADER_SIZE 64u
#define UIIA_FILE_MAGIC 0x41494955u // 'UIIA' (re-packable .uiia container)
#define RMAP_VERSION_V4 4 // S21-native
#define MATL_VERSION 15
#define MT4A_VERSION 3
#define RSON_VERSION 1
#define FONT_VERSION 7
#define WRAP_VERSION 7
#define EFCT_VERSION_V16 16 // S21-native particle effect (24-byte GUID-reference-graph subheader)

namespace Assets
{
	void AddPatchAsset(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);

	void AddAnimRecording_v1(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);

	void AddTextureAsset_v8(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddTextureAsset_v10(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddTextureAnimAsset_v1(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddTextureListAsset_v1(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);

	void AddMaterialAsset_v12(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddMaterialAsset_v15(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddMaterialAsset_v23(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);

	void AddMaterialForAspectAsset_v3(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);

	void AddUIImageAsset_v10(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddUIImageAtlasAsset_v2(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddUIImageAsset_v10_FromHex(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddLcdScreenEffect_v0(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);

	void AddDataTableAsset(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddSettingsLayout_v0(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddSettingsAsset_v1(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);

	void AddModelAsset_v9(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddModelAsset_v17(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);

	void AddAnimSeqAsset_v7(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddAnimSeqAsset_v11(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddAnimRigAsset_v4(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddAnimRigAsset_v6(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddTextureExtraAsset_v2(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddMapAsset_v4(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);

	void AddShaderSetAsset_v8(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddShaderSetAsset_v11(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddShaderSetAsset_v12(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddShaderAsset_v8(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddShaderAsset_v12(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
	void AddShaderAsset_v15(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);

	void AddRSONAsset_v1(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);

	void AddRuiAsset_v30(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);

	void AddFontAtlasAsset_v7(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);

	void AddWrapAsset_v7(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);

	void AddEffectAsset_v16(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry);
};
