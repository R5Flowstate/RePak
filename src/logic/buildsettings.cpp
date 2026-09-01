//=============================================================================//
//
// Pak build settings manager
//
//=============================================================================//
#include "pch.h"
#include "utils/utils.h"

#include "buildsettings.h"

CBuildSettings::CBuildSettings()
{
	m_pakVersion = 0;
	m_buildFlags = 0;
}

void CBuildSettings::Init(const js::Document& doc, const char* const buildMapFile)
{
	m_pakVersion = JSON_GetValueOrDefault(doc, "version", -1);

	if (m_pakVersion < 0)
		Error("No \"version\" field provided.\n");

	m_buildMapPath = buildMapFile;

	// Determine final build path from map file.
	m_outputPath = JSON_GetValueRequired<const char*>(doc, "outputDir");

	Utils::AppendSlash(m_outputPath);
	Utils::ResolvePath(m_outputPath, m_buildMapPath);

	// Create output directory if it does not exist yet.
	fs::create_directories(m_outputPath);

	// S3 dedicated-server pak: route mdl_/aseq/arig to the S3 asset versions
	// (v10/v7/v4). A dedi server pak carries no client-only data (vertex groups,
	// materials, textures), so client-only scope defaults off for a dedi build.
	const bool isDedi = JSON_GetValueOrDefault(doc, "dedi", false);

	if (isDedi)
		AddFlags(PF_DEDI);

	// Should dev-only data be kept - e.g. texture asset names, shader names, etc.
	if (JSON_GetValueOrDefault(doc, "keepDevOnly", false))
		AddFlags(PF_KEEP_DEV);

	if (JSON_GetValueOrDefault(doc, "keepServerOnly", true))
		AddFlags(PF_KEEP_SERVER);

	if (JSON_GetValueOrDefault(doc, "keepClientOnly", !isDedi))
		AddFlags(PF_KEEP_CLIENT);

	g_showDebugLogs = JSON_GetValueOrDefault(doc, "showDebugInfo", false);
}
