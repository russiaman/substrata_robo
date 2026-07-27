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
 - only kicks off a re-sort once the camera has moved/rotated past a
   threshold (in the splat object's local space) since the last sort it
   started - a static viewpoint doesn't need re-sorting at all;
 - does the sort itself with Sort::radixSort32BitKey (already used
   elsewhere in the engine for batch sorting, see OpenGLEngine.cpp's
   sortBatchDrawInfoWithDists()) instead of std::sort;
 - runs the sort on a glare::TaskManager worker thread (the same
   high_priority_task_manager GUIClient already uses for per-frame
   physics/animation work - see OpenGLEngine.h's doc comment on that
   manager), not the main/render thread. Results come back via a
   ThreadSafeQueue<Reference<ThreadMessage>>, the same message-queue
   idiom LoadModelTask/TerrainSystem already use to hand results from a
   worker back to the main thread - think() drains it non-blockingly
   each frame and is the only place that calls VBO::updateData()
   (GL calls are main-thread-only; see VBO.cpp).
 - if a newer sort is kicked off before an in-flight one's result comes
   back, the stale result is simply dropped when it arrives (matched by
   the object's stable id) - one frame of slightly-stale draw order is
   visually harmless, and the next result always supersedes it.
Not handled yet: multiple splat objects don't share a task queue budget
(each gets its own worker task when it needs a re-sort) - fine for the
handful of splat objects a scene is expected to have; would need
throttling if that assumption changes.

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
		double last_sort_duration_s; // Wall-clock time the most recently completed background sort task took, or -1 if none has completed yet.
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

		bool sort_in_flight;
		Vec4f last_sort_cam_pos_ws; // World-space camera position as of the last sort *kicked off* (not necessarily completed) - used to decide when a re-sort is worth doing.
		bool have_last_sort_cam_pos;

		double last_sort_duration_s;
		uint64 num_sorts_completed;
	};
	std::vector<ManagedObject> managed_objects; // Objects created by createObject(), refreshed each frame by think().

	uint64 next_object_id;
	ThreadSafeQueue<Reference<ThreadMessage> > sort_result_queue; // Written to by GaussianSplatSortTask::run() (worker thread), drained by think() (main thread).
};
