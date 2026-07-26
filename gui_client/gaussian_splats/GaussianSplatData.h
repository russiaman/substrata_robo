/*=====================================================================
GaussianSplatData.h
--------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <maths/vec3.h>
#include <maths/Vec4f.h>
#include <physics/jscol_aabbox.h>
#include <utils/Reference.h>
#include <utils/RefCounted.h>
#include <vector>


/*=====================================================================
GaussianSplatData
------------------
CPU-side decoded Gaussian Splat cloud (positions, scales, rotations, colour+opacity),
as reconstructed from a SOG file. Not tied to any particular GPU texture layout -
that packing is done separately (see GaussianSplatRenderer).

See snapshots/2026-07-26_phase2-architecture-contract.md for how this fits into
the wider loading pipeline (opaque field on ModelLoadedThreadMessage).
=====================================================================*/
class GaussianSplatData : public RefCounted
{
public:
	size_t numSplats() const { return positions.size(); }

	std::vector<Vec3f> positions; // World-object space.
	std::vector<Vec3f> scales; // Linear scale factors (already exponentiated), one per axis.
	std::vector<Vec4f> rotations; // Unit quaternions, (x, y, z, w).
	std::vector<Vec4f> colours; // (r, g, b, opacity), all in [0, 1].

	js::AABBox aabb_os; // Bounding box in object space, over splat centres only (does not account for splat extents).
};


typedef Reference<GaussianSplatData> GaussianSplatDataRef;
