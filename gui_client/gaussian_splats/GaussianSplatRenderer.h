/*=====================================================================
GaussianSplatRenderer.h
------------------------
coded by AI agent under @russiaman supervision -
Generated at Mon Jul 27 06:16:15 2026
=====================================================================*/
#pragma once


#include "GaussianSplatData.h"
#include <opengl/OpenGLEngine.h>
#include <opengl/OpenGLProgram.h>
#include <utils/Platform.h>
#include <utils/Reference.h>
#include <utils/ThreadSafeQueue.h>
#include <utils/ThreadMessage.h>
#include <string>
#include <vector>

namespace glare { class TaskManager; }
class GaussianSplatSortScratch; // Defined in GaussianSplatRenderer.cpp - the reusable working buffers one object's background depth-sort needs, see ManagedObject::sort_scratch.


/*=====================================================================
GaussianSplatRenderer
----------------------
Renders Gaussian Splat clouds (GaussianSplatData) as app-level OpenGL
objects, drawn through the engine's normal transparent-object pass
(see snapshots/2026-07-26_phase2-architecture-contract.md §2.G).

GPU representation: all splat attributes (position, scale, rotation,
colour+opacity) are packed into a single RGBA32F texture, 4 texels per
splat, unpacked with texelFetch() in the vertex shader. A single packed
texture (rather than e.g. one texture per attribute) is required by the
engine's custom-shader draw path, which only binds material.albedo_texture
for app-supplied shaders (architecture contract §4.1).

Geometry is one instanced quad per splat cloud; draw order is controlled
by a dedicated per-object uint32 index VBO (attribute "splat_index_in",
forced to attribute location 1, see makeShaders()), not gl_InstanceID
directly - this lets the CPU depth-sort below reorder splats back-to-front
just by rewriting that VBO with VBO::updateData(), without touching the
mesh or shader.

CPU depth-sort (architecture contract task #7): a full re-sort of up to
~1M splats is too slow to redo unconditionally every frame on the main
thread (see snapshots/2026-07-27-session6-depth-sort-and-axis-convention-fix.md
for the first, deliberately-naive std::sort-every-frame version this
replaces). This version instead:
 - sorts on *distance from the camera*, not on depth along the camera's
   forward axis. The two give the same visible result - two splats can only
   blend into each other where they overlap on screen, i.e. when they're on
   (nearly) the same view ray, where distance and forward-depth order agree
   - but distance doesn't change when the camera merely rotates on the spot.
   That makes the re-sort trigger below exact rather than approximate:
   camera *position* is genuinely the only thing that can invalidate the
   order, so standing still and looking around needs no re-sorting at all.
   (An earlier version sorted on forward-axis depth while still only
   triggering on position, which left the order silently stale - and very
   visibly wrong - through any turn-in-place.)
 - only kicks off a re-sort once the camera has moved past a threshold
   since the last sort it started - see the point above for why position
   alone is the correct criterion;
 - quantises that distance *linearly* onto the full uint32 sort key range
   (see GaussianSplatSortTask::run()), rather than using the float's own bit
   pattern as the key. Both orderings are identical for a full sort, but a
   linear key is what makes the coarse stage below meaningful: the top bits
   of a float's bit pattern are its *exponent*, so bucketing on them groups
   splats logarithmically (an entire 8-16m shell lands in one bucket, in
   arbitrary order within it), whereas the top bits of a linear key are
   evenly-spaced depth slices.
 - does the sort itself in two stages, both computed back-to-back on the
   same worker task (see GaussianSplatSortTask::run()):
     1. a fast single-pass approximate sort (Sort::serialCountingSortWithNumBuckets()
        over the top 16 bits of the linear key - 65536 evenly-spaced depth
        slices), posted back and applied to the VBO as soon as it's ready;
     2. the precise sort (Sort::radixSort32BitKey(), already used elsewhere
        in the engine for batch sorting, see OpenGLEngine.cpp's
        sortBatchDrawInfoWithDists()), posted and applied right after.
   Splats sharing a coarse bucket are left in arbitrary relative order after
   stage 1, but with 65536 linear slices a bucket spans a tiny fraction of
   the cloud's depth range (well under a millimetre for a room-sized scene),
   far finer than the splats themselves - so stage 1 alone is already
   visually correct, and stage 2 is a guarantee rather than a visible fix.
   This matters because a single large interior splat cloud's *sole*
   background sort task can take far longer than the ~12ms measured for a
   140k-splat object (that number was never a scene-wide budget, just one
   data point), and until a result lands the viewer keeps drawing the
   pre-move order. See snapshots/2026-07-28-session016-*.md.
 - reuses each object's sort working buffers between sorts (ManagedObject::sort_scratch)
   rather than allocating them per task: at multi-million splat counts those
   buffers are hundreds of MB, and allocating plus zeroing them every time
   was a large fraction of the total sort time.
 - runs the sort on a glare::TaskManager worker thread (the same
   high_priority_task_manager GUIClient already uses for per-frame
   physics/animation work - see OpenGLEngine.h's doc comment on that
   manager), not the main/render thread. Results come back via a
   ThreadSafeQueue<Reference<ThreadMessage>>, the same message-queue
   idiom LoadModelTask/TerrainSystem already use to hand results from a
   worker back to the main thread - think() drains it non-blockingly
   each frame and is the only place that calls VBO::updateData()
   (GL calls are main-thread-only; see VBO.cpp). Both stages' results go
   through the same queue/message type (GaussianSplatSortResultMsg::stage
   tells them apart); sort_in_flight only clears on the precise-stage
   message, so a new re-sort can't be kicked off for the same object while
   its precise stage is still catching up.
 - if a newer sort is kicked off before an in-flight one's result comes
   back, the stale result is simply dropped when it arrives (matched by
   the object's stable id) - one frame of slightly-stale draw order is
   visually harmless, and the next result always supersedes it.
Not handled yet: multiple splat objects don't share a task queue budget
(each gets its own worker task when it needs a re-sort) - fine for the
handful of splat objects a scene is expected to have; would need
throttling if that assumption changes. Also not done (documented as a
future option, not started): genuinely parallelising a single object's
sort itself (e.g. Sort::radixSortWithParallelPartition() with a dedicated
glare::TaskManager) - would cut worst-case latency further at extreme
(~10M+) splat counts, but needs its own OS threads carved out of the
Emscripten PTHREAD_POOL_SIZE budget (see gui_client/CMakeLists.txt) and
must not reuse high_priority_task_manager from within a task already
running on it (reentrant addTask()+wait() on the same pool can deadlock).

Not handled yet (see architecture contract §6.3 - documented, not silent):
 - Order-independent transparency (an optional native-only engine
   feature). The fragment shader writes a single premultiplied-alpha
   colour output, matching the non-OIT blend path the web build always
   takes (OpenGLEngine::drawTransparentMaterialBatches()'s
   !use_order_indep_transparency branch). See gaussian_splat_frag_shader.glsl
   for why this differs from the dual-output pattern used by
   parcel_frag_shader.glsl (that pattern has a known, commented TODO gap
   for the non-OIT case in the upstream shader).
=====================================================================*/
class GaussianSplatRenderer
{
public:
	GaussianSplatRenderer();
	~GaussianSplatRenderer(); // Defined in the .cpp, not defaulted here, because ManagedObject holds a Reference<> to a type only defined there.

