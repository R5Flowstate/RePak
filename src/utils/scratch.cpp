#include "pch.h"
#include "scratch.h"

ScratchAllocator::ScratchAllocator(size_t blockSize)
	: m_blockSize(blockSize)
	, m_currentBlock(0)
{
	// Allocate first block
	m_blocks.push_back(Block());
	m_blocks.back().data.resize(m_blockSize);
	m_blocks.back().used = 0;
}

ScratchAllocator::~ScratchAllocator()
{
	Clear();
}

void* ScratchAllocator::Allocate(size_t size, size_t alignment)
{
	// Align size
	size = (size + alignment - 1) & ~(alignment - 1);

	// Check if current block has enough space
	while (m_currentBlock < m_blocks.size())
	{
		Block& block = m_blocks[m_currentBlock];
		size_t alignedOffset = (block.used + alignment - 1) & ~(alignment - 1);

		if (alignedOffset + size <= block.data.size())
		{
			void* ptr = &block.data[alignedOffset];
			block.used = alignedOffset + size;
			return ptr;
		}

		// Try next block
		m_currentBlock++;
	}

	// Need to allocate a new block
	size_t newBlockSize = m_blockSize;
	if (size > newBlockSize)
		newBlockSize = size;

	m_blocks.push_back(Block());
	m_blocks.back().data.resize(newBlockSize);
	m_blocks.back().used = size;

	return &m_blocks.back().data[0];
}

void ScratchAllocator::Reset()
{
	// Reset all blocks to unused state
	for (auto& block : m_blocks)
		block.used = 0;
	m_currentBlock = 0;
}

void ScratchAllocator::Clear()
{
	m_blocks.clear();
	m_currentBlock = 0;
}

// Global instance
static ScratchAllocator g_globalScratch(1024 * 1024); // 1 MB default

ScratchAllocator& GetGlobalScratch()
{
	return g_globalScratch;
}
