/*=====================================================================
GaussianSplatRenderer.cpp
---------------------------
coded by AI agent under @russiaman supervision -
Generated at Mon Jul 27 06:16:15 2026
=====================================================================*/
#include "GaussianSplatRenderer.h"


#include <opengl/IncludeOpenGL.h>
#include <opengl/OpenGLMeshRenderData.h>
#include <opengl/OpenGLShader.h>
#include <opengl/OpenGLTexture.h>
#include <opengl/VBO.h>
#include <opengl/VAO.h>
#include <opengl/VertexBufferAllocator.h>
#include <maths/mathstypes.h>
#include <maths/Matrix4f.h>
#include <utils/ArrayRef.h>
#include <utils/BitUtils.h>
#include <utils/ConPrint.h>
#include <utils/Exception.h>
#include <utils/FileUtils.h>
#include <utils/IncludeXXHash.h>
#include <utils/RefCounted.h>
#include <utils/StringUtils.h>
#include <utils/Sort.h>
#include <utils/Task.h>
#include <utils/TaskManager.h>
#include <utils/Timer.h>
#include <utils/Vector.h>
#include <assert.h>
#include <algorithm>
#include <cstring>
#include <limits>


// Camera position must move at least this far (in world-space metres) since the world splat cloud's last kicked-off depth-sort before another one is worth
// kicking off - avoids resorting every single frame for a static or near-static viewpoint. Position is the *only* thing that can invalidate the sort order,
// since the order is by distance from the camera, which a pure rotation doesn't change - see the class comment in GaussianSplatRenderer.h.
static const float resort_move_threshold_ws = 0.1f;


// Reusable working buffers for the world splat cloud's background depth-sorts (forward-declared in GaussianSplatRenderer.h, held by GaussianSplatRenderer::sort_scratch).
// At multi-million splat counts these run to hundreds of MB, so they're kept and reused across sorts rather than allocated (and zeroed) per sort.
// Reference-counted rather than owned outright by GaussianSplatRenderer: an in-flight sort task holds a reference too.
class GaussianSplatSortScratch : public RefCounted
{
public:
	struct SortItem
	{
		uint32 key;
		uint32 splat_index;
	};

	js::Vector<Vec3f, 16> positions_snapshot; // A frozen copy of world_positions[0, num_splats) taken synchronously on the main thread when a sort is kicked off - see the concurrency note in GaussianSplatRenderer.h. The worker thread only ever reads this, never the live, growable world_positions vector.

	js::Vector<SortItem, 16> items; // Sort input, and the precise stage's output.
	js::Vector<SortItem, 16> working_space; // Scratch space the Sort:: routines need, and the coarse stage's output.

	// The two stages' results, as instance draw orders ready for VBO::updateData(). Kept in separate buffers so that the precise stage can't overwrite a
	// coarse result the main thread hasn't consumed yet - each is written exactly once per sort, and read only after its result message is dequeued.
	js::Vector<uint32, 16> coarse_indices;
	js::Vector<uint32, 16> precise_indices;

	js::Vector<uint32, 16> temp_counts; // Bucket counts Sort::radixSort32BitKey() needs.
};


