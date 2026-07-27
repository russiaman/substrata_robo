/*=====================================================================
GaussianSplatZipReader.cpp
----------------------------
coded by AI agent under @russiaman supervision -
Generated at Mon Jul 27 06:16:15 2026
=====================================================================*/
#include "GaussianSplatZipReader.h"


#include <utils/Exception.h>
#include <cstring>


// Deliberately NOT defining WUFFS_IMPLEMENTATION here: graphics/PNGDecoder.cpp already includes this same amalgamated file with WUFFS_IMPLEMENTATION and
// WUFFS_CONFIG__MODULE__DEFLATE (needed for PNG's zlib-wrapped deflate streams) elsewhere in this same gui_client/server binary - the actual wuffs_deflate__decoder__*
// function bodies and error-message globals it emits have external linkage (there's no STATIC_FUNCTIONS opt-in that also covers those globals), so defining
// WUFFS_IMPLEMENTATION a second time in this TU produced duplicate-symbol link errors. Omitting it here just pulls in the *declarations* (prototypes, the
// wuffs_deflate__decoder type, and the alloc()-based C++ convenience wrapper - see its "#if !defined(WUFFS_IMPLEMENTATION)" branch) and links against the
// definitions PNGDecoder.cpp's translation unit already provides - the ordinary declare-in-many-places / define-in-one-place split, just achieved by a macro
// rather than a separate header, because wuffs ships as a single amalgamated .c file. This only works because WUFFS_SUPPORT=1 (and hence PNGDecoder.cpp's
// wuffs_deflate__decoder definitions) is unconditionally on for every translation unit in this project (see shared_cxx_settings.cmake).
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__DEFLATE
#include <graphics/wuffs/wuffs-v0.3.c>


