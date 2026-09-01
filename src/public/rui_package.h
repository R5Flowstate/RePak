#pragma once
#include "public/ui.h"

#define RUI_PACKAGE_MAGIC ('R' | ('U' << 8) | ('I' << 16) | ('P' << 24))
#define RUI_PACKAGE_VERSION 1
#define RUI_PACKAGE_VERSION_2 2

#pragma pack(push, 1)
struct RuiPackageHeader_v1_t {
	uint32_t magic;
	uint16_t packageVersion;
	uint16_t ruiVersion;
	uint64_t nameOffset;
	float elementWidth;
	float elementHeight;
	float elementWidthRcp;
	float elementHeightRcp;
	uint16_t defaultValuesSize;
	uint16_t dataStructSize;
	uint16_t styleDescriptorCount;
	uint16_t unk_A4;
	uint16_t renderJobCount;
	uint16_t argClusterCount;
	uint16_t argCount;
	uint16_t keyframingCount;
	uint16_t transformDataSize;
	uint16_t nameSize;
	uint16_t rpakPointersInDefaltDataCount;
	uint8_t pad[2];
	uint32_t argNamesSize;
	uint32_t renderJobSize;
	uint32_t keyframingSize;
	uint32_t defaultStringsSize;
	uint64_t argNamesOffset;
	uint64_t argClusterOffset;
	uint64_t argumentsOffset;
	uint64_t styleDescriptorOffset;
	uint64_t renderJobOffset;
	uint64_t keyframingOffset;
	uint64_t transformDataOffset;
	uint64_t defaultValuesOffset;
	uint64_t defaultStringDataOffset;
	uint64_t rpakPointersInDefaultDataOffset;
	uint64_t defaultStringsDataSize;
};

// Extended header with generic pointer fixups (backwards compatible with v1)
struct RuiPackageHeader_v2_t : public RuiPackageHeader_v1_t {
	uint32_t pointerFixupCount;
	uint64_t pointerFixupOffset;
};

// Pointer fixup entry: sections 0=head, 1=cpu
struct RuiPointerFixup_t {
	uint32_t srcSection;
	uint32_t srcOffset;
	uint32_t dstSection;
	uint32_t dstOffset;
};
#pragma pack(pop)