namespace
{


const size_t texels_per_splat = 4; // See class comment in GaussianSplatRenderer.h and gaussian_splat_vert_shader.glsl for the texel layout this implies.
const size_t splat_tex_width = 4096; // 4096 gives ~16.7M splat capacity on GPUs with GL_MAX_TEXTURE_SIZE >= 16384 (common). WebGL2 only guarantees 2048, but real hardware consistently reports much more.
const int splat_index_attribute_loc = 1; // Forced via bindAttributeLocation() in GaussianSplatRenderer::makeShaders() - slot 1 is otherwise used for "normal_in", which splats have no use for.


// How many texture rows (each splat_tex_width texels wide) are needed to hold num_splats splats.
size_t texHeightForSplatCount(size_t num_splats)
{
	const size_t total_texels = num_splats * texels_per_splat;
	return myMax<size_t>(1, Maths::roundedUpDivide(total_texels, splat_tex_width));
}


// The instanced quad's geometry: a local-space unit square. The vertex shader scales/orients this per-splat based on the projected 2D covariance (EWA splatting), so this local shape only needs to bound [-1, 1] in both axes.
Reference<OpenGLMeshRenderData> makeInstancedQuadMeshData(VertexBufferAllocator& allocator)
{
	Reference<OpenGLMeshRenderData> mesh_data = new OpenGLMeshRenderData();
	mesh_data->setIndexType(GL_UNSIGNED_SHORT);
	mesh_data->has_uvs = false;
	mesh_data->has_shading_normals = false;
	mesh_data->num_materials_referenced = 1;
	mesh_data->aabb_os = js::AABBox::emptyAABBox(); // Grown as splat objects are added - see GaussianSplatRenderer::addObject()/removeObject().

	mesh_data->batches.resize(1);
	mesh_data->batches[0].material_index = 0;
	mesh_data->batches[0].prim_start_offset_B = 0;
	mesh_data->batches[0].num_indices = 6;

	VertexAttrib pos_attrib;
	pos_attrib.enabled = true;
	pos_attrib.num_comps = 3;
	pos_attrib.type = GL_FLOAT;
	pos_attrib.normalised = false;
	pos_attrib.stride = (uint32)(sizeof(float) * 3);
	pos_attrib.offset = 0;
	mesh_data->vertex_spec.attributes.push_back(pos_attrib);

	// Placeholder for the per-instance splat-index attribute - disabled and with no VBO of its own until GaussianSplatRenderer::ensureGpuCapacity()/rebuildVAO() builds the instance-index VBO and rebuilds the VAO with it enabled.
	// (This mirrors how GLMeshBuilding.cpp adds disabled instance-matrix attributes that GLObject::enableInstancing() enables later.)
	VertexAttrib splat_index_attrib;
	splat_index_attrib.enabled = false;
	splat_index_attrib.num_comps = 1;
	splat_index_attrib.type = GL_UNSIGNED_INT;
	splat_index_attrib.normalised = false;
	splat_index_attrib.integer_attribute = true;
	splat_index_attrib.instancing = true;
	splat_index_attrib.stride = (uint32)sizeof(uint32);
	splat_index_attrib.offset = 0;
	assert(mesh_data->vertex_spec.attributes.size() == (size_t)splat_index_attribute_loc);
	mesh_data->vertex_spec.attributes.push_back(splat_index_attrib);

	const float quad_verts[4 * 3] = {
		-1, -1, 0,
		 1, -1, 0,
		 1,  1, 0,
		-1,  1, 0
	};
	const uint16 quad_indices[6] = { 0, 1, 2, 0, 2, 3 };

	allocator.allocateBufferSpaceAndVAO(*mesh_data, mesh_data->vertex_spec, quad_verts, sizeof(quad_verts), quad_indices, sizeof(quad_indices));

	return mesh_data;
}


// Result of a background depth-sort, handed from the worker thread (GaussianSplatSortTask::run()) back to the main thread via GaussianSplatRenderer::sort_result_queue.
// Matched up against GaussianSplatRenderer::structure_generation in think(); if a removeObject() happened since this sort was kicked off, the generation
// will have moved on and the (now stale - see class comment in GaussianSplatRenderer.h) result is simply dropped.
const int coarse_key_bits = 16; // How many high bits of the (linear) sort key the coarse stage buckets on, i.e. 2^16 = 65536 evenly-spaced depth slices. See GaussianSplatSortTask::run().


class GaussianSplatSortResultMsg : public ThreadMessage
{
public:
	enum Stage
	{
		Stage_Coarse, // Fast, single-pass approximate order (see GaussianSplatSortTask::run()) - splats sharing one of the 65536 depth slices are in arbitrary relative order, which is far finer than the splats themselves.
		Stage_Precise // Full radixSort32BitKey() order - exact back-to-front order.
	};

	uint64 generation; // GaussianSplatRenderer::structure_generation as of when this sort was kicked off - see that field's comment.
	Stage stage;
	Reference<GaussianSplatSortScratch> scratch; // Holds this stage's result buffer (see sortedIndices()), and keeps it alive even if shutdown() was called while the sort ran.
	double sort_duration_s; // Wall-clock time since the task started (not just this stage's own cost) - lets the perf overlay show "how long until this stage's result was ready".

	// Back-to-front (farthest first) instance order, ready to write directly into instance_index_vbo.
	const js::Vector<uint32, 16>& sortedIndices() const { return (stage == Stage_Coarse) ? scratch->coarse_indices : scratch->precise_indices; }
};


// Sorts the world splat cloud's instances back-to-front by camera distance, entirely on a worker thread (glare::TaskManager) - no GL calls here, see class comment in GaussianSplatRenderer.h.
class GaussianSplatSortTask : public glare::Task
{
public:
	GaussianSplatSortTask(uint64 generation_, const Reference<GaussianSplatSortScratch>& scratch_, const Matrix4f& world_to_cam_,
		ThreadSafeQueue<Reference<ThreadMessage> >* result_queue_)
	:	generation(generation_), scratch(scratch_), world_to_cam(world_to_cam_), result_queue(result_queue_)
	{}