namespace
{


uint16_t readU16LE(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t readU32LE(const uint8_t* p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }


const uint32_t END_OF_CENTRAL_DIR_SIG    = 0x06054b50;
const uint32_t CENTRAL_DIR_FILE_HDR_SIG  = 0x02014b50;
const uint32_t LOCAL_FILE_HDR_SIG        = 0x04034b50;

const uint16_t COMPRESSION_STORE   = 0;
const uint16_t COMPRESSION_DEFLATE = 8;


// Raw DEFLATE (RFC 1951), not zlib (RFC 1950) - ZIP entries store the deflate stream directly, with no zlib header/Adler-32 trailer.
std::vector<uint8_t> inflateRaw(const uint8_t* compressed_data, size_t compressed_size, size_t uncompressed_size, const std::string& filename_for_error_msg)
{
	// alloc() heap-allocates using the *real* struct size (known only in PNGDecoder.cpp's translation unit, where WUFFS_IMPLEMENTATION is defined) and already
	// calls initialize() internally - see wuffs_deflate__decoder__alloc()'s definition. This TU's own (WUFFS_IMPLEMENTATION-less) view of the struct's size is
	// deliberately not the real one (see the "dead_weight" comment in wuffs-v0.3.c), which is exactly why alloc() rather than a stack/local instance is used here.
	wuffs_deflate__decoder::unique_ptr decoder = wuffs_deflate__decoder::alloc();
	if(!decoder)
		throw glare::Exception("GaussianSplatZipReader: failed to allocate DEFLATE decoder for '" + filename_for_error_msg + "'.");

	wuffs_base__io_buffer src = wuffs_base__ptr_u8__reader(const_cast<uint8_t*>(compressed_data), compressed_size, /*closed=*/true);

	std::vector<uint8_t> dst_buf(uncompressed_size);
	wuffs_base__io_buffer dst = wuffs_base__ptr_u8__writer(dst_buf.data(), dst_buf.size());

	const wuffs_base__range_ii_u64 workbuf_range = decoder->workbuf_len();
	std::vector<uint8_t> workbuf(workbuf_range.max_incl);
	const wuffs_base__slice_u8 workbuf_slice = wuffs_base__make_slice_u8(workbuf.data(), workbuf.size());

	// Closed, fully-buffered source and an exactly-sized destination should complete in one transform_io call in practice, but loop on suspensions rather than assume that,
	// matching Wuffs' general usage pattern (see e.g. PNGDecoder.cpp's decode_frame loop).
	wuffs_base__status status;
	for(;;)
	{
		status = decoder->transform_io(&dst, &src, workbuf_slice);
		if(status.is_ok())
			break;
		if(!status.is_suspension())
			throw glare::Exception("GaussianSplatZipReader: DEFLATE decode error in '" + filename_for_error_msg + "': " + std::string(status.message()));
	}

	if(dst.meta.wi != uncompressed_size)
		throw glare::Exception("GaussianSplatZipReader: DEFLATE-decoded size mismatch for '" + filename_for_error_msg + "' (expected " +
			std::to_string(uncompressed_size) + " bytes, got " + std::to_string(dst.meta.wi) + ").");

	return dst_buf;
}


// Shared central-directory walk used by both readEntries() (wanted_filename == NULL, decode everything) and
// readEntry() (wanted_filename != NULL, decode only the matching entry and skip inflating anything else).
std::map<std::string, std::vector<uint8_t>> readEntriesImpl(const uint8_t* data, size_t size, const std::string* wanted_filename)
{
	if(size < 22)
		throw glare::Exception("GaussianSplatZipReader: buffer too small to be a valid ZIP file.");

	// Find the End Of Central Directory record by scanning backwards from the end of the buffer.
	// (The EOCD record is a fixed 22 bytes plus an optional comment of up to 65535 bytes, so we only need to scan the last ~64KB+22 bytes.)
	const size_t scan_start = (size > 22 + 65535) ? (size - 22 - 65535) : 0;
	size_t eocd_offset = 0;
	bool found_eocd = false;
	for(size_t i = size - 22; ; --i)
	{
		if(readU32LE(data + i) == END_OF_CENTRAL_DIR_SIG)
		{
			eocd_offset = i;
			found_eocd = true;
			break;
		}
		if(i == scan_start)
			break;
	}
	if(!found_eocd)
		throw glare::Exception("GaussianSplatZipReader: could not find End Of Central Directory record - not a valid ZIP file.");

	const uint16_t num_entries      = readU16LE(data + eocd_offset + 10);
	const uint32_t central_dir_size = readU32LE(data + eocd_offset + 12);
	const uint32_t central_dir_off  = readU32LE(data + eocd_offset + 16);

	if((size_t)central_dir_off + central_dir_size > size)
		throw glare::Exception("GaussianSplatZipReader: central directory offset/size out of range.");

	std::map<std::string, std::vector<uint8_t>> result;

	size_t cursor = central_dir_off;
	for(uint16_t entry_i = 0; entry_i < num_entries; ++entry_i)
	{
		if(cursor + 46 > size)
			throw glare::Exception("GaussianSplatZipReader: truncated central directory entry.");

		if(readU32LE(data + cursor) != CENTRAL_DIR_FILE_HDR_SIG)
			throw glare::Exception("GaussianSplatZipReader: bad central directory file header signature.");

		const uint16_t compression_method   = readU16LE(data + cursor + 10);
		const uint32_t compressed_size      = readU32LE(data + cursor + 20);
		const uint32_t uncompressed_size    = readU32LE(data + cursor + 24);
		const uint16_t filename_len         = readU16LE(data + cursor + 28);
		const uint16_t extra_len            = readU16LE(data + cursor + 30);
		const uint16_t comment_len          = readU16LE(data + cursor + 32);
		const uint32_t local_hdr_off        = readU32LE(data + cursor + 42);

		if(cursor + 46 + filename_len > size)
			throw glare::Exception("GaussianSplatZipReader: truncated filename in central directory entry.");

		const std::string filename((const char*)(data + cursor + 46), filename_len);

		cursor += 46 + filename_len + extra_len + comment_len;

		// Skip directory entries (filenames ending in '/') and entries with no data.
		if(!filename.empty() && filename.back() == '/')
			continue;

		// If we're only after one named entry, skip decoding (and even locating) any other entry entirely.
		if(wanted_filename && (filename != *wanted_filename))
			continue;

		// Now read the local file header to find the actual start of the file data
		// (the "extra" field length can differ between the central directory and local headers).
		if((size_t)local_hdr_off + 30 > size)
			throw glare::Exception("GaussianSplatZipReader: local file header offset out of range.");
		if(readU32LE(data + local_hdr_off) != LOCAL_FILE_HDR_SIG)
			throw glare::Exception("GaussianSplatZipReader: bad local file header signature.");

		const uint16_t local_filename_len = readU16LE(data + local_hdr_off + 26);
		const uint16_t local_extra_len    = readU16LE(data + local_hdr_off + 28);

		const size_t data_offset = local_hdr_off + 30 + local_filename_len + local_extra_len;
		if(data_offset + compressed_size > size)
			throw glare::Exception("GaussianSplatZipReader: entry data out of range for '" + filename + "'.");

		if(compression_method == COMPRESSION_STORE)
		{
			result[filename] = std::vector<uint8_t>(data + data_offset, data + data_offset + compressed_size);
		}
		else if(compression_method == COMPRESSION_DEFLATE)
		{
			result[filename] = inflateRaw(data + data_offset, compressed_size, uncompressed_size, filename);
		}
		else
		{
			throw glare::Exception("GaussianSplatZipReader: entry '" + filename + "' uses unsupported compression method " + std::to_string(compression_method) + ".");
		}

		if(wanted_filename)
			break; // Found the one entry we wanted, no need to keep scanning the central directory.
	}

	return result;
}


} // end anonymous namespace


std::map<std::string, std::vector<uint8_t>> GaussianSplatZipReader::readEntries(const uint8_t* data, size_t size)
{
	return readEntriesImpl(data, size, /*wanted_filename=*/nullptr);
}


std::vector<uint8_t> GaussianSplatZipReader::readEntry(const uint8_t* data, size_t size, const std::string& filename)
{
	const std::map<std::string, std::vector<uint8_t>> result = readEntriesImpl(data, size, &filename);
	const auto it = result.find(filename);
	if(it == result.end())
		throw glare::Exception("GaussianSplatZipReader: entry '" + filename + "' not found in ZIP.");
	return it->second;
}
