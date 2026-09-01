#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

// Simple arena allocator for temporary allocations
// Designed for fast allocation and bulk deallocation
class ScratchAllocator
{
public:
	static constexpr size_t DEFAULT_BLOCK_SIZE = 256 * 1024; // 256 KB

	ScratchAllocator(size_t blockSize = DEFAULT_BLOCK_SIZE);
	~ScratchAllocator();

	// Allocate memory from the scratch buffer
	void* Allocate(size_t size, size_t alignment = 8);

	// Reset all allocations (keeps the memory blocks)
	void Reset();

	// Free all memory blocks
	void Clear();

private:
	struct Block
	{
		std::vector<uint8_t> data;
		size_t used;
	};

	std::vector<Block> m_blocks;
	size_t m_blockSize;
	size_t m_currentBlock;

	// Prevent copying
	ScratchAllocator(const ScratchAllocator&) = delete;
	ScratchAllocator& operator=(const ScratchAllocator&) = delete;
};

// Global scratch allocator instance
ScratchAllocator& GetGlobalScratch();