	virtual void run(size_t /*thread_index*/) override
	{
		Timer timer;

		typedef GaussianSplatSortScratch::SortItem SortItem;
		struct SortItemGetKey { inline uint32 operator () (const SortItem& item) const { return item.key; } };

		const js::Vector<Vec3f, 16>& positions = scratch->positions_snapshot; // Frozen snapshot taken by think() at kickoff - never the live, growable world_positions vector, see class comment in GaussianSplatRenderer.h.
		const size_t num_splats = positions.size();

		js::Vector<SortItem, 16>& items = scratch->items;
		js::Vector<SortItem, 16>& working_space = scratch->working_space;
		items.resizeNoCopy(num_splats);
		working_space.resizeNoCopy(num_splats);

		// Pass 1: distance from the camera to each splat, and the range those distances span.
		// Distance, rather than depth along the camera's forward axis, is what makes the resulting order invariant to camera rotation - see the class comment in
		// GaussianSplatRenderer.h. The distance is stashed in the key field as raw bits for now; pass 2 turns it into the actual integer sort key in place, so no
		// second array is needed to carry it between the two passes.
		float min_dist = std::numeric_limits<float>::max();
		float max_dist = 0;
		for(size_t i = 0; i < num_splats; ++i)
		{
			const Vec3f& p = positions[i];
			const float dist = maskWToZero(world_to_cam * Vec4f(p.x, p.y, p.z, 1.f)).length(); // world_to_cam's rotation+translation preserves lengths, so this is the true world-space camera distance.
			items[i].key = bitCast<uint32>(dist);
			items[i].splat_index = (uint32)i;
			min_dist = myMin(min_dist, dist);
			max_dist = myMax(max_dist, dist);
		}

		// Pass 2: quantise those distances linearly across the whole uint32 key range, inverted so that ascending key order means farthest-first (back-to-front, as
		// premultiplied-alpha "over" blending needs - see architecture contract §2.G). A linear key, rather than the float's own bit pattern (which sorts identically
		// but spaces values by exponent), is what makes the coarse stage below meaningful - see the class comment in GaussianSplatRenderer.h.
		const float dist_range = myMax(max_dist - min_dist, 1.0e-9f); // Guards the degenerate all-splats-equidistant case; any non-zero value works there, since every key ends up 0 anyway.
		const double key_scale = (double)std::numeric_limits<uint32>::max() / (double)dist_range;
		for(size_t i = 0; i < num_splats; ++i)
			items[i].key = (uint32)((double)(max_dist - bitCast<float>(items[i].key)) * key_scale);

		// Stage 1: coarse sort - one counting-sort pass over the top coarse_key_bits of the key, i.e. 2^coarse_key_bits evenly-spaced depth slices. Much cheaper than
		// the 3-pass precise sort below, and (unlike a bucketing that groups by float exponent) already fine-grained enough to be visually correct on its own, so it's
		// posted immediately rather than making the view wait out the precise stage still showing the pre-move order.
		{
			struct CoarseBucketChooser { inline size_t operator () (const SortItem& item) const { return item.key >> (32 - coarse_key_bits); } };
			Sort::serialCountingSortWithNumBuckets(items.data(), working_space.data(), num_splats, (size_t)1 << coarse_key_bits, CoarseBucketChooser());

			scratch->coarse_indices.resizeNoCopy(num_splats);
			for(size_t i = 0; i < num_splats; ++i)
				scratch->coarse_indices[i] = working_space[i].splat_index;

			enqueueResult(GaussianSplatSortResultMsg::Stage_Coarse, timer.elapsed());
		}

		// Stage 2: precise sort - unaffected by stage 1 above, which only wrote to working_space (which radixSort32BitKey() treats as scratch anyway); items is still in its original order here.
		scratch->temp_counts.resizeNoCopy(6144); // Required size for Sort::radixSort32BitKey(), see its doc comment in Sort.h.
		Sort::radixSort32BitKey(items.data(), working_space.data(), num_splats, SortItemGetKey(), scratch->temp_counts.data(), scratch->temp_counts.size());

		scratch->precise_indices.resizeNoCopy(num_splats);
		for(size_t i = 0; i < num_splats; ++i)
			scratch->precise_indices[i] = items[i].splat_index;

		enqueueResult(GaussianSplatSortResultMsg::Stage_Precise, timer.elapsed());
	}

private:
	void enqueueResult(GaussianSplatSortResultMsg::Stage stage, double elapsed_s)
	{
		Reference<GaussianSplatSortResultMsg> msg = new GaussianSplatSortResultMsg();
		msg->generation = generation;
		msg->stage = stage;
		msg->scratch = scratch;
		msg->sort_duration_s = elapsed_s;
		result_queue->enqueue(msg);
	}

	uint64 generation;
	Reference<GaussianSplatSortScratch> scratch; // Keeps the position snapshot and this task's working buffers alive for the duration of the task, independent of what the main thread does to the live world arrays meanwhile.
	Matrix4f world_to_cam;
	ThreadSafeQueue<Reference<ThreadMessage> >* result_queue;
};


} // end anonymous namespace


GaussianSplatRenderer::GaussianSplatRenderer()
:	gpu_capacity_splats(0), total_splats(0), structure_generation(0), sort_in_flight(false), have_last_sort_cam_pos(false),
	last_coarse_sort_duration_s(-1.0), last_sort_duration_s(-1.0), num_sorts_completed(0)
{}


GaussianSplatRenderer::~GaussianSplatRenderer()
{}


void GaussianSplatRenderer::makeShaders(OpenGLEngine& opengl_engine, const std::string& shader_dir)
{
	const std::string version_directive    = opengl_engine.getVersionDirective();
	const std::string preprocessor_defines = opengl_engine.getPreprocessorDefines();

	// Hash the actual shader source bytes read from disk right here, so getShaderSourceHash() reflects exactly what this run loaded and compiled -
	// see its declaration comment for why (catches stale-preload-copy issues a compile-time build indicator can't).
	{
		const std::string vert_src = FileUtils::readEntireFileTextMode(shader_dir + "/gaussian_splat_vert_shader.glsl");
		const std::string frag_src = FileUtils::readEntireFileTextMode(shader_dir + "/gaussian_splat_frag_shader.glsl");
		const uint64 hash = XXH64(vert_src.data(), vert_src.size(), /*seed=*/1) ^ XXH64(frag_src.data(), frag_src.size(), /*seed=*/2);
		shader_source_hash = leftPad(toHexString(hash & 0xFFFFFFFFull), '0', 8);
	}

	// wait_for_build_to_complete is false here because we need to bind our custom per-instance attribute location and relink before the program is considered finished - see below.
	shader_prog = new OpenGLProgram(
		"gaussian splat prog",
		new OpenGLShader(shader_dir + "/gaussian_splat_vert_shader.glsl", version_directive, preprocessor_defines, GL_VERTEX_SHADER),
		new OpenGLShader(shader_dir + "/gaussian_splat_frag_shader.glsl", version_directive, preprocessor_defines, GL_FRAGMENT_SHADER),
		opengl_engine.getAndIncrNextProgramIndex(),
		/*wait_for_build_to_complete=*/false
	);

	// OpenGLProgram's constructor already links the program once, with glBindAttribLocation() calls for the engine's standard attribute names (see OpenGLProgram::OpenGLProgram()).
	// Our vertex shader's "splat_index_in" attribute isn't one of those names, so at this point it's bound to whatever free location the driver auto-assigned it.
	// Force it to a known location (1, the otherwise-unused "normal_in" slot - splats have no normals) and relink, so ensureGpuCapacity()/rebuildVAO() can build the instance-index VertexAttrib at a location we know ahead of time.
	shader_prog->bindAttributeLocation(splat_index_attribute_loc, "splat_index_in");
	glLinkProgram(shader_prog->program);
	shader_prog->forceFinishLinkAndDoPostLinkCode(); // Throws glare::Exception if the (re)link failed.

	opengl_engine.addProgram(shader_prog);

	shader_prog->appendUserUniformInfo(UserUniformInfo::UniformType_Vec2, "viewport_dims_px");
	shader_prog->appendUserUniformInfo(UserUniformInfo::UniformType_Vec2, "focal_len_px");
	shader_prog->appendUserUniformInfo(UserUniformInfo::UniformType_Int,  "splat_tex_width");
}


