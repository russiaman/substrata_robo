/*=====================================================================
GaussianSplatRenderer.h
------------------------
coded by AI agent under @russiaman supervision -
Generated at Mon Jul 27 06:16:15 2026
=====================================================================*/
#pragma once


#include "GaussianSplatData.h"
#include "../../shared/UID.h"
#include <opengl/OpenGLEngine.h>
#include <opengl/OpenGLProgram.h>
#include <maths/Quat.h>
#include <physics/jscol_aabbox.h>
#include <utils/Platform.h>
#include <utils/Reference.h>
#include <utils/ThreadSafeQueue.h>
#include <utils/ThreadMessage.h>
#include <string>
#include <vector>

namespace glare { class TaskManager; }
class GaussianSplatSortScratch; // Defined in GaussianSplatRenderer.cpp - the reusable working buffers the background depth-sort needs, see GaussianSplatRenderer::sort_scratch.
class GaussianSplatLodTraversalScratch; // Defined in GaussianSplatRenderer.cpp - the reusable working buffers the background LoD traversal needs (Claude_LOD_plan.md stage 5), see GaussianSplatRenderer::traversal_scratch.


/*=====================================================================
GaussianSplatRenderer
----------------------
Renders every Gaussian Splat cloud in the world as ONE app-level OpenGL
object - "the world splat cloud" - drawn through the engine's normal
transparent-object pass (see snapshots/2026-07-26_phase2-architecture-contract.md
§2.G). See snapshots/2026-07-28-session020-cross-object-merge-abandoned-unified-world-space-plan.md
for why: an earlier design gave each splat WorldObject its own GLObject and
merged their depth-sorted lists at draw time to get correct cross-object
occlusion, but that k-way merge was either too slow (140k+ draw calls/frame)
or, once optimised by only interleaving AABB-"contested" pairs, silently
wrong (two objects whose 3D bounds don't overlap can still overlap on
screen - see that snapshot's root-cause section). Putting every splat into
one shared world-space cloud sidesteps the problem entirely: there is no
"cross-object" left to merge, because there is only one object. Each
splat WorldObject just owns a [offset, count) range within it (addObject()/
removeObject()/updateObjectTransform() below), and ob->opengl_engine_ob for
every one of them points at the SAME shared GLObject returned by addObject().

GPU representation, geometry, and the CPU depth-sort itself are otherwise
unchanged from the original per-object design - see the "GPU representation"
and "CPU depth-sort" paragraphs this replaces in git history (session6/7/8/16)
for the full rationale, still accurate:
 - all splat attributes (position, scale, rotation, colour+opacity) are
   packed into a single RGBA32F texture, 4 texels per splat, unpacked with
   texelFetch() in the vertex shader (a single packed texture, rather than
   e.g. one texture per attribute, is required by the engine's custom-shader
   draw path, which only binds material.albedo_texture for app-supplied
   shaders - architecture contract §4.1);
 - geometry is one instanced quad, draw order controlled by a dedicated
   per-splat uint32 index VBO ("splat_index_in", forced to attribute
   location 1, see makeShaders()), not gl_InstanceID directly;
 - the CPU depth-sort sorts by distance from the camera (not forward-axis
   depth - see resort_move_threshold_ws's comment in the .cpp for why),
   in two worker-thread stages (fast coarse counting-sort posted first,
   precise radix sort posted right after), reusing scratch buffers across
   sorts (sort_scratch).

What's new here vs. the per-object design: positions/scales/rotations
baked into WORLD space (not object space) at add time, one growable set of
CPU arrays and one growable GPU texture/VBO pair for the whole world (see
"Growable GPU buffers" below), and a WorldSplatEntry registry recording
which [offset, count) range within those shared arrays belongs to which
WorldObject, so a single object can be re-baked (move/scale) or removed
(erased + the rest rebuilt) without disturbing anyone else's data.

Capacity: there is a hard ceiling on the WORLD's total splat count (not
per-object any more), computed by the same maxSupportedSplats() formula as
before - callers must check numSplatsInWorld() + <new file's count> against
it before calling addObject() (see GUIClient::createGaussianSplatObjectFromLocalFile()).
Below that ceiling, GPU storage grows on demand (see below), so an empty or
small world doesn't pay for a texture sized for the ceiling.

Growable GPU buffers: the data texture and instance-index VBO are sized to
the current splat count, not the world ceiling. When a new addObject() call
needs more room than the current texture height provides, a new, bigger
OpenGLTexture (and a matching, bigger instance-index VBO) is allocated, ALL
current splats are repacked/reuploaded into it, and it replaces the old one
(same idea as std::vector's doubling growth) - see ensureGpuCapacity() in
the .cpp. This only happens on the (rare) frames a growth threshold is
crossed; ordinary addObject() calls that fit in existing capacity do a
partial texture upload (OpenGLTexture::loadRegionIntoExistingTexture(),
just the newly-appended texel rows) instead of repacking everything.

Concurrency note for the background sort: the sort worker thread must never
read the live, growable world_positions/etc. vectors directly, since the
main thread can resize/reallocate them (on the next addObject()) while the
worker is still running. Instead, think() copies the current positions into
a snapshot buffer (GaussianSplatSortScratch::positions_snapshot) synchronously,
on the main thread, at the moment it kicks off a new sort task - the worker
only ever touches that frozen snapshot. An append (addObject() that doesn't
trigger a growth reallocation) happening after a sort was kicked off is safe
without any extra bookkeeping: the in-flight sort's result only covers the
snapshot's (smaller, prefix) splat range, and applying it via a VBO update of
just that many bytes leaves the newly-appended tail (already written in
identity order by addObject() itself) untouched. Only removeObject() actually
invalidates in-flight results - it renumbers/repacks the whole world, so
existing indices stop meaning the same splats - which is what
structure_generation is for: every removeObject() bumps it, every sort
result carries the generation it was computed against, and think() drops any
result whose generation doesn't match current on arrival (same "stale result,
just drop it" idiom the original per-object design used, generalised from
"object id no longer found" to "generation moved on").

On-the-fly LoD (Claude_LOD_plan.md, stage 4): when a splat WorldObject's object_space_data->lod_tree has been built (async, by LoadModelTask - see GaussianSplatData.h), addObject() bakes and uploads
EVERY node of that tree (leaves AND merged internal nodes) into the shared world_* arrays/texture, not just the leaf splats - each object's nodes occupy a stable [offset, offset+count) range exactly like
the leaf-only case did before, so a node's GLOBAL index (usable as a texelFetch/instance index) is always entry.offset + <the node's own index within object_space_data->lod_tree>. A splat object whose tree
hasn't been built yet (or failed to build - not fatal, see GaussianSplatData.h) still gets its plain leaf list baked/uploaded exactly as before - see the has_tree branches in addObject()/updateObjectTransform().
What's actually DRAWN is a separate concern from what's uploaded - see rebuildCurrentInstanceIndices()/current_instance_indices. Stage 4 wired up GPU upload of every node and a synchronous placeholder
"root only" selection (still used as the very first frame's content for a newly-added object, before its first real traversal result lands - see addObject()). Stage 5 (implemented) adds the real thing:
GaussianSplatLodTraversalTask (background worker, same TaskManager as the depth-sort) does a best-first priority-queue walk per Spark's Tiny-LoD approach (plan §4) - starting from every entry's root,
repeatedly expanding whichever frontier node currently subtends the most screen pixels (pixel_scale = feature_size / distance * focal_px) until either the worst-offending node is already small enough
(<= ~1px) or expanding it would exceed the splat budget, at which point the whole remaining frontier (across ALL entries, in ONE combined heap/output - see §4a and the concurrency note below) becomes
the new current_instance_indices, exactly like a fresh addObject() would set it, which in turn forces a fresh depth-sort of that selection (have_last_sort_cam_pos = false) - the sort itself needed zero
changes for this, since positions_snapshot/index_snapshot already generalised from "the whole world" to "whatever's currently selected" back in stage 4. The frontier arrives from the worker thread already
sorted farthest-first (GaussianSplatLodTraversalTask::run() does this itself, using the same positions_snapshot it walked the tree with) - not just heap-pop order - so it's never drawn unsorted even for
one frame; an earlier version sorted it on the main thread instead, right when applied here, which was correct but visibly dropped FPS on a large (500K+ selected node) scene - see that function's comment.

Concurrency note for the traversal (mirrors the depth-sort's, above, but wider): the worker must never touch the live world_positions/world_scales vectors OR any entry's object_space_data->lod_tree
topology while the main thread could be mutating them. Unlike the sort - whose input set is already fixed by the time it's kicked off (current_instance_indices) - traversal *discovers* which nodes it
needs to look at as it walks the heap, so it can't pre-copy just the nodes it'll touch; instead it snapshots the WHOLE world_positions/world_scales arrays (a Reference-kept copy of the per-entry
{offset, object_space_data} list too, which keeps every touched entry's lod_tree alive via GaussianSplatDataRef's ref-counting regardless of what removeObject() does to the live `entries` vector
meanwhile) - see GaussianSplatLodTraversalScratch in the .cpp. Staleness on arrival is checked against topology_generation, NOT structure_generation: unlike the sort (which only cares that indices still
mean the same splat, something only removeObject() can break), a stale traversal result is wrong the moment ANY entry is added or removed (a just-added object's root wouldn't be in an older traversal's
frontier at all, and applying that older result would make it vanish until the next cycle) - see topology_generation's own comment for why addObject() bumps it too, unlike structure_generation.

Not handled yet (see architecture contract §6.3 - documented, not silent):
 - Order-independent transparency (an optional native-only engine feature,
   not present in the web build regardless - see the .cpp's fragment shader
   notes carried over from the per-object version).
 - Multiple GPU buckets / texture arrays if the world ever needs more splats
   than a single texture can address - explicitly deferred, see
   snapshots/2026-07-28-session020-*.md's "Ограничение вместимости" section
   for why (owner's call: don't design for it before it's a real problem).
=====================================================================*/
class GaussianSplatRenderer
{
public:
	GaussianSplatRenderer();
	~GaussianSplatRenderer(); // Defined in the .cpp, not defaulted here, because state holds a Reference<> to a type only defined there.

