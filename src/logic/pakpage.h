#pragma once
#include "public/rpak.h"

// Maximum number of slabs that can be allocated into the runtime collection.
#define PAK_MAX_SLAB_COUNT 20

// Pages can only be merged with other pages with equal flags and an alignment
// equal or higher than its own if the combined size aligned to the page's
// new alignment is below this value.
#define PAK_MAX_PAGE_MERGE_SIZE 0xffff

// A piece of data that belongs to the pak page, if PakPageLump_s::data is null
// the lump will be treated as alignment padding.
struct PakPageLump_s
{
	void Release()
	{
		if (data)
		{
			delete[] data;
			data = nullptr;
		}
	}

	// Gets the pointer to the page buffer at offset.
	inline PagePtr_t GetPointer(size_t offset = 0) const { return { pageInfo.index, static_cast<int>(pageInfo.offset + offset) }; };

	char* data;
	int size;
	int alignment;

	PagePtr_t pageInfo;
};

// A page of data, all lumps are aligned to the lump's alignment. The
// alignment of the page is equal to the page's lump with the highest
// alignment.
struct PakPage_s
{
	bool operator<(const PakPage_s& a) const
	{
		return header.slabIndex < a.header.slabIndex;
	}

	int index;
	int flags;
	PakPageHdr_s header;

	// Asset epoch of the most recent lump placed on this page (see
	// CPakPageBuilder::m_assetEpoch). Used to keep header (SF_HEAD) pages
	// contiguous in asset-add order: an append-only header page may only be
	// reused while it is still the "open" page of the current asset run.
	int lastEpoch = -1;

	// ordered list of data chunks belonging to this page.
	std::vector<PakPageLump_s> lumps;
};

// A large piece of memory in which all pages matching the alignment and flags
// of the slab reside. The alignment of the slab is equal to the slab's page
// with the highest alignment.
struct PakSlab_s
{
	int index;
	PakSlabHdr_s header;
};

class CPakPageBuilder
{
public:
	CPakPageBuilder();
	~CPakPageBuilder();

	inline uint16_t GetSlabCount() const { return m_slabCount; }
	inline uint16_t GetPageCount() const { return static_cast<uint16_t>(m_pages.size()); }

	// Advance the asset epoch; called once per asset (CPakFileBuilder::BeginAsset).
	// Header pages track the last epoch that touched them so they are never
	// reused after the asset run that owns them has ended (keeps each header
	// page's assets contiguous in the asset table -> required by the runtime
	// pak-load compaction's single monotonic descriptor cursor).
	inline void BeginAssetEpoch() { ++m_assetEpoch; }

	const PakPageLump_s CreatePageLump(const int size, const int flags, const int align, void* const buf = nullptr);
	void EnsurePageCapacity(const int flags, const int align, const int requiredSize);

	void PadSlabSizeForPageAlignment();

	void WriteSlabHeaders(BinaryIO& out) const;
	void WritePageHeaders(BinaryIO& out) const;
	void WritePageData(BinaryIO& out) const;

private:
	PakSlab_s& FindOrCreateSlab(const int flags, const int align);
	PakPage_s& FindOrCreatePage(const int flags, const int align, const int size);

private:
	std::array<PakSlab_s, PAK_MAX_SLAB_COUNT> m_slabs;
	uint16_t m_slabCount;

	// Monotonic per-asset counter (advanced by BeginAssetEpoch). Used only to
	// keep append-only header pages contiguous in asset-add order.
	int m_assetEpoch = 0;

	std::vector<PakPage_s> m_pages;
};