size_t GaussianSplatRenderer::maxSupportedSplats(int gl_max_texture_size)
{
	// Texture width is fixed at splat_tex_width regardless of gl_max_texture_size (true for any conformant WebGL2/desktop GL implementation, which guarantees
	// GL_MAX_TEXTURE_SIZE >= 2048 - see splat_tex_width's own comment). Height is capped at the real driver limit, not the guaranteed minimum, since real
	// hardware/drivers commonly support much larger textures than the spec minimum. This is now a WORLD-wide limit (see class comment), not a single object's.
	const size_t max_tex_h = (size_t)myMax(1, gl_max_texture_size);
	return (splat_tex_width * max_tex_h) / texels_per_splat;
}


void GaussianSplatRenderer::rebuildVAO(OpenGLEngine& /*opengl_engine*/)
{
	VertexSpec vertex_spec = world_ob->mesh_data->vertex_spec;
	vertex_spec.attributes[splat_index_attribute_loc].vbo = instance_index_vbo;
	vertex_spec.attributes[splat_index_attribute_loc].enabled = true;
#if DO_INDIVIDUAL_VAO_ALLOC
	world_ob->vert_vao = new VAO(world_ob->mesh_data->vbo_handle.vbo, world_ob->mesh_data->indices_vbo_handle.index_vbo, vertex_spec);
#else
	world_ob->vert_vao = new VAO(vertex_spec);
#endif
	world_ob->instance_matrix_vbo = instance_index_vbo; // Keep the VBO referenced-alive via the object; also gives think()/the depth-sort code access to it for VBO::updateData().
}


void GaussianSplatRenderer::uploadTexelRowsForSplatRange(size_t first_splat, size_t num_splats_to_upload)
{
	if(num_splats_to_upload == 0)
		return;

	// Repack whole texture rows spanning [first_splat, first_splat + num_splats_to_upload) - an arbitrary splat range doesn't align to row boundaries
	// (splat_tex_width / texels_per_splat splats per row), so this may re-pack a few splats belonging to a neighbouring entry too; that's harmless, since
	// their data in world_positions/etc. is already correct and we're just re-deriving the same texels for them.
	const size_t first_texel = first_splat * texels_per_splat;
	const size_t last_texel_excl = (first_splat + num_splats_to_upload) * texels_per_splat; // Exclusive.
	const size_t start_row = first_texel / splat_tex_width;
	const size_t end_row = Maths::roundedUpDivide(last_texel_excl, splat_tex_width); // Exclusive.
	const size_t num_rows = end_row - start_row;

	const size_t row_start_splat = (start_row * splat_tex_width) / texels_per_splat;
	const size_t row_end_splat_excl = myMin(total_splats, (end_row * splat_tex_width) / texels_per_splat);

	std::vector<float> texel_data(splat_tex_width * num_rows * 4, 0.f);
	for(size_t i = row_start_splat; i < row_end_splat_excl; ++i)
	{
		const size_t local_splat_i = i - row_start_splat;
		const size_t base = local_splat_i * texels_per_splat * 4;

		const Vec3f& pos   = world_positions[i];
		const Vec3f& scale = world_scales[i];
		const Vec4f& rot   = world_rotations[i];
		const Vec4f& col   = world_colours[i];

		float* const t = &texel_data[base];
		t[0] = pos.x;    t[1] = pos.y;    t[2] = pos.z;    t[3] = scale.x;
		t[4] = scale.y;  t[5] = scale.z;  t[6] = rot.x[0]; t[7] = rot.x[1];
		t[8] = rot.x[2]; t[9] = rot.x[3]; t[10] = col.x[0]; t[11] = col.x[1];
		t[12] = col.x[2]; t[13] = col.x[3]; // t[14], t[15] left as zero.
	}

	world_ob->materials[0].albedo_texture->loadRegionIntoExistingTexture(/*mipmap_level=*/0, /*x=*/0, /*y=*/start_row, /*z=*/0, /*region_w=*/splat_tex_width, /*region_h=*/num_rows, /*region_d=*/1,
		/*src_row_stride_B=*/splat_tex_width * 4 * sizeof(float), ArrayRef<uint8>(reinterpret_cast<const uint8*>(texel_data.data()), texel_data.size() * sizeof(float)), /*bind_needed=*/true);
}