	// Builds the shared shader program used by the world splat cloud. Call once, after the OpenGL context exists (mirrors GUIClient::makeShaders() for parcel/portal).
	void makeShaders(OpenGLEngine& opengl_engine, const std::string& shader_dir);

	// Bakes object_space_data's positions/scales/rotations into world space using the given pose (translation/rotation/uniform scale - non-uniform scale isn't
	// supported, see known limitations in snapshots/2026-07-28-session020-*.md), and appends the result as a new range in the shared world splat cloud.
	// world_object_id identifies the owning WorldObject, for later updateObjectTransform()/removeObject() calls and for isSplatObject().
	// Lazily creates and opengl_engine.addObject()s the shared world GLObject on the very first call ever made on this renderer; every call (including the
	// first) returns that same GLObject - callers should assign it to WorldObject::opengl_engine_ob same as for any other object type, and must NOT
	// opengl_engine.addObject() it themselves (already done here) or opengl_engine.removeObject() it directly (see removeObject() below).
	// Caller is responsible for checking numSplatsInWorld() + object_space_data->numSplats() against maxSupportedSplats() first - this call has no such guard
	// and would just grow GPU storage to fit (up to whatever the driver allows) if asked to.
	// source_name: kept only for the perf-diagnostics overlay (see PerfStats), so multiple splat objects/sources can be told apart there.
	GLObjectRef addObject(const UID& world_object_id, const GaussianSplatDataRef& object_space_data, const Vec4f& translation_ws, const Quat<float>& rotation_ws,
		float uniform_scale_ws, OpenGLEngine& opengl_engine, const std::string& source_name);

