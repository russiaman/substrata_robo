/*=====================================================================
GaussianSplatZipReader.h
-------------------------
coded by AI agent under @russiaman supervision -
Generated at Mon Jul 27 06:16:15 2026
=====================================================================*/
#pragma once


#include <cstdint>
#include <map>
#include <string>
#include <vector>


/*=====================================================================
GaussianSplatZipReader
-----------------------
Minimal ZIP central-directory reader for bundled .sog files (a bundled SOG
is just a ZIP of meta.json + several .webp files, per the PlayCanvas SOG spec:
https://developer.playcanvas.com/user-manual/gaussian-splatting/formats/sog/).

Not a general-purpose ZIP implementation - just enough to read the flat file
list out of a SOG bundle. Supports both STORE (uncompressed) and DEFLATE
entries - some real-world SOG-writing tools do use DEFLATE (e.g. LichtFeld
Studio), even though it buys little for already-compressed WebP payloads.
DEFLATE decoding reuses the already-vendored wuffs DEFLATE module
(glare-core/graphics/wuffs/wuffs-v0.3.c, see GaussianSplatZipReader.cpp),
included from this translation unit with its own WUFFS_CONFIG__MODULE__DEFLATE
define and WUFFS_CONFIG__STATIC_FUNCTIONS (so its symbols stay local to this
TU rather than clashing with graphics/PNGDecoder.cpp's own separate inclusion
of the same amalgamated file for PNG/zlib decoding) - the same general
approach PNGDecoder.cpp uses, just with a different, independently-configured
module set and linkage.
=====================================================================*/
class GaussianSplatZipReader
{
public:
	// Throws glare::Exception on failure (malformed zip, or an entry using an unsupported compression method).
	static std::map<std::string, std::vector<uint8_t>> readEntries(const uint8_t* data, size_t size);

	// Returns the decompressed contents of a single named entry, without decompressing any other entry in the zip.
	// Throws glare::Exception if the zip is malformed, the entry uses an unsupported compression method, or no entry
	// with that exact filename exists. Useful for reading just meta.json out of a bundle without paying for the
	// (much larger) WebP payloads - see GaussianSplatLoader::readMetaSummaryFromBuffer().
	static std::vector<uint8_t> readEntry(const uint8_t* data, size_t size, const std::string& filename);
};