void GaussianSplatRenderer::ensureGpuCapacity(size_t needed_splats, OpenGLEngine& opengl_engine, bool ob_already_in_engine)
{
	if(needed_splats <= gpu_capacity_splats && world_ob->materials[0].albedo_texture.nonNull()) // The albedo_texture check covers the pathological first-ever-addObject()-with-zero-splats case, where needed_splats (0) <= gpu_capacity_splats (0) would otherwise skip ever creating a texture at all.
		return;

	size_t new_tex_h = texHeightForSplatCount(needed_splats);
	const size_t cur_tex_h = texHeightForSplatCount(myMax<size_t>(1, gpu_capacity_splats));
	new_tex_h = myMax(new_tex_h, cur_tex_h * 2); // Headroom, so repeated small appends don't reallocate every time (like std::vector's doubling growth) - see class comment.

	const size_t new_capacity_splats = (splat_tex_width * new_tex_h) / texels_per_splat;

	// Repack every splat currently in the world into a fresh, bigger texture. Growth is rare (only when crossing a capacity threshold), so this O(total_splats) cost isn't paid on every addObject() call - see class comment.
	std::vector<float> texel_data(splat_tex_width * new_tex_h * 4, 0.f);
	for(size_t i = 0; i < total_splats; ++i)
	{
		const size_t base = i * texels_per_splat * 4;
		const Vec3f& pos   = world_positions[i];
		const Vec3f& scale = world_scales[i];
		const Vec4f& rot   = world_rotations[i];
		const Vec4f& col   = world_colours[i];

		float* const t = &texel_data[base];
		t[0] = pos.x;    t[1] = pos.y;    t[2] = pos.z;    t[3] = scale.x;
		t[4] = scale.y;  t[5] = scale.z;  t[6] = rot.x[0]; t[7] = rot.x[1];
		t[8] = rot.x[2]; t[9] = rot.x[3]; t[10] = col.x[0]; t[11] = col.x[1];
		t[12] = col.x[2]; t[13] = col.x[3];
	}

	world_ob->materials[0].albedo_texture = new OpenGLTexture(splat_tex_width, new_tex_h, &opengl_engine,
		ArrayRef<uint8>(reinterpret_cast<const uint8*>(texel_data.data()), texel_data.size() * sizeof(float)),
		Format_RGBA_Linear_Float,
		OpenGLTexture::Filtering_Nearest, // Must be Nearest: this is a data texture, not an image, and filtering/mipmapping would blend unrelated splats' attributes together.
		OpenGLTexture::Wrapping_Clamp,
		/*has_mipmaps=*/false);

	// Grow instance_index_vbo to match, in identity order - any in-flight sort's result will simply be reapplied (or dropped, if a removeObject() also happened - see structure_generation) as normal once it lands; there's no need to try to preserve the pre-growth sorted order here, since ensureGpuCapacity() is only ever called from addObject(), which already resets have_last_sort_cam_pos to force a fresh resort anyway.
	std::vector<uint32> identity_indices(new_capacity_splats);
	for(size_t i = 0; i < new_capacity_splats; ++i)
		identity_indices[i] = (uint32)i;
	instance_index_vbo = new VBO(identity_indices.data(), identity_indices.size() * sizeof(uint32), GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW);

	rebuildVAO(opengl_engine);

	// world_ob->vert_vao was just replaced above. If world_ob is already in the engine (i.e. this ISN'T the very first ensureGpuCapacity() call, made while
	// addObject() is still building world_ob for the first time - see its caller), the engine cached a draw-time VAO reference back when it was added
	// (OpenGLEngine::buildObjectData()/rebuildDenormalisedDrawData(), see object.vao there) that's now stale and would keep drawing the old, smaller/freed
	// VAO (or crash) - objectMaterialsUpdated() is the existing public hook (also used e.g. for the Hypercard texture-swap path in GUIClient.cpp) that
	// recomputes it from the current vert_vao. Must NOT be called on the very-first-call path: at that point world_ob hasn't been through
	// OpenGLEngine::addObject()'s buildObjectData() yet, so e.g. object.per_ob_vert_data_index isn't assigned and objectMaterialsUpdated() would misbehave.
	if(ob_already_in_engine)
		opengl_engine.objectMaterialsUpdated(*world_ob);

	gpu_capacity_splats = new_capacity_splats;
}


