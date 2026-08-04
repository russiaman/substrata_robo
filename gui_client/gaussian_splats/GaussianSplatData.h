/*=====================================================================
GaussianSplatData.h
--------------------
coded by AI agent under @russiaman supervision -
Generated at Mon Jul 27 06:16:15 2026
=====================================================================*/
#pragma once


#include "GaussianSplatLodTree.h"
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

	// On-the-fly LoD tree (Claude_LOD_plan.md), built asynchronously from positions/scales/rotations/colours above by LoadModelTask right after this GaussianSplatData is decoded - see LoadModelTask.cpp's
	// .sog branch. Built once, in this same object space, so that GaussianSplatRenderer::updateObjectTransform() can re-bake every node (leaves and merged internal nodes alike) into world space the same way it
	// already does for individual leaf splats, rather than rebuilding the tree on every move (see buildGaussianSplatLodTree()'s declaration comment for why a merged node transforms identically to a leaf one
	// under a rigid + uniform-scale transform). Empty if the build hasn't happened yet, or failed (a missing tree isn't fatal to rendering the splat cloud - see LoadModelTask.cpp) - GaussianSplatRenderer must
	// treat empty the same as "not built yet, fall back to rendering every splat with no LoD" (stage 4, not implemented yet as of this comment).
	std::vector<GaussianSplatLodNode> lod_tree;
};


typedef Reference<GaussianSplatData> GaussianSplatDataRef;
