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
list out of a SOG bundle. Supports the STORE (uncompressed) method fully.
DEFLATE-compressed entries currently throw (see readEntries()) - re-compressing
already-compressed WebP payloads with DEFLATE saves essentially nothing, so
STORE is what SOG-writing tools are expected to use in practice, but this is
a known gap, not a silent limitation. If we hit real-world .sog files using
DEFLATE, the fix is to reuse the already-vendored wuffs DEFLATE module
(glare-core/graphics/wuffs/wuffs-v0.3.c) from this translation unit with its
own WUFFS_CONFIG__MODULE__DEFLATE define, following the pattern in
graphics/PNGDecoder.cpp.
=====================================================================*/
class GaussianSplatZipReader
{
public:
	// Throws glare::Exception on failure (malformed zip, or an entry using an unsupported compression method).
	static std::map<std::string, std::vector<uint8_t>> readEntries(const uint8_t* data, size_t size);
};