GLObjectRef GaussianSplatRenderer::addObject(const UID& world_object_id, const GaussianSplatDataRef& object_space_data, const Vec4f& translation_ws, const Quat<float>& rotation_ws,
	float uniform_scale_ws, OpenGLEngine& opengl_engine, const std::string& source_name)
{
	if(shader_prog.isNull())
		throw glare::Exception("GaussianSplatRenderer::addObject(): makeShaders() must be called first.");

	const bool creating_world_ob = world_ob.isNull();
	if(creating_world_ob)
	{
		// First ever splat object added to the world - create the shared "world splat cloud" GLObject. Its ob_to_world_matrix stays identity forever: every splat's position is baked into world space in the data itself (see class comment).
		// Deliberately NOT opengl_engine.addObject()'d yet - see below, after ensureGpuCapacity() has actually built a real VAO/texture for it.
		world_ob = new GLObject();
		world_ob->ob_to_world_matrix = Matrix4f::identity();
		world_ob->mesh_data = makeInstancedQuadMeshData(*opengl_engine.vert_buf_allocator);

		world_ob->materials.resize(1);
		OpenGLMaterial& mat = world_ob->materials[0];
		mat.shader_prog = shader_prog;
		mat.auto_assign_shader = false;
		mat.transparent = true; // Routes the object through OpenGLEngine::drawTransparentMaterialBatches() - see gaussian_splat_frag_shader.glsl for the blend mode this assumes.
		mat.user_uniform_vals.resize(3); // viewport_dims_px, focal_len_px, splat_tex_width - set by think().
		mat.user_uniform_vals[2].intval = (int)splat_tex_width;
		// mat.albedo_texture is set below by ensureGpuCapacity()'s first call.
	}

	const size_t num_new_splats = object_space_data->numSplats();
	const size_t old_total = total_splats;
	const size_t new_total = old_total + num_new_splats;

	// Bake object-space positions/scales/rotations into world space (see class comment for why the sort's snapshotting makes an append like this safe even while a sort is in flight).
	world_positions.resize(new_total);
	world_scales.resize(new_total);
	world_rotations.resize(new_total);
	world_colours.resize(new_total);

	js::AABBox new_range_aabb_ws = js::AABBox::emptyAABBox();
	for(size_t i = 0; i < num_new_splats; ++i)
	{
		const Vec3f& os_pos   = object_space_data->positions[i];
		const Vec3f& os_scale = object_space_data->scales[i];
		const Vec4f& os_rot   = object_space_data->rotations[i]; // (x, y, z, w)

		const Vec4f rotated = rotation_ws.rotateVector(Vec4f(uniform_scale_ws * os_pos.x, uniform_scale_ws * os_pos.y, uniform_scale_ws * os_pos.z, 0.f));
		const Vec4f world_pos = translation_ws + rotated; // translation_ws.w == 1, rotated.w == 0, so world_pos.w == 1, as a point should be.

		const Quat<float> os_quat(os_rot.x[0], os_rot.x[1], os_rot.x[2], os_rot.x[3]);
		const Quat<float> world_quat = rotation_ws * os_quat;

		world_positions[old_total + i] = toVec3f(world_pos);
		world_scales[old_total + i] = os_scale * uniform_scale_ws;
		world_rotations[old_total + i] = world_quat.v; // Quat::v is already (x, y, z, w), matching our storage convention.
		world_colours[old_total + i] = object_space_data->colours[i]; // Colour/opacity isn't affected by the object's pose.

		new_range_aabb_ws.enlargeToHoldPoint(world_pos);
	}

	total_splats = new_total;

	// Set num_instances_to_draw and enlarge the world AABB *before* possibly opengl_engine.addObject()-ing world_ob below, so that call never sees a stale
	// instance count or the placeholder empty box makeInstancedQuadMeshData() started it as - see class comment's "Growable GPU buffers" paragraph for the
	// general "fully ready before add" principle this follows (matches the original per-object design's ordering, which set these before its own addObject() call).
	world_ob->num_instances_to_draw = (int)new_total;
	world_ob->mesh_data->aabb_os.enlargeToHoldAABBox(new_range_aabb_ws); // Only ever enlarged incrementally here - removeObject() recomputes it exactly on removal, see rebuildWorldAABB().

	ensureGpuCapacity(new_total, opengl_engine, /*ob_already_in_engine=*/!creating_world_ob); // May repack + reupload everything (rare); if it does, the partial upload below is redundant but harmless (same data).

	// Only NOW does world_ob have a real VAO (instance-index attribute bound) and a real data texture - see ensureGpuCapacity()/rebuildVAO(). Adding it to the
	// engine any earlier would have left OpenGLEngine::buildObjectData() caching a stale (mesh-default, non-instanced) VAO reference that a later VAO rebuild
	// can't retroactively fix - see the comment on ensureGpuCapacity()'s ob_already_in_engine param.
	if(creating_world_ob)
		opengl_engine.addObject(world_ob);

	uploadTexelRowsForSplatRange(old_total, num_new_splats); // Partial upload of just the newly-appended rows - if ensureGpuCapacity() just repacked everything above, this re-uploads the same (correct) data for those rows again, which is harmless.

	// Extend instance_index_vbo's identity-order tail to cover the newly appended splats (ensureGpuCapacity(), if it ran, already rebuilt the whole thing in identity order, making this redundant-but-harmless too - same reasoning as above).
	{
		std::vector<uint32> new_indices(num_new_splats);
		for(size_t i = 0; i < num_new_splats; ++i)
			new_indices[i] = (uint32)(old_total + i);
		instance_index_vbo->updateData(old_total * sizeof(uint32), new_indices.data(), new_indices.size() * sizeof(uint32));
	}

	opengl_engine.updateObjectTransformData(*world_ob); // Refreshes aabb_ws from the aabb_os enlarged above; ob_to_world_matrix itself never changes (stays identity). Redundant but harmless on the very first call (buildObjectData(), inside the addObject() call above, already computed aabb_ws fresh from the by-then-already-correct aabb_os).

	WorldSplatEntry entry;
	entry.world_object_id = world_object_id;
	entry.object_space_data = object_space_data;
	entry.offset = old_total;
	entry.count = num_new_splats;
	entries.push_back(entry);

	have_last_sort_cam_pos = false; // Force a fresh resort next think() - the newly-appended splats are in arbitrary (identity) order relative to the rest.
	(void)source_name; // Currently only surfaced in aggregate via getPerfStats(); kept as a parameter for future per-source breakdown / logging.

	return world_ob;
}