	// Re-bakes world_object_id's splat range with a new pose (same params as addObject()) and partially re-uploads just the affected GPU texture rows -
	// for GUIClient::moveObject()/scaleObject()/the transform gizmo, and any other path that changes a splat WorldObject's transform (network updates from
	// other clients included - see the class comment in GaussianSplatRenderer.h... er, this file, for why that matters). No-op (returns false) if
	// world_object_id isn't a registered splat object; returns true if handled.
	bool updateObjectTransform(const UID& world_object_id, const Vec4f& translation_ws, const Quat<float>& rotation_ws, float uniform_scale_ws, OpenGLEngine& opengl_engine);

	// Removes world_object_id's range from the world splat cloud (full CPU rebuild + full GPU re-upload - see class comment; removal is a rare, user-driven
	// event, not a per-frame one, so this isn't optimised further). Returns false (no-op) if world_object_id isn't a registered splat object - in that case
	// the caller should proceed with its normal (non-splat) object-removal path. Returns true if handled; in that case the caller must NOT
	// opengl_engine.removeObject() ob->opengl_engine_ob itself (it's the shared world GLObject, and other splat objects may still be using it) - just clear
	// the WorldObject's own opengl_engine_ob reference, same as this renderer's removeObject() always required, just no longer for an object-specific GLObject.
	bool removeObject(const UID& world_object_id);

	bool isSplatObject(const UID& world_object_id) const;

