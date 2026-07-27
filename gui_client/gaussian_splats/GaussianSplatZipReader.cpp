/*=====================================================================
GaussianSplatZipReader.cpp
----------------------------
coded by AI agent under @russiaman supervision -
Generated at Mon Jul 27 06:16:15 2026
=====================================================================*/
#include "GaussianSplatZipReader.h"


#include <utils/Exception.h>
#include <cstring>


namespace
{


uint16_t readU16LE(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t readU32LE(const uint8_t* p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }


const uint32_t END_OF_CENTRAL_DIR_SIG    = 0x06054b50;
const uint32_t CENTRAL_DIR_FILE_HDR_SIG  = 0x02014b50;
const uint32_t LOCAL_FILE_HDR_SIG        = 0x04034b50;

const uint16_t COMPRESSION_STORE   = 0;
const uint16_t COMPRESSION_DEFLATE = 8;


} // end anonymous namespace


std::map<std::string, std::vector<uint8_t>> GaussianSplatZipReader::readEntries(const uint8_t* data, size_t size)
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
			// See class comment in the header: DEFLATE entries aren't handled yet.
			throw glare::Exception("GaussianSplatZipReader: entry '" + filename + "' uses DEFLATE compression, which isn't supported yet (only STORE is currently handled).");
		}
		else
		{
			throw glare::Exception("GaussianSplatZipReader: entry '" + filename + "' uses unsupported compression method " + std::to_string(compression_method) + ".");
		}

		(void)uncompressed_size;
	}

	return result;
}