bool GaussianSplatRenderer::updateObjectTransform(const UID& world_object_id, const Vec4f& translation_ws, const Quat<float>& rotation_ws, float uniform_scale_ws, OpenGLEngine& opengl_engine)
{
	for(size_t e = 0; e < entries.size(); ++e)
	{
		if(entries[e].world_object_id == world_object_id)
		{
			WorldSplatEntry& entry = entries[e];
			const GaussianSplatData& os_data = *entry.object_space_data;

			js::AABBox new_range_aabb_ws = js::AABBox::emptyAABBox();
			for(size_t i = 0; i < entry.count; ++i)
			{
				const Vec3f& os_pos   = os_data.positions[i];
				const Vec3f& os_scale = os_data.scales[i];
				const Vec4f& os_rot   = os_data.rotations[i];

				const Vec4f rotated = rotation_ws.rotateVector(Vec4f(uniform_scale_ws * os_pos.x, uniform_scale_ws * os_pos.y, uniform_scale_ws * os_pos.z, 0.f));
				const Vec4f world_pos = translation_ws + rotated;

				const Quat<float> os_quat(os_rot.x[0], os_rot.x[1], os_rot.x[2], os_rot.x[3]);
				const Quat<float> world_quat = rotation_ws * os_quat;

				world_positions[entry.offset + i] = toVec3f(world_pos);
				world_scales[entry.offset + i] = os_scale * uniform_scale_ws;
				world_rotations[entry.offset + i] = world_quat.v; // Quat::v is already (x, y, z, w), matching our storage convention.
				// Colour is unchanged - re-baking never touches world_colours.

				new_range_aabb_ws.enlargeToHoldPoint(world_pos);
			}

			uploadTexelRowsForSplatRange(entry.offset, entry.count);

			world_ob->mesh_data->aabb_os.enlargeToHoldAABBox(new_range_aabb_ws); // Conservative: only grows. A full shrink-to-fit recompute happens on removeObject() instead - see rebuildWorldAABB().
			opengl_engine.updateObjectTransformData(*world_ob);

			have_last_sort_cam_pos = false; // This entry's splats may now be in the wrong depth order relative to the rest of the world - force a fresh resort. (Doesn't bump structure_generation: offsets/counts are unchanged, so any in-flight sort's indices remain meaningful - see class comment.)

			return true;
		}
	}
	return false;
}


bool GaussianSplatRenderer::isSplatObject(const UID& world_object_id) const
{
	for(size_t e = 0; e < entries.size(); ++e)
		if(entries[e].world_object_id == world_object_id)
			return true;
	return false;
}


bool GaussianSplatRenderer::removeObject(const UID& world_object_id)
{
	for(size_t e = 0; e < entries.size(); ++e)
	{
		if(entries[e].world_object_id == world_object_id)
		{
			const size_t offset = entries[e].offset;
			const size_t count = entries[e].count;

			world_positions.erase(world_positions.begin() + offset, world_positions.begin() + offset + count);
			world_scales.erase(world_scales.begin() + offset, world_scales.begin() + offset + count);
			world_rotations.erase(world_rotations.begin() + offset, world_rotations.begin() + offset + count);
			world_colours.erase(world_colours.begin() + offset, world_colours.begin() + offset + count);

			entries.erase(entries.begin() + e);
			for(size_t j = 0; j < entries.size(); ++j)
				if(entries[j].offset > offset)
					entries[j].offset -= count;

			total_splats -= count;
			structure_generation++; // Invalidates any in-flight sort's result - see class comment.

			// Full re-upload: repack everything that's left (capacity/texture size is unchanged - removal never shrinks GPU storage, see class comment's "не переусложнять" stance on capacity management).
			uploadTexelRowsForSplatRange(0, total_splats);

			std::vector<uint32> identity_indices(total_splats);
			for(size_t i = 0; i < total_splats; ++i)
				identity_indices[i] = (uint32)i;
			if(total_splats > 0)
				instance_index_vbo->updateData(0, identity_indices.data(), identity_indices.size() * sizeof(uint32));

			world_ob->num_instances_to_draw = (int)total_splats;

			rebuildWorldAABB();

			have_last_sort_cam_pos = false; // Force a fresh resort of the now-renumbered world.

			return true;
		}
	}
	return false;
}


void GaussianSplatRenderer::rebuildWorldAABB()
{
	js::AABBox aabb = js::AABBox::emptyAABBox();
	for(size_t i = 0; i < total_splats; ++i)
		aabb.enlargeToHoldPoint(Vec4f(world_positions[i].x, world_positions[i].y, world_positions[i].z, 1.f));
	world_ob->mesh_data->aabb_os = aabb;
}


