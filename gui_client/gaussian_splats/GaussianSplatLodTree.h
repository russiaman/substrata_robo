/*=====================================================================
GaussianSplatLodTree.h
------------------------
coded by AI agent under @russiaman supervision -
Generated at Sun Aug  3 00:00:00 2026
=====================================================================*/
#pragma once


#include <maths/vec3.h>
#include <maths/Vec4f.h>
#include <utils/Platform.h>
#include <cstddef>
#include <vector>


/*=====================================================================
GaussianSplatLodTree
---------------------
CPU-side on-the-fly LoD tree for the Gaussian Splat world cloud - see
Claude_LOD_plan.md at the repo root for the overall design and staged
rollout this is part of. Stages 1 (node type + merge maths) and 2 (the
voxel-grid tree builder) live here; traversal (best-first frontier
selection, plan stage 5) and wiring into GaussianSplatRenderer (plan
stages 3/4/6) are separate, not-yet-written stages - see the plan for
the full sequence.

Modelled on Spark's (sparkjs.dev) Tiny-LoD approach: a bottom-up
voxel-grid merge builds a hierarchy of "virtual" splats, each a
statistically-faithful stand-in for the splats below it, so that at
render time a much smaller "frontier" of nodes (large/coarse ones
standing in for many small ones where the camera is far away or the
budget is tight) can represent the full splat count while staying
visually close to the un-thinned cloud.

Chunk layout (see plan §1.2): node indices are meant to be chunk-encoded
- (chunk_id << 16) | local_index, chunks of 65536 nodes - from the very
first tree the builder produces, even though nothing in this repo yet
implements paging/eviction of those chunks (the MVP always keeps every
chunk resident). Getting the index space chunk-shaped now is what makes
adding real paging later an addition instead of a re-layout - see the
plan's "Full-scale" section. This file doesn't itself encode/decode that
scheme (it has no notion of chunks yet, since it doesn't build trees) -
it's mentioned here because child_start's eventual meaning depends on it.

Merge maths (mergeGaussianSplatLodNodes(), see the .cpp): weighted
mean centre and weighted (Kerbl-et-al.-style) covariance merge, decomposed
back to scale+rotation via a small self-contained symmetric 3x3 Jacobi
eigensolver (no existing utility for this in glare-core's maths/ - Imath's
jacobiEigenSolver is vendored only for OpenEXR's own internal use, and
using its Imath::Matrix33/Vec3<T> types here would mean an awkward second
set of maths types alongside Vec3f/Vec4f/Matrix4f for no benefit).

Documented simplification (see plan §2 and §7): opacity is plain [0, 1],
clamped at merge time (Spark's extended "D parameter" opacity model - a
sharper-than-Gaussian falloff for densely-merged nodes instead of a hard
clamp - is deferred as a fast-follow once tree build + traversal work
end-to-end, not implemented here yet).
=====================================================================*/


// One node of the LoD tree - either an original ("leaf") splat, or a merged stand-in for child_count children starting at child_start.
struct GaussianSplatLodNode
{
	Vec3f centre_ws; // World-space centre once wired into the renderer - matches GaussianSplatRenderer::world_positions' convention. buildGaussianSplatLodTree() itself is transform-agnostic though (see its declaration comment) - during tree *construction* this holds whatever space the input splats were given in (object space, at load time), and the eventual caller re-bakes it to world space the same way GaussianSplatRenderer::addObject() already does per leaf splat, since a merged node transforms identically to a leaf one under a rigid + uniform-scale transform.
	Vec3f scale; // Linear scale factors (already exponentiated), one per axis - matches GaussianSplatData::scales' convention.
	Vec4f rotation; // Unit quaternion (x, y, z, w) - matches GaussianSplatData::rotations' convention.
	Vec4f colour; // (r, g, b, opacity), all in [0, 1] - matches GaussianSplatData::colours' convention. See the file header comment re: the deferred D-parameter opacity model.
	float feature_size; // 2 * max(scale.x, scale.y, scale.z) - the "how big does this node look" metric the (not yet written) traversal stage will prioritise nodes by.

	uint32 child_start; // Meant to become a chunk-encoded index of the first child once the (not yet written) tree builder exists - see file header comment. Always 0 on a node fresh out of mergeGaussianSplatLodNodes()/makeGaussianSplatLodLeafNode(); the tree builder is what actually places nodes into the linearised array and fills this in.
	uint16 child_count; // 0 = leaf (an original, unmerged splat). Always 0 on a node fresh out of mergeGaussianSplatLodNodes()/makeGaussianSplatLodLeafNode(), for the same reason as child_start.
};


// Builds a leaf node directly from one splat's un-merged attributes (e.g. from GaussianSplatData / the world cloud arrays). child_count = 0, feature_size computed from scale.
GaussianSplatLodNode makeGaussianSplatLodLeafNode(const Vec3f& centre_ws, const Vec3f& scale, const Vec4f& rotation, const Vec4f& colour);

// Merges 'num_children' nodes (leaves, already-merged nodes, or a mix) into one parent node's attributes - see the .cpp for the maths. Only centre_ws/scale/rotation/colour/feature_size are set on the result;
// child_start/child_count are left at 0 - the (not yet written) tree builder, not this function, knows where the returned node will end up living in the linearised array. num_children must be >= 1.
GaussianSplatLodNode mergeGaussianSplatLodNodes(const GaussianSplatLodNode* children, size_t num_children);

// Builds a complete LoD tree from a flat splat cloud (Tiny-LoD style: bottom-up voxel-grid merge - see the .cpp for the algorithm). Transform-agnostic (see the centre_ws field comment above) - operates in
// whichever space 'centres' is given in, doesn't know or care about world transforms.
//
// Returns the tree linearised into a single array: the result's [0] is always the root; every node's children occupy the contiguous range [child_start, child_start + child_count) later in the array (see the
// file header comment's chunk-layout note - indices here are plain array indices, NOT yet chunk-encoded, since chunking only matters once nodes start living in separate GPU-resident pages, which nothing does
// yet - see plan §8).
//
// num_splats must be >= 1. lod_base is the grid-step growth factor between levels (> 1) - Spark's default for this method is 1.5 (a non-integer base gives smoother LoD transitions than doubling - see plan §3).
std::vector<GaussianSplatLodNode> buildGaussianSplatLodTree(const Vec3f* centres, const Vec3f* scales, const Vec4f* rotations, const Vec4f* colours, size_t num_splats, float lod_base = 1.5f);


namespace GaussianSplatLodTreeTests
{
	void test();
}
