/*=====================================================================
GaussianSplatLoader.h
-----------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "GaussianSplatData.h"
#include <cstdint>
#include <vector>


/*=====================================================================
GaussianSplatLoader
---------------------
Loads a SOG (PlayCanvas "Spatially Ordered Gaussians") splat file into a
GaussianSplatData. Handles the bundled (.sog = ZIP of meta.json + .webp files)
layout - see GaussianSplatZipReader for the ZIP-container side.

Format spec: https://developer.playcanvas.com/user-manual/gaussian-splatting/formats/sog/
See snapshots/2026-07-26_phase2-architecture-contract.md §4.7 for how this
format was chosen and what it depends on.

Not handled yet (see architecture contract §6.3 - documented, not silent):
 - Unbundled layout (loose meta.json + .webp files) - we only support the
   single-file bundled .sog container, which is what the resource_manager /
   LoadModelTask pipeline expects (one URL per object).
 - Higher-order spherical harmonics (shN) - only the DC term (sh0) is used,
   per architecture contract §2.G decision to skip view-dependent colour
   in the first version.
=====================================================================*/
class GaussianSplatLoader
{
public:
	// data/size is the raw contents of a .sog file (a ZIP archive). Throws glare::Exception on failure.
	static GaussianSplatDataRef loadFromBuffer(const uint8_t* data, size_t size);
};