	// Builds the shared shader program used by all splat objects. Call once, after the OpenGL context exists (mirrors GUIClient::makeShaders() for parcel/portal).
	void makeShaders(OpenGLEngine& opengl_engine, const std::string& shader_dir);

	// Builds a ready-to-add GLObject for a decoded splat cloud: packs the GPU texture, builds the instanced quad mesh and instance-index VBO, sets up the material.
	// Caller still needs to set the returned object's ob_to_world_matrix and call opengl_engine.addObject() on it.
	// source_name: the object's model_url (or similar), kept only for the perf-diagnostics overlay (see PerfStats) so multiple splat objects can be told apart.
	GLObjectRef createObject(const GaussianSplatDataRef& splat_data, OpenGLEngine& opengl_engine, const std::string& source_name);

	// Maximum number of splats a single splat object's data texture can hold, given the real GL_MAX_TEXTURE_SIZE (OpenGLEngine::max_texture_size). Callers
	// building a new splat object client-side (see GUIClient::createGaussianSplatObjectFromLocalFile()) should reject files with more splats than this
	// before spending any effort on upload/decode - createObject() itself has no such guard and would just build an oversized (and likely GL-rejected) texture.
	static size_t maxSupportedSplats(int gl_max_texture_size);

	// Per-frame update: refreshes the viewport-size / focal-length user uniforms each managed object's shader needs for the EWA covariance projection,
	// drains any completed background depth-sort results, and kicks off a new depth-sort task for any object whose camera viewpoint has moved past
	// the re-sort threshold since its last sort. Call once per frame, after the frame's camera transform has been set on opengl_engine.
	void think(OpenGLEngine& opengl_engine, glare::TaskManager& task_manager);