void GaussianSplatRenderer::think(OpenGLEngine& opengl_engine, glare::TaskManager& task_manager)
{
	if(world_ob.isNull())
		return;

	// Drain any completed background sort result (non-blocking) and write it straight to instance_index_vbo - the only GL call in this whole depth-sort
	// pipeline, which is why it has to happen here on the main/GL thread rather than in the worker task itself (see VBO::updateData()/VBO.cpp - no thread-safety of its own).
	{
		js::Vector<Reference<ThreadMessage>, 16> completed_msgs;
		sort_result_queue.dequeueAnyQueuedItems(completed_msgs);
		for(size_t i = 0; i < completed_msgs.size(); ++i)
		{
			const GaussianSplatSortResultMsg* msg = static_cast<const GaussianSplatSortResultMsg*>(completed_msgs[i].ptr());

			// If a later message in this same batch is for the same (still-current) generation, that one supersedes this one - both stages of a sort can land
			// in the same frame on a fast-sorting world, and uploading the coarse order only to overwrite it with the precise order in the same frame is a
			// pointless (and, at millions of splats, far from free) buffer upload. The timing stats below are still recorded either way.
			bool superseded_this_frame = false;
			for(size_t j = i + 1; j < completed_msgs.size(); ++j)
				if(static_cast<const GaussianSplatSortResultMsg*>(completed_msgs[j].ptr())->generation == msg->generation)
				{
					superseded_this_frame = true;
					break;
				}

			if(msg->stage == GaussianSplatSortResultMsg::Stage_Coarse)
				last_coarse_sort_duration_s = msg->sort_duration_s;
			else
			{
				sort_in_flight = false;
				last_sort_duration_s = msg->sort_duration_s;
				num_sorts_completed++;
			}

			// A removeObject() since this sort was kicked off has renumbered the world - this result's indices no longer mean the same splats. Drop it, per the class comment.
			if(msg->generation != structure_generation)
				continue;

			if(!superseded_this_frame)
			{
				const js::Vector<uint32, 16>& sorted_indices = msg->sortedIndices();
				// The snapshot this was computed from may be a (strict prefix) of the current, possibly-since-grown world - see class comment. Only ever write as many bytes as the result actually covers; any appended tail beyond it already holds valid identity-order indices written by addObject() itself.
				instance_index_vbo->updateData(0, sorted_indices.data(), sorted_indices.size() * sizeof(uint32));
			}
		}
	}

	const Vec2i viewport_dims = opengl_engine.getViewportDims();
	const OpenGLScene* scene = opengl_engine.getCurrentScene();
	// Focal length in pixels, derived the same way as OpenGLEngine's own screen-space projections (see e.g. OpenGLEngine::getPixelForPoint()/l_over_w, l_over_h): focal_px = viewport_px * (lens_sensor_dist / sensor_size).
	const float focal_x = (float)viewport_dims.x * scene->lens_sensor_dist / scene->use_sensor_width;
	const float focal_y = (float)viewport_dims.y * scene->lens_sensor_dist / scene->use_sensor_height;

	const Vec4f cam_pos_ws = scene->cam_to_world.getColumn(3);

	OpenGLMaterial& mat = world_ob->materials[0];
	mat.user_uniform_vals[0].vec2 = Vec2f((float)viewport_dims.x, (float)viewport_dims.y);
	mat.user_uniform_vals[1].vec2 = Vec2f(focal_x, focal_y);
	// user_uniform_vals[2] (splat_tex_width) is constant, set once above.

	// Depth-sort (architecture contract task #7): kick off a background re-sort only if one isn't already in flight, and the camera has moved far enough
	// since the last one was kicked off to be worth it. Camera rotation deliberately isn't tracked: the sort orders splats by distance from the camera, which
	// rotating on the spot doesn't change - see resort_move_threshold_ws's comment above and the class comment in GaussianSplatRenderer.h.
	const bool moved_enough = !have_last_sort_cam_pos || cam_pos_ws.getDist(last_sort_cam_pos_ws) >= resort_move_threshold_ws;
	if(total_splats > 0 && !sort_in_flight && moved_enough)
	{
		Matrix4f world_to_cam;
		scene->cam_to_world.getInverseForAffine3Matrix(world_to_cam); // world_to_camera_space_matrix itself is private to OpenGLScene; cam_to_world (its inverse, public) is available instead.

		if(sort_scratch.isNull())
			sort_scratch = new GaussianSplatSortScratch();

		// Freeze a snapshot of the current world positions on the main thread, synchronously, before handing off to the worker - see the concurrency note in the class comment for why the worker must never touch the live, growable world_positions vector directly.
		sort_scratch->positions_snapshot.resizeNoCopy(total_splats);
		std::memcpy(sort_scratch->positions_snapshot.data(), world_positions.data(), total_splats * sizeof(Vec3f));

		sort_in_flight = true;
		have_last_sort_cam_pos = true;
		last_sort_cam_pos_ws = cam_pos_ws;

		task_manager.addTask(new GaussianSplatSortTask(structure_generation, sort_scratch, world_to_cam, &sort_result_queue));
	}
}


void GaussianSplatRenderer::getPerfStats(std::vector<PerfStats>& stats_out) const
{
	stats_out.clear();
	if(world_ob.isNull())
		return;

	PerfStats s;
	s.source_name = toString(entries.size()) + " splat object(s) merged into world cloud";
	s.num_splats = total_splats;
	s.last_coarse_sort_duration_s = last_coarse_sort_duration_s;
	s.last_sort_duration_s = last_sort_duration_s;
	s.num_sorts_completed = num_sorts_completed;
	stats_out.push_back(s);
}


void GaussianSplatRenderer::shutdown()
{
	world_ob = NULL;
	instance_index_vbo = NULL;
	entries.clear();
	world_positions.clear();
	world_scales.clear();
	world_rotations.clear();
	world_colours.clear();
	total_splats = 0;
	gpu_capacity_splats = 0;
	shader_prog = NULL;
}