struct RuiPackage {
	RuiPackage(const fs::path& inputPath) {
		FILE* f = NULL;
		errno_t errorCode = fopen_s(&f, inputPath.string().c_str(), "rb");
		if (errorCode == 0) {
			fread(&hdr, sizeof(hdr), 1, f);
			if (hdr.magic != RUI_PACKAGE_MAGIC)
				Error("Attempted to load an invalid RUIP file (expected magic %x, got %x).\n", RUI_PACKAGE_MAGIC, hdr.magic);
			if (hdr.packageVersion != RUI_PACKAGE_VERSION && hdr.packageVersion != RUI_PACKAGE_VERSION_2)
				Error("Attempted to load an unsupported RUIP file (expected version %u or %u, got %u).\n",
					RUI_PACKAGE_VERSION, RUI_PACKAGE_VERSION_2, hdr.packageVersion);

			fseek(f, (long)hdr.nameOffset, 0);
			name.resize(hdr.nameSize);
			fread(name.data(), 1, hdr.nameSize, f);

			fseek(f, (long)hdr.defaultValuesOffset, 0);
			defaultData.resize(hdr.defaultValuesSize);
			fread(defaultData.data(), 1, hdr.defaultValuesSize, f);

			fseek(f, (long)hdr.defaultStringDataOffset, 0);
			defaultStrings.resize(hdr.defaultStringsDataSize);
			fread(defaultStrings.data(), 1, hdr.defaultStringsDataSize, f);

			fseek(f, (long)hdr.rpakPointersInDefaultDataOffset, 0);
			defaultStringOffsets.resize(hdr.rpakPointersInDefaltDataCount);
			fread(defaultStringOffsets.data(), sizeof(uint16_t), hdr.rpakPointersInDefaltDataCount, f);

			fseek(f, (long)hdr.styleDescriptorOffset, 0);
			// V39+ uses 68-byte descriptors, V30 uses 52-byte
			size_t styleDescSize = (hdr.ruiVersion >= 39) ? sizeof(StyleDescriptor_v39_s) : sizeof(StyleDescriptor_v30_s);
			styleDescriptors.resize(hdr.styleDescriptorCount * styleDescSize);
			fread(styleDescriptors.data(), styleDescSize, hdr.styleDescriptorCount, f);

			fseek(f, (long)hdr.renderJobOffset, 0);
			renderJobs.resize(hdr.renderJobSize);
			fread(renderJobs.data(), 1, hdr.renderJobSize, f);

			fseek(f, (long)hdr.transformDataOffset, 0);
			transformData.resize(hdr.transformDataSize);
			fread(transformData.data(), 1, hdr.transformDataSize, f);

			fseek(f, (long)hdr.keyframingOffset, 0);
			keyframings.resize(hdr.keyframingSize);
			fread(keyframings.data(), 1, hdr.keyframingSize, f);

			fseek(f, (long)hdr.argumentsOffset, 0);
			arguments.resize(hdr.argCount);
			fread(arguments.data(), sizeof(Argument_s), hdr.argCount, f);

			fseek(f, (long)hdr.argClusterOffset, 0);
			argCluster.resize(hdr.argClusterCount);
			fread(argCluster.data(), sizeof(ArgCluster_s), hdr.argClusterCount, f);

			// Read v2 extension: generic pointer fixups
			if (hdr.packageVersion >= RUI_PACKAGE_VERSION_2) {
				fseek(f, sizeof(RuiPackageHeader_v1_t), 0);
				uint32_t fixupCount = 0;
				uint64_t fixupOffset = 0;
				fread(&fixupCount, sizeof(uint32_t), 1, f);
				fread(&fixupOffset, sizeof(uint64_t), 1, f);

				if (fixupCount > 0 && fixupOffset > 0) {
					pointerFixups.resize(fixupCount);
					fseek(f, (long)fixupOffset, 0);
					fread(pointerFixups.data(), sizeof(RuiPointerFixup_t), fixupCount, f);
				}
			}

			fclose(f);
		}
		else {
			Error("Could not open ruip file %s with error %x", inputPath.string().c_str(), errorCode);
		}
	}

	RuiHeader_v30_s CreateRuiHeader_v30() {
		RuiHeader_v30_s ruiHdr{};
		ruiHdr.elementWidth = hdr.elementWidth;
		ruiHdr.elementHeight = hdr.elementHeight;
		ruiHdr.elementWidthRcp = hdr.elementWidthRcp;
		ruiHdr.elementHeightRcp = hdr.elementHeightRcp;
		ruiHdr.argumentCount = hdr.argCount;
		ruiHdr.keyframingCount = hdr.keyframingCount;
		ruiHdr.dataStructSize = hdr.dataStructSize;
		ruiHdr.dataStructInitSize = hdr.defaultValuesSize;
		ruiHdr.styleDescriptorCount = hdr.styleDescriptorCount;
		ruiHdr.maxTransformIndex = hdr.unk_A4;
		ruiHdr.renderJobCount = hdr.renderJobCount;
		ruiHdr.argClusterCount = hdr.argClusterCount;
		return ruiHdr;
	}

	RuiPackageHeader_v1_t hdr{};
	std::vector<char> name;
	std::vector<char> defaultData;
	std::vector<char> defaultStrings;
	std::vector<uint16_t> defaultStringOffsets;
	std::vector<char> transformData;
	std::vector<char> renderJobs;
	std::vector<char> keyframings;
	std::vector<Argument_s> arguments;
	std::vector<ArgCluster_s> argCluster;
	std::vector<char> styleDescriptors;
	std::vector<RuiPointerFixup_t> pointerFixups;
};