	// Removes the managed entry for a splat object previously returned by createObject(), if any (no-op if ob isn't a splat object, or isn't
	// found - e.g. already removed). Must be called whenever a splat object's GLObject is torn down (see
	// GUIClient::removeAndDeleteGLObjectsForOb()) - otherwise its entry lingers forever in getPerfStats(), showing deleted objects in the
	// perf-diagnostics overlay indefinitely. Does not touch the GL resources themselves (textures/VBOs) or opengl_engine - the caller is
	// responsible for that, same as for any other object type.
	void removeObject(const GLObjectRef& ob);

	void shutdown();

	// Exposed for the in-world performance-diagnostics overlay (GUIClient) - not used by the rendering path itself.
	struct PerfStats
	{
		std::string source_name; // The source_name passed to createObject(), e.g. the object's model_url.
		size_t num_splats;
		double last_coarse_sort_duration_s; // Wall-clock time (from task start) the most recently completed background sort's fast/approximate stage 1 took, or -1 if none has completed yet.
		double last_sort_duration_s; // Wall-clock time (from task start) the most recently completed background sort's precise stage 2 took, or -1 if none has completed yet.
		uint64 num_sorts_completed;
	};
	void getPerfStats(std::vector<PerfStats>& stats_out) const;

private:
	GLARE_DISABLE_COPY(GaussianSplatRenderer);

	Reference<OpenGLProgram> shader_prog;

	// One managed splat cloud: the GL object plus everything think() needs to keep its instance-index VBO sorted back-to-front.
	struct ManagedObject
	{
		uint64 id; // Stable identity for matching an async sort result back to this object, independent of managed_objects' storage (a std::vector, so element addresses aren't stable across push_back).
		std::string source_name; // See createObject()'s source_name param - kept only for the perf-diagnostics overlay (PerfStats).
		GLObjectRef ob;
		GaussianSplatDataRef splat_data;
		Reference<VBO> instance_index_vbo;
		Reference<GaussianSplatSortScratch> sort_scratch; // Working buffers for this object's depth-sorts, reused across sorts. Reference-counted (not owned outright) so an in-flight sort task keeps them alive even if the object is removed mid-sort.

		bool sort_in_flight; // True from when a sort task is kicked off until its *precise* (stage 2) result is applied - the fast coarse (stage 1) result doesn't clear this, see think().
		Vec4f last_sort_cam_pos_ws; // World-space camera position as of the last sort *kicked off* (not necessarily completed) - used to decide when a re-sort is worth doing.
		bool have_last_sort_cam_pos;

		double last_coarse_sort_duration_s;
		double last_sort_duration_s;
		uint64 num_sorts_completed;
	};
	std::vector<ManagedObject> managed_objects; // Objects created by createObject(), refreshed each frame by think().

	uint64 next_object_id;
	ThreadSafeQueue<Reference<ThreadMessage> > sort_result_queue; // Written to by GaussianSplatSortTask::run() (worker thread), drained by think() (main thread).
};
