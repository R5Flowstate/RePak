#pragma once

#include <cstdint>
#include <unordered_set>

class CBuildSettings
{
public:
	CBuildSettings();

	void Init(const js::Document& doc, const char* const buildMapFile);

	inline void AddFlags(const int flags) { m_buildFlags |= flags; }
	inline bool IsFlagSet(const int flag) const { return m_buildFlags & flag; };

	inline int GetPakVersion() const { return m_pakVersion; }

	inline const char* GetBuildMapPath() const { return m_buildMapPath.c_str(); }
	inline const char* GetOutputPath() const { return m_outputPath.c_str(); }

	// Build-wide set of every asset guid declared across all paks in a build list.
	// Lets per-pak reference validation recognise cross-pak refs (e.g. a settings
	// asset in root_lgnd_skins referencing a model built into common_mp) instead of
	// flagging them as dangling. PakGuid_t == uint64_t.
	inline void AddGlobalKnownAsset(const uint64_t guid) { m_globalKnownAssets.insert(guid); }
	inline bool IsGlobalKnownAsset(const uint64_t guid) const { return m_globalKnownAssets.contains(guid); }

private:
	int m_pakVersion;
	int m_buildFlags;

	std::string m_buildMapPath;
	std::string m_outputPath;

	std::unordered_set<uint64_t> m_globalKnownAssets;
};
