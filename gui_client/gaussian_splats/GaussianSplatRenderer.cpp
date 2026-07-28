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
#include <utils/ConPrint.h>
#include <utils/Exception.h>
#include <utils/StringUtils.h>
#include <utils/Sort.h>
#include <utils/Task.h>
#include <utils/TaskManager.h>
#include <utils/Timer.h>
#include <assert.h>
#include <algorithm>


// Camera position must move at least this far (in world-space metres) since an object's last kicked-off depth-sort before another one is worth kicking off - avoids
// resorting every single frame for a static or near-static viewpoint. See class comment in GaussianSplatRenderer.h for why this is a reasonable simplification
// (rotation-only viewpoint changes can also affect back-to-front order, but a pure position threshold is the standard, cheap approximation used elsewhere for this).
static const float resort_move_threshold_ws = 0.1f;


namespace
{


const size_t texels_per_splat = 4; // See class comment in GaussianSplatRenderer.h and gaussian_splat_vert_shader.glsl for the texel layout this implies.
const size_t splat_tex_width = 4096; // 4096 gives ~16.7M splat capacity on GPUs with GL_MAX_TEXTURE_SIZE >= 16384 (common). WebGL2 only guarantees 2048, but real hardware consistently reports much more.
const int splat_index_attribute_loc = 1; // Forced via bindAttributeLocation() in GaussianSplatRenderer::makeShaders() - slot 1 is otherwise used for "normal_in", which splats have no use for.


// Packs splat_data into a flat RGBA32F texel buffer, 4 texels (16 floats) per splat:
//   texel 0: (pos.x,   pos.y,   pos.z,   scale.x)
//   texel 1: (scale.y, scale.z, rot.x,   rot.y)
//   texel 2: (rot.z,   rot.w,   colour.r, colour.g)
//   texel 3: (colour.b, opacity, 0,       0)         (last two floats reserved, e.g. for higher-order SH later)
void packSplatDataToTexels(const GaussianSplatData& splat_data, std::vector<float>& texel_data_out, size_t& tex_h_out)
{
	const size_t num_splats = splat_data.numSplats();
	const size_t total_texels = num_splats * texels_per_splat;
	const size_t tex_h = myMax<size_t>(1, Maths::roundedUpDivide(total_texels, splat_tex_width));

	texel_data_out.assign(splat_tex_width * tex_h * 4, 0.f);

	for(size_t i = 0; i < num_splats; ++i)
	{
		const size_t base = i * texels_per_splat * 4; // 4 floats per texel * 4 texels per splat

		const Vec3f& pos   = splat_data.positions[i];
		const Vec3f& scale = splat_data.scales[i];
		const Vec4f& rot   = splat_data.rotations[i]; // (x, y, z, w)
		const Vec4f& col   = splat_data.colours[i]; // (r, g, b, opacity)

		float* const t = &texel_data_out[base];
		t[0] = pos.x;    t[1] = pos.y;    t[2] = pos.z;    t[3] = scale.x;
		t[4] = scale.y;  t[5] = scale.z;  t[6] = rot.x[0]; t[7] = rot.x[1];
		t[8] = rot.x[2]; t[9] = rot.x[3]; t[10] = col.x[0]; t[11] = col.x[1];
		t[12] = col.x[2]; t[13] = col.x[3]; // t[14], t[15] left as zero.
	}

	tex_h_out = tex_h;
}


Reference<OpenGLTexture> makeSplatDataTexture(const GaussianSplatData& splat_data, OpenGLEngine& opengl_engine)
{
	std::vector<float> texel_data;
	size_t tex_h;
	packSplatDataToTexels(splat_data, texel_data, tex_h);

	return new OpenGLTexture(splat_tex_width, tex_h, &opengl_engine,
		ArrayRef<uint8>(reinterpret_cast<const uint8*>(texel_data.data()), texel_data.size() * sizeof(float)),
		Format_RGBA_Linear_Float,
		OpenGLTexture::Filtering_Nearest, // Must be Nearest: this is a data texture, not an image, and filtering/mipmapping would blend unrelated splats' attributes together.
		OpenGLTexture::Wrapping_Clamp,
		/*has_mipmaps=*/false);
}


// The instanced quad's geometry: a local-space unit square. The vertex shader scales/orients this per-splat based on the projected 2D covariance (EWA splatting), so this local shape only needs to bound [-1, 1] in both axes.
Reference<OpenGLMeshRenderData> makeInstancedQuadMeshData(VertexBufferAllocator& allocator, const js::AABBox& aabb_os)
{
	Reference<OpenGLMeshRenderData> mesh_data = new OpenGLMeshRenderData();
	mesh_data->setIndexType(GL_UNSIGNED_SHORT);
	mesh_data->has_uvs = false;
	mesh_data->has_shading_normals = false;
	mesh_data->num_materials_referenced = 1;
	mesh_data->aabb_os = aabb_os; // Whole splat cloud's extent (over splat centres - see GaussianSplatData.h), not the local quad's - splat position comes from the GPU texture, not this mesh's geometry.

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

	// Placeholder for the per-instance splat-index attribute - disabled and with no VBO of its own until GaussianSplatRenderer::createObject() builds the per-object instance-index VBO and rebuilds ob->vert_vao with it enabled.
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
// Matched back up to a ManagedObject by "object_id" (a stable id, not a raw pointer - managed_objects is a std::vector, so element addresses move on push_back) in think();
// if the object no longer exists (removed) or a newer sort has since superseded this one, think() just drops it - see class comment in GaussianSplatRenderer.h.
class GaussianSplatSortResultMsg : public ThreadMessage
{
public:
	uint64 object_id;
	std::vector<uint32> sorted_indices; // Back-to-front (farthest first) instance order, ready to write directly into the object's instance-index VBO.
	double sort_duration_s;
};


// Sorts one splat cloud's instances back-to-front by camera distance, entirely on a worker thread (glare::TaskManager) - no GL calls here, see class comment in GaussianSplatRenderer.h.
class GaussianSplatSortTask : public glare::Task
{
public:
	GaussianSplatSortTask(uint64 object_id_, const GaussianSplatDataRef& splat_data_, const Matrix4f& ob_to_cam_, ThreadSafeQueue<Reference<ThreadMessage> >* result_queue_)
	:	object_id(object_id_), splat_data(splat_data_), ob_to_cam(ob_to_cam_), result_queue(result_queue_)
	{}

	virtual void run(size_t /*thread_index*/) override
	{
		Timer timer;

		const std::vector<Vec3f>& positions = splat_data->positions;
		const size_t num_splats = positions.size();

		// Sort key: farthest splat first (back-to-front, for correct premultiplied-alpha "over" blending - see architecture contract §2.G).
		// Sort::flippedKey() maps a (possibly negative) float to a uint32 that sorts in the same order as the float itself; negating from UINT32_MAX before that gives us descending-depth-first
		// out of an ascending radix sort, the same trick OpenGLEngine.cpp's sortBatchDrawInfoWithDists() uses for its own back-to-front object sort (see its "distval = max - dist_i").
		struct SortItem
		{
			uint32 key;
			uint32 splat_index;
		};
		struct SortItemGetKey { inline uint32 operator () (const SortItem& item) const { return item.key; } };

		std::vector<SortItem> items(num_splats);
		for(size_t i = 0; i < num_splats; ++i)
		{
			const Vec3f& p = positions[i];
			const Vec4f pos_cs = ob_to_cam * Vec4f(p.x, p.y, p.z, 1.f);
			// Camera space here is the engine's raw (unconverted) convention, y-forwards (see OpenGLScene::world_to_camera_space_matrix's doc comment) - this is genuine forward depth,
			// unaffected by the indigo_to_opengl_cam_matrix conversion the *shader's* view_matrix uniform gets (see snapshots/2026-07-27-session6-depth-sort-and-axis-convention-fix.md).
			const float depth = pos_cs[1];
			items[i].key = std::numeric_limits<uint32>::max() - Sort::flippedKey(depth);
			items[i].splat_index = (uint32)i;
		}

		std::vector<SortItem> scratch(num_splats);
		std::vector<uint32> temp_counts(6144); // Required size for Sort::radixSort32BitKey(), see its doc comment in Sort.h.
		Sort::radixSort32BitKey(items.data(), scratch.data(), num_splats, SortItemGetKey(), temp_counts.data(), temp_counts.size());

		Reference<GaussianSplatSortResultMsg> msg = new GaussianSplatSortResultMsg();
		msg->object_id = object_id;
		msg->sorted_indices.resize(num_splats);
		for(size_t i = 0; i < num_splats; ++i)
			msg->sorted_indices[i] = items[i].splat_index;
		msg->sort_duration_s = timer.elapsed();

		result_queue->enqueue(msg);
	}

private:
	uint64 object_id;
	GaussianSplatDataRef splat_data; // Keeps the splat position data alive while this task runs on a worker thread, independent of whether the main thread still has it referenced.
	Matrix4f ob_to_cam;
	ThreadSafeQueue<Reference<ThreadMessage> >* result_queue;
};


} // end anonymous namespace


GaussianSplatRenderer::GaussianSplatRenderer()
:	next_object_id(1)
{}


void GaussianSplatRenderer::makeShaders(OpenGLEngine& opengl_engine, const std::string& shader_dir)
{
	const std::string version_directive    = opengl_engine.getVersionDirective();
	const std::string preprocessor_defines = opengl_engine.getPreprocessorDefines();

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
	// Force it to a known location (1, the otherwise-unused "normal_in" slot - splats have no normals) and relink, so createObject() can build the instance-index VertexAttrib at a location we know ahead of time.
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
	// hardware/drivers commonly support much larger textures than the spec minimum (e.g. the ~5M-splat Copiapo_park2.sog test file rendered fine, well past
	// what a 2048-height cap would allow) - see packSplatDataToTexels() for the packing this mirrors.
	const size_t max_tex_h = (size_t)myMax(1, gl_max_texture_size);
	return (splat_tex_width * max_tex_h) / texels_per_splat;
}


GLObjectRef GaussianSplatRenderer::createObject(const GaussianSplatDataRef& splat_data, OpenGLEngine& opengl_engine, const std::string& source_name)
{
	if(shader_prog.isNull())
		throw glare::Exception("GaussianSplatRenderer::createObject(): makeShaders() must be called first.");

	const size_t num_splats = splat_data->numSplats();

	GLObjectRef ob = new GLObject();
	ob->mesh_data = makeInstancedQuadMeshData(*opengl_engine.vert_buf_allocator, splat_data->aabb_os);
	ob->num_instances_to_draw = (int)num_splats;

	// Build the per-object instance-index VBO. Initial order is identity (splat i drawn as instance i); a later CPU depth-sort (architecture contract task #7) will reorder this back-to-front each frame via VBO::updateData().
	std::vector<uint32> instance_indices(num_splats);
	for(size_t i = 0; i < num_splats; ++i)
		instance_indices[i] = (uint32)i;
	Reference<VBO> instance_index_vbo = new VBO(instance_indices.data(), instance_indices.size() * sizeof(uint32), GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW);

	// Rebuild the VAO with the instance-index attribute enabled and bound to instance_index_vbo. Mirrors GLObject::enableInstancing()'s approach (OpenGLEngine.cpp), just with our own attribute instead of the hardcoded instance-matrix one.
	VertexSpec vertex_spec = ob->mesh_data->vertex_spec;
	vertex_spec.attributes[splat_index_attribute_loc].vbo = instance_index_vbo;
	vertex_spec.attributes[splat_index_attribute_loc].enabled = true;
#if DO_INDIVIDUAL_VAO_ALLOC
	ob->vert_vao = new VAO(ob->mesh_data->vbo_handle.vbo, ob->mesh_data->indices_vbo_handle.index_vbo, vertex_spec);
#else
	ob->vert_vao = new VAO(vertex_spec);
#endif
	ob->instance_matrix_vbo = instance_index_vbo; // Keep the VBO referenced-alive via the object; also gives think()/a future depth-sort step access to it for VBO::updateData().

	ob->materials.resize(1);
	OpenGLMaterial& mat = ob->materials[0];
	mat.albedo_texture = makeSplatDataTexture(*splat_data, opengl_engine);
	mat.shader_prog = shader_prog;
	mat.auto_assign_shader = false;
	mat.transparent = true; // Routes the object through OpenGLEngine::drawTransparentMaterialBatches() - see gaussian_splat_frag_shader.glsl for the blend mode this assumes.
	mat.user_uniform_vals.resize(3); // viewport_dims_px, focal_len_px, splat_tex_width - set by think().
	mat.user_uniform_vals[2].intval = (int)splat_tex_width;

	ManagedObject managed_ob;
	managed_ob.id = next_object_id++;
	managed_ob.source_name = source_name;
	managed_ob.ob = ob;
	managed_ob.splat_data = splat_data;
	managed_ob.instance_index_vbo = instance_index_vbo;
	managed_ob.sort_in_flight = false;
	managed_ob.have_last_sort_cam_pos = false;
	managed_ob.last_sort_duration_s = -1.0;
	managed_ob.num_sorts_completed = 0;
	managed_objects.push_back(managed_ob);
	return ob;
}


void GaussianSplatRenderer::think(OpenGLEngine& opengl_engine, glare::TaskManager& task_manager)
{
	if(managed_objects.empty())
		return;

	// Drain any completed background sort results (non-blocking) and write them straight to each object's instance-index VBO - the only GL call in this whole depth-sort
	// pipeline, which is why it has to happen here on the main/GL thread rather than in the worker task itself (see VBO::updateData()/VBO.cpp - no thread-safety of its own).
	{
		js::Vector<Reference<ThreadMessage>, 16> completed_msgs;
		sort_result_queue.dequeueAnyQueuedItems(completed_msgs);
		for(size_t i = 0; i < completed_msgs.size(); ++i)
		{
			const GaussianSplatSortResultMsg* msg = static_cast<const GaussianSplatSortResultMsg*>(completed_msgs[i].ptr());

			// Find the managed object this result belongs to, by stable id (not index - createObject()'s push_back can reallocate managed_objects and move elements around).
			// If not found, the object has since been removed - just drop the (now-meaningless) result, per the class comment in GaussianSplatRenderer.h.
			for(size_t q = 0; q < managed_objects.size(); ++q)
			{
				ManagedObject& managed_ob = managed_objects[q];
				if(managed_ob.id == msg->object_id)
				{
					managed_ob.sort_in_flight = false;
					managed_ob.last_sort_duration_s = msg->sort_duration_s;
					managed_ob.num_sorts_completed++;
					// NOTE: if the object's splat count somehow changed since the sort was kicked off (it can't currently - GaussianSplatData is immutable after loading), a size
					// mismatch here would corrupt the VBO. Left unguarded because that precondition can't occur with the current, load-once splat pipeline.
					managed_ob.instance_index_vbo->updateData(msg->sorted_indices.data(), msg->sorted_indices.size() * sizeof(uint32));
					break;
				}
			}
		}
	}

	const Vec2i viewport_dims = opengl_engine.getViewportDims();
	const OpenGLScene* scene = opengl_engine.getCurrentScene();
	// Focal length in pixels, derived the same way as OpenGLEngine's own screen-space projections (see e.g. OpenGLEngine::getPixelForPoint()/l_over_w, l_over_h): focal_px = viewport_px * (lens_sensor_dist / sensor_size).
	const float focal_x = (float)viewport_dims.x * scene->lens_sensor_dist / scene->use_sensor_width;
	const float focal_y = (float)viewport_dims.y * scene->lens_sensor_dist / scene->use_sensor_height;

	const Vec4f cam_pos_ws = scene->cam_to_world.getColumn(3);

	for(size_t i = 0; i < managed_objects.size(); ++i)
	{
		ManagedObject& managed_ob = managed_objects[i];
		OpenGLMaterial& mat = managed_ob.ob->materials[0];
		mat.user_uniform_vals[0].vec2 = Vec2f((float)viewport_dims.x, (float)viewport_dims.y);
		mat.user_uniform_vals[1].vec2 = Vec2f(focal_x, focal_y);
		// user_uniform_vals[2] (splat_tex_width) is constant, set once in createObject().

		// Depth-sort (architecture contract task #7): kick off a background re-sort only if one isn't already in flight for this object, and the camera has moved far enough
		// since the last one was kicked off to be worth it - see resort_move_threshold_ws's comment above and the class comment in GaussianSplatRenderer.h for why a plain
		// world-space position threshold (not also tracking rotation) is an accepted simplification here.
		const bool moved_enough = !managed_ob.have_last_sort_cam_pos || cam_pos_ws.getDist(managed_ob.last_sort_cam_pos_ws) >= resort_move_threshold_ws;
		if(!managed_ob.sort_in_flight && moved_enough)
		{
			Matrix4f world_to_cam;
			scene->cam_to_world.getInverseForAffine3Matrix(world_to_cam); // world_to_camera_space_matrix itself is private to OpenGLScene; cam_to_world (its inverse, public) is available instead.
			const Matrix4f ob_to_cam = world_to_cam * managed_ob.ob->ob_to_world_matrix;

			managed_ob.sort_in_flight = true;
			managed_ob.have_last_sort_cam_pos = true;
			managed_ob.last_sort_cam_pos_ws = cam_pos_ws;

			task_manager.addTask(new GaussianSplatSortTask(managed_ob.id, managed_ob.splat_data, ob_to_cam, &sort_result_queue));
		}
	}
}


void GaussianSplatRenderer::getPerfStats(std::vector<PerfStats>& stats_out) const
{
	stats_out.resize(managed_objects.size());
	for(size_t i = 0; i < managed_objects.size(); ++i)
	{
		stats_out[i].source_name = managed_objects[i].source_name;
		stats_out[i].num_splats = managed_objects[i].splat_data->numSplats();
		stats_out[i].last_sort_duration_s = managed_objects[i].last_sort_duration_s;
		stats_out[i].num_sorts_completed = managed_objects[i].num_sorts_completed;
	}
}


void GaussianSplatRenderer::removeObject(const GLObjectRef& ob)
{
	for(size_t i = 0; i < managed_objects.size(); ++i)
	{
		if(managed_objects[i].ob.ptr() == ob.ptr())
		{
			managed_objects.erase(managed_objects.begin() + i);
			return;
		}
	}
}


void GaussianSplatRenderer::shutdown()
{
	managed_objects.clear();
	shader_prog = NULL;
}