	// Maximum number of splats the WORLD (not a single object any more) can hold, given the real GL_MAX_TEXTURE_SIZE (OpenGLEngine::max_texture_size).
	// Callers adding a new splat object client-side (see GUIClient::createGaussianSplatObjectFromLocalFile()) should reject files that would push
	// numSplatsInWorld() over this before spending any effort on upload/decode - addObject() itself has no such guard.
	static size_t maxSupportedSplats(int gl_max_texture_size);

	// Since stage 4 (Claude_LOD_plan.md), this is actually a NODE count where a tree has been built for an object (leaves + merged internal nodes, typically ~1.3x-1.8x that object's real leaf/splat count -
	// see the class comment's "On-the-fly LoD" paragraph), not a pure leaf count - see the known-gap note at GUIClient::createGaussianSplatObjectFromLocalFile()'s maxSupportedSplats() check.
	size_t numSplatsInWorld() const { return total_splats; }

	// Per-frame update: refreshes the viewport-size / focal-length user uniforms the world cloud's shader needs for the EWA covariance projection, drains any
	// completed background LoD traversal result (applying it as the new current_instance_indices - see class comment) and depth-sort result, and kicks off a
	// new traversal and/or depth-sort task if the camera has moved past the relevant re-run threshold since the last one of each was kicked off. Call once
	// per frame, after the frame's camera transform has been set on opengl_engine.
	void think(OpenGLEngine& opengl_engine, glare::TaskManager& task_manager);

	void shutdown();

	// Live-tunable traversal/resort parameters (Claude_LOD_plan.md stage 7 - "Gaussian Splats settings" dock widget on Qt / equivalent on SDL). Take effect on the NEXT traversal/sort kick-off in think() -
	// no rebuild or reload needed, unlike lod_base (a tree-build-time parameter, owned by the caller of buildGaussianSplatLodTree() - see LoadModelTask - not by this class at all).
	float getPixelScaleLimit() const { return pixel_scale_limit; }
	void setPixelScaleLimit(float v) { pixel_scale_limit = v; }
	size_t getMaxSplatsBudget() const { return max_splats_budget; }
	void setMaxSplatsBudget(size_t v) { max_splats_budget = v; }
	float getResortMoveThreshold() const { return resort_move_threshold_ws; }
	void setResortMoveThreshold(float v) { resort_move_threshold_ws = v; }

	// Exposed for the in-world performance-diagnostics overlay (GUIClient) - not used by the rendering path itself.
	struct PerfStats
	{
		std::string source_name; // Summary string (e.g. "N splat object(s)") - see getPerfStats(), there's only one row now, for the whole world cloud.
		size_t num_splats;
		double last_coarse_sort_duration_s; // Wall-clock time (from task start) the most recently completed background sort's fast/approximate stage 1 took, or -1 if none has completed yet.
		double last_sort_duration_s; // Wall-clock time (from task start) the most recently completed background sort's precise stage 2 took, or -1 if none has completed yet.
		uint64 num_sorts_completed;
		double last_traversal_duration_s; // Wall-clock time the most recently completed LoD traversal (stage 5) took, or -1 if none has completed yet.
		size_t last_traversal_num_selected; // How many nodes the most recently completed traversal's frontier contained - i.e. what current_instance_indices was set to (before the subsequent depth-sort, which never changes the count).
		uint64 num_traversals_completed;
		bool last_traversal_hit_budget_cap; // True if the most recently completed traversal's while loop stopped because expanding the next node would have exceeded max_splats_budget, rather than because
			// every remaining frontier node was already small enough on screen (see GaussianSplatLodTraversalTask::run()) - i.e. detail is being limited by the budget, not by what the scene actually needs.
			// Surfaced as a "!WARNING!" line in GUIClient::getDiagnosticsString() when true, so it's obvious (not just inferred by eyeballing whether last_traversal_num_selected ~= max_splats_budget).
	};
	void getPerfStats(std::vector<PerfStats>& stats_out) const;

