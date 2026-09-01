#pragma once

// Kraken wrappers for pak-level compression, matching the raw OodleLZ stream
// format the runtime decodes (PAK_HEADER_FLAGS_OODLE_ENCODED). Oodle itself is
// loaded from an oo2core DLL at runtime; see src/thirdparty/oodle/README.md.
namespace Oodle
{
	// Worst-case compressed buffer size for a raw input of rawLen bytes.
	// Returns 0 when no oo2core DLL could be loaded.
	size_t GetCompressedBufferSizeNeeded(const size_t rawLen);

	// Compresses rawLen bytes from src into dst (Kraken). level maps to
	// OodleLZ_CompressionLevel (0..9). backwardCompatMajor, if > 0, asks the
	// encoder for a stream readable by that older Oodle2 MAJOR version.
	// Returns the compressed byte count, or 0 on failure.
	size_t Compress(const uint8_t* const src, const size_t rawLen, uint8_t* const dst, const int level, const int backwardCompatMajor);

	// Decompresses compLen bytes from src into dst, expecting rawLen output bytes.
	// Returns true on success.
	bool Decompress(const uint8_t* const src, const size_t compLen, uint8_t* const dst, const size_t rawLen);
}
