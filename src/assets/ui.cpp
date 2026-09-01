#include "pch.h"
#include "assets.h"
#include "public/ui.h"
#include "public/rui_package.h"

void UI_loadFromPackage(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath)
{
	UNUSED(assetGuid);
	const fs::path inputFilePath = pak->GetAssetPath() / fs::path(assetPath).replace_extension("ruip");
	RuiPackage rui{ inputFilePath };

	PakAsset_t& asset = pak->BeginAsset(assetGuid, assetPath);
	PakPageLump_s hdrChunk = pak->CreatePageLump(sizeof(RuiHeader_v30_s), SF_HEAD | SF_CLIENT, 8);
	RuiHeader_v30_s* ruiHdr = reinterpret_cast<RuiHeader_v30_s*>(hdrChunk.data);
	*ruiHdr = rui.CreateRuiHeader_v30();

	// Pre-calculate the total CPU data size so all lumps land on the same page.
	// Section order: name, defaultValues, transform, styleDesc, renderJobs, argClusters, arguments, keyframings.
	{
		int totalCpuSize = IALIGN(static_cast<int>(rui.name.size()), 2);

		if (!rui.defaultData.empty() || !rui.defaultStrings.empty())
			totalCpuSize += static_cast<int>(rui.defaultData.size() + rui.defaultStrings.size());

		if (!rui.transformData.empty())
			totalCpuSize += static_cast<int>(rui.transformData.size());

		if (!rui.styleDescriptors.empty())
			totalCpuSize = IALIGN(totalCpuSize, 2) + IALIGN(static_cast<int>(rui.styleDescriptors.size()), 2);

		if (!rui.renderJobs.empty())
			totalCpuSize = IALIGN(totalCpuSize, 2) + IALIGN(static_cast<int>(rui.renderJobs.size()), 2);

		if (!rui.argCluster.empty())
			totalCpuSize = IALIGN(totalCpuSize, 2) + IALIGN(static_cast<int>(rui.argCluster.size() * sizeof(ArgCluster_s)), 2);

		if (!rui.arguments.empty())
			totalCpuSize = IALIGN(totalCpuSize, 2) + IALIGN(static_cast<int>(rui.arguments.size() * sizeof(Argument_s)), 2);

		const uint16_t keyframingCount = rui.hdr.keyframingCount;
		const size_t keyframingArraySize = keyframingCount * sizeof(UIAssetMapping_t);
		if (keyframingCount > 0 && rui.keyframings.size() >= keyframingArraySize)
			totalCpuSize = IALIGN(totalCpuSize, 8) + IALIGN(static_cast<int>(rui.keyframings.size()), 8);

		pak->EnsurePageCapacity(SF_CPU | SF_CLIENT, 8, totalCpuSize);
	}

	PakPageLump_s nameChunk = pak->CreatePageLump(rui.name.size(), SF_CPU | SF_CLIENT, 2);
	memcpy(nameChunk.data, rui.name.data(), rui.name.size());
	pak->AddPointer(hdrChunk, offsetof(RuiHeader_v30_s, name), nameChunk, 0);

	// Only create chunks for non-empty sections; engine expects NULL pointers for empty ones.

	if (!rui.defaultData.empty() || !rui.defaultStrings.empty())
	{
		PakPageLump_s defaultValuesChunk = pak->CreatePageLump(rui.defaultData.size() + rui.defaultStrings.size(), SF_CPU | SF_CLIENT, 1);
		memcpy(defaultValuesChunk.data, rui.defaultData.data(), rui.defaultData.size());
		memcpy(&defaultValuesChunk.data[rui.defaultData.size()], rui.defaultStrings.data(), rui.defaultStrings.size());
		for (uint16_t offset : rui.defaultStringOffsets) {
			// The raw 8-byte value at each string pointer slot is a defaultStrings-relative offset.
			// Adding defaultData.size() converts it to an offset within the combined chunk.
			uint64_t stringOffset = *reinterpret_cast<uint64_t*>(&rui.defaultData[offset]) + rui.defaultData.size();
			pak->AddPointer(defaultValuesChunk, offset, defaultValuesChunk, stringOffset);
		}
		// RUIP v2 generic pointerFixups (section 1 = this combined CPU blob).
		// Apply when defaultStringOffsets empty so we do not double-register.
		if (!rui.pointerFixups.empty() && rui.defaultStringOffsets.empty())
		{
			for (const RuiPointerFixup_t& fx : rui.pointerFixups)
			{
				if (fx.srcSection != 1 || fx.dstSection != 1)
					continue;
				if (fx.srcOffset + sizeof(uint64_t) > rui.defaultData.size())
					continue;
				const uint64_t stringOffset = static_cast<uint64_t>(fx.dstOffset) + rui.defaultData.size();
				if (stringOffset > rui.defaultData.size() + rui.defaultStrings.size())
					continue;
				pak->AddPointer(defaultValuesChunk, fx.srcOffset, defaultValuesChunk, stringOffset);
			}
		}
		pak->AddPointer(hdrChunk, offsetof(RuiHeader_v30_s, dataStructInitData), defaultValuesChunk, 0);
	}

	if (!rui.transformData.empty())
	{
		PakPageLump_s transformDataChunk = pak->CreatePageLump(rui.transformData.size(), SF_CPU | SF_CLIENT, 1);
		memcpy(transformDataChunk.data, rui.transformData.data(), rui.transformData.size());
		pak->AddPointer(hdrChunk, offsetof(RuiHeader_v30_s, transformData), transformDataChunk, 0);
	}

	ruiHdr->argNames = 0;

	if (!rui.styleDescriptors.empty())
	{
		PakPageLump_s styleDescriptorChunk = pak->CreatePageLump(rui.styleDescriptors.size(), SF_CPU | SF_CLIENT, 2);
		memcpy(styleDescriptorChunk.data, rui.styleDescriptors.data(), rui.styleDescriptors.size());
		pak->AddPointer(hdrChunk, offsetof(RuiHeader_v30_s, styleDescriptors), styleDescriptorChunk, 0);
	}

	if (!rui.renderJobs.empty())
	{
		PakPageLump_s renderJobChunk = pak->CreatePageLump(rui.renderJobs.size(), SF_CPU | SF_CLIENT, 2);
		memcpy(renderJobChunk.data, rui.renderJobs.data(), rui.renderJobs.size());
		pak->AddPointer(hdrChunk, offsetof(RuiHeader_v30_s, renderJobData), renderJobChunk, 0);
	}

	if (!rui.argCluster.empty())
	{
		PakPageLump_s argClustersChunk = pak->CreatePageLump(rui.argCluster.size() * sizeof(ArgCluster_s), SF_CPU | SF_CLIENT, 2);
		memcpy(argClustersChunk.data, rui.argCluster.data(), rui.argCluster.size() * sizeof(ArgCluster_s));
		pak->AddPointer(hdrChunk, offsetof(RuiHeader_v30_s, argClusters), argClustersChunk, 0);
	}

	if (!rui.arguments.empty())
	{
		PakPageLump_s argumentsChunk = pak->CreatePageLump(rui.arguments.size() * sizeof(Argument_s), SF_CPU | SF_CLIENT, 2);
		memcpy(argumentsChunk.data, rui.arguments.data(), rui.arguments.size() * sizeof(Argument_s));
		pak->AddPointer(hdrChunk, offsetof(RuiHeader_v30_s, arguments), argumentsChunk, 0);
	}

	// Keyframings: contiguous blob of [UIAssetMapping_t array][float data].
	// Each UIAssetMapping_t.data is a byte offset into this blob that needs a pointer fixup.
	const uint16_t keyframingCount = rui.hdr.keyframingCount;
	const size_t keyframingArraySize = keyframingCount * sizeof(UIAssetMapping_t);

	if (keyframingCount > 0 && rui.keyframings.size() >= keyframingArraySize)
	{
		PakPageLump_s keyframingsChunk = pak->CreatePageLump(rui.keyframings.size(), SF_CPU | SF_CLIENT, 8);
		memcpy(keyframingsChunk.data, rui.keyframings.data(), rui.keyframings.size());

		const UIAssetMapping_t* srcMappings = reinterpret_cast<const UIAssetMapping_t*>(rui.keyframings.data());

		for (uint16_t i = 0; i < keyframingCount; i++)
		{
			uintptr_t serializedOffset = reinterpret_cast<uintptr_t>(srcMappings[i].data);

			if (serializedOffset >= keyframingArraySize && serializedOffset < rui.keyframings.size())
			{
				pak->AddPointer(keyframingsChunk,
					i * sizeof(UIAssetMapping_t) + offsetof(UIAssetMapping_t, data),
					keyframingsChunk, serializedOffset);
			}
		}

		pak->AddPointer(hdrChunk, offsetof(RuiHeader_v30_s, keyframings), keyframingsChunk, 0);
	}

	asset.InitAsset(hdrChunk.GetPointer(), sizeof(RuiHeader_v30_s), PagePtr_t::NullPtr(), rui.hdr.ruiVersion, AssetType::UI);
	asset.SetHeaderPointer(hdrChunk.data);

	pak->FinishAsset();
}

void Assets::AddRuiAsset_v30(CPakFileBuilder* const pak, const PakGuid_t assetGuid, const char* const assetPath, const rapidjson::Value& mapEntry)
{
	UNUSED(mapEntry);
	UI_loadFromPackage(pak, assetGuid, assetPath);
}