	// Per-object breakdown, for the "Show Gaussian splat LOD details" diagnostics checkbox (GUIClient::getDiagnosticsString()) - the aggregate PerfStats above only covers the whole world cloud, not
	// individual objects, which isn't enough to answer "is THIS object actually showing full detail right now, or a coarse stand-in?" for a specific splat.
	struct PerObjectStats
	{
		UID world_object_id;
		bool has_tree; // Whether object_space_data->lod_tree was built (successfully) for this object - false means it's still using the pre-stage-4 "all leaves, no LoD" fallback, see the class comment.
		size_t num_leaf_splats; // The object's original splat count (object_space_data->numSplats()), regardless of whether a tree was built.
		size_t num_tree_nodes; // entry.count - equals num_leaf_splats when !has_tree (trivially, no internal nodes exist); the tree's full node count (leaves + merged internal nodes) otherwise.
		size_t num_selected_now; // How many of this object's nodes are in the CURRENT current_instance_indices selection (i.e. what's actually being drawn this frame) - see that field's comment.
	};
	void getPerObjectStats(std::vector<PerObjectStats>& stats_out) const;

	// Hash of the actual vertex+fragment shader source bytes read from disk by makeShaders(), as an 8-hex-digit string. Lets the ImGui overlay
	// prove which shader source is actually running - unlike a build-date string baked in at compile time, this is computed from the file
	// makeShaders() genuinely loaded, so it reflects preload-cache/staging-copy problems (e.g. a stale data/shaders/ copy) that a build indicator can't catch.
	const std::string& getShaderSourceHash() const { return shader_source_hash; }

private:
	GLARE_DISABLE_COPY(GaussianSplatRenderer);

	// Grows the data texture + instance_index_vbo (and rebuilds the VAO) if needed_splats exceeds current capacity. ob_already_in_engine must be false only for the
	// very first call ever (made while building world_ob, before opengl_engine.addObject(world_ob) has happened - see addObject()) - in every other case it must
	// be true, so a VAO rebuild here refreshes the engine's cached draw-time VAO reference via opengl_engine.objectMaterialsUpdated() (see .cpp for why that's needed).
	void ensureGpuCapacity(size_t needed_splats, OpenGLEngine& opengl_engine, bool ob_already_in_engine);
	void rebuildVAO(OpenGLEngine& opengl_engine); // Rebuilds world_ob->vert_vao bound to the current instance_index_vbo - needed whenever that VBO is replaced (growth).
	void uploadTexelRowsForSplatRange(size_t first_splat, size_t num_splats_to_upload); // Repacks and re-uploads just the texture rows spanning [first_splat, first_splat+num_splats_to_upload).
	void rebuildWorldAABB(); // Recomputes world_ob->mesh_data->aabb_os as the union of every entry's current world AABB. O(num entries), not O(num splats).

	// Recomputes current_instance_indices from entries (see that field's comment) and sets world_ob->num_instances_to_draw to match. Does NOT touch instance_index_vbo itself - callers (addObject()/
	// removeObject()) write current_instance_indices to it explicitly, once instance_index_vbo is known to exist (it doesn't yet on the very first-ever addObject() call, before ensureGpuCapacity() has
	// run) - see those functions for why the ordering matters. Call whenever entries/topology changes; NOT needed from updateObjectTransform() (a move/scale re-bakes node attributes in place but never
	// changes which entries exist or their offsets, so the selected index set itself stays valid).
	void rebuildCurrentInstanceIndices();

	Reference<OpenGLProgram> shader_prog;
	std::string shader_source_hash;

	GLObjectRef world_ob; // The single persistent "world splat cloud" GLObject - created lazily by the first addObject() call. Identity ob_to_world_matrix: every splat's position is already baked into world space in the data itself.
	Reference<VBO> instance_index_vbo;
	size_t gpu_capacity_splats; // Current allocated capacity of both the data texture and instance_index_vbo, in splats. total_splats <= gpu_capacity_splats always.
	size_t total_splats;

	// Combined world-space CPU-side splat data for every splat currently in the world, concatenated in registration order (see entries below for which range belongs to which object).
	std::vector<Vec3f> world_positions;
	std::vector<Vec3f> world_scales;
	std::vector<Vec4f> world_rotations; // (x, y, z, w)
	std::vector<Vec4f> world_colours; // (r, g, b, opacity) - never changes after an entry is added (colour isn't affected by an object's pose), so removeObject()'s rebuild is the only thing that ever moves these around.

	struct WorldSplatEntry
	{
		UID world_object_id;
		GaussianSplatDataRef object_space_data; // Original, untransformed splat data - kept so move/scale (updateObjectTransform()) can re-bake from scratch with a new pose rather than accumulating floating-point drift over repeated re-bakes.
		size_t offset, count; // This entry's range within the world_* arrays above (and within the GPU texture/VBO, in nodes - texel offset is offset * texels_per_splat). If object_space_data->lod_tree is
			// non-empty, this range covers EVERY node of that tree (leaves and merged internal nodes alike, in the tree's own linearised order - see GaussianSplatLodTree.h), not just the leaf splats -
			// see the class comment's "On-the-fly LoD" paragraph. Otherwise (no tree built yet/failed) it's the plain leaf list, same as before stage 4.
	};
	std::vector<WorldSplatEntry> entries;

	// The world-node GLOBAL indices (i.e. directly usable as texelFetch/instance indices, already offset by whichever entry they belong to) currently selected for drawing - written into instance_index_vbo
	// (in this exact order) by whoever last set it, and consumed by think() as the *input* to the depth-sort (see positions_snapshot/index_snapshot in the .cpp) rather than sorting the whole world_positions
	// array. Two different things write it, at two different rates: rebuildCurrentInstanceIndices() (synchronous, called from addObject()/removeObject() - see that method's comment) sets an immediate
	// "root node only per tree-having entry, all leaves otherwise" placeholder the instant entries changes, and GaussianSplatLodTraversalTask's result (async, applied in think() - see the class comment's
	// traversal concurrency note) overwrites it again shortly after with the real per-frame camera-based best-first frontier. Whichever one last ran wins; nothing else in this class (the sort, the VBO
	// write, the draw call) cares which - only what's actually IN this vector matters to them.
	std::vector<uint32> current_instance_indices;

	uint64 structure_generation; // Bumped by removeObject() (which renumbers/repacks everything); NOT bumped by addObject() (a pure append is safe for an in-flight sort - see class comment). Sort results carry the generation they were computed against and are dropped on arrival if it's gone stale.
	uint64 topology_generation; // Bumped by addObject() AND removeObject() alike (unlike structure_generation, which addObject() deliberately leaves untouched) - a stale LoD traversal result is wrong the instant entries changes at all, not only on removal, see the class comment's traversal concurrency note.

	bool sort_in_flight; // True from when a sort task is kicked off until its *precise* (stage 2) result is applied - the fast coarse (stage 1) result doesn't clear this, see think().
	Vec4f last_sort_cam_pos_ws; // World-space camera position as of the last sort *kicked off* (not necessarily completed) - used to decide when a re-sort is worth doing.
	bool have_last_sort_cam_pos;
	Reference<GaussianSplatSortScratch> sort_scratch; // Working buffers for the world cloud's depth-sorts, reused across sorts (allocated on first use). Reference-counted (not owned outright) so an in-flight sort task keeps them alive even across shutdown().

	double last_coarse_sort_duration_s;
	double last_sort_duration_s;
	uint64 num_sorts_completed;

	ThreadSafeQueue<Reference<ThreadMessage> > sort_result_queue; // Written to by GaussianSplatSortTask::run() (worker thread), drained by think() (main thread).

	// LoD traversal (Claude_LOD_plan.md stage 5) - same "kick off in think(), drain the result queue, generation-gate staleness" idiom as the depth-sort above, see the class comment's traversal concurrency note.
	bool traversal_in_flight; // True from when a traversal task is kicked off until its result is applied (traversal has no coarse/precise split like the sort - one stage only).
	Vec4f last_traversal_cam_pos_ws; // World-space camera position as of the last traversal *kicked off* - used to decide when a re-traversal is worth doing (same idea as last_sort_cam_pos_ws, tracked separately since the two tasks run independently).
	bool have_last_traversal_cam_pos;
	Reference<GaussianSplatLodTraversalScratch> traversal_scratch; // Working buffers for the traversal, reused across runs (allocated on first use) - see GaussianSplatSortScratch's comment for why Reference<> (not owned outright).

	double last_traversal_duration_s;
	size_t last_traversal_num_selected;
	uint64 num_traversals_completed;
	bool last_traversal_hit_budget_cap;

	ThreadSafeQueue<Reference<ThreadMessage> > traversal_result_queue; // Written to by GaussianSplatLodTraversalTask::run() (worker thread), drained by think() (main thread).

	// Live-tunable via getPixelScaleLimit()/setPixelScaleLimit() etc. above - defaults match the constants these replaced (Claude_LOD_plan.md stage 5/6), except max_splats_budget's default was raised from
	// 2,000,000 (stage 5's original conservative pick) to 10,000,000 after real-scale testing on an 8.6M-splat scene showed 2M was the binding constraint on close-up detail for a single large object.
	float pixel_scale_limit;
	size_t max_splats_budget;
	float resort_move_threshold_ws;
};
