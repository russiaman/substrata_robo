/*=====================================================================
LoadModelTask.cpp
-----------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "LoadModelTask.h"


#include "LoadTextureTask.h"
#include "ThreadMessages.h"
#include "ModelLoading.h"
#include "gaussian_splats/GaussianSplatLoader.h"
#include "gaussian_splats/GaussianSplatLodTree.h"
#include "../shared/ResourceManager.h"
#include <opengl/OpenGLEngine.h>
#include <opengl/OpenGLMeshRenderData.h>
#include <utils/LimitedAllocator.h>
#include <utils/ConPrint.h>
#include <utils/PlatformUtils.h>
#include <utils/FileUtils.h>
#include <utils/UniqueRef.h>
#include <utils/MemMappedFile.h>
#include <utils/Mutex.h>
#include <utils/Lock.h>
#include <graphics/FormatDecoderSubVox.h>
#include <tracy/Tracy.hpp>
#include <unordered_map>


namespace
{


// Cache of already-decoded-and-LoD-tree-built Gaussian splat clouds, keyed by model URL (which is content-hash-based - see ResourceManager::URLForPathAndHash() - so same URL always means same file bytes,
// making this safe to share the same GaussianSplatDataRef across every WorldObject that happens to reference that URL). Exists because loadModelForObject() (GUIClient.cpp) re-triggers a LoadModelTask for an
// object whenever its *raw* object LOD level changes (crosses a getLODLevel() distance band), even though the *model* LOD level it actually resolves to is unaffected (splats have no model-LOD-level variants
// at all, max_model_lod_level == 0) - the model-LOD-level-based skip-if-unchanged check in loadModelForObject() only applies inside the ObjectType_Generic branch, downstream of that raw-level check, so for an
// object sitting near a raw LOD-level boundary this can still fire repeatedly on small camera movements. Without this cache, each of those redundant reloads would redecode the whole file and rebuild its LoD
// tree from scratch, and (worse, from a user's perspective) re-show the "Building..." indicator every time - found 2026-08-04 when a splat placed within a couple of metres of a LOD-level boundary made the
// indicator pop up on every few steps. Cheap fix: skip straight to the cached result instead of re-decoding/re-building.
Mutex g_gaussian_splat_cache_mutex;
std::unordered_map<std::string, GaussianSplatDataRef> g_gaussian_splat_cache GUARDED_BY(g_gaussian_splat_cache_mutex);


} // end anonymous namespace


LoadModelTask::LoadModelTask()
:	build_physics_ob(true),
	build_dynamic_physics_ob(false),
	model_lod_level(-1),
	need_lightmap_uvs(false)
{}


LoadModelTask::~LoadModelTask()
{}


void LoadModelTask::run(size_t thread_index)
{
	ZoneScopedN("LoadModelTask"); // Tracy profiler
	
	for(int attempt = 0; attempt < 10; ++attempt)
	{
		try
		{
			Reference<OpenGLMeshRenderData> gl_meshdata;
			PhysicsShape physics_shape;
			int subsample_factor = 1; // computed when loading voxels

			if(compressed_voxels)
			{
				ZoneText("Voxel", 5);

				assert(compressed_voxels->size() > 0);

				VoxelGroup voxel_group;
				voxel_group.voxels.setAllocator(worker_allocator);
				WorldObject::decompressVoxelGroup(compressed_voxels->data(), compressed_voxels->size(), worker_allocator.ptr(), /*decompressed group out=*/voxel_group);

				const int max_model_lod_level = (voxel_group.voxels.size() > 256) ? 2 : 0;
				const int use_model_lod_level = myMin(model_lod_level, max_model_lod_level);

				if(use_model_lod_level == 1)
					subsample_factor = 2;
				else if(use_model_lod_level == 2)
					subsample_factor = 4;

				// conPrint("Loading vox model for ob with UID " + voxel_ob->uid.toString() + " for LOD level " + toString(use_model_lod_level) + ", using subsample_factor " + toString(subsample_factor) + ", " + toString(voxel_group.voxels.size()) + " voxels");

				gl_meshdata = ModelLoading::makeModelForVoxelGroup(voxel_group, subsample_factor, ob_to_world_matrix, /*vert_buf_allocator=*/NULL, /*do_opengl_stuff=*/false, 
					need_lightmap_uvs, mat_transparent, build_dynamic_physics_ob, worker_allocator.ptr(), /*physics shape out=*/physics_shape);
			}
			else // Else not voxel ob, just loading a model:
			{
				ZoneText(lod_model_url.c_str(), lod_model_url.size());

				assert(!lod_model_url.empty());
				runtimeCheck(resource.nonNull() && resource_manager.nonNull());

				// conPrint("LoadModelTask: loading mesh with URL '" + lod_model_url + "'.");

				const std::string lod_model_path = resource_manager->getLocalAbsPathForResource(*this->resource);

				UniqueRef<MemMappedFile> file;
				ArrayRef<uint8> model_buffer;
#if EMSCRIPTEN
				if(resource->external_resource)
				{
					// conPrint("LoadModelTask: '" + lod_model_url + "' is an external_resource, using MemMappedFile...");
					file.set(new MemMappedFile(lod_model_path));
					model_buffer = ArrayRef<uint8>((const uint8*)file->fileData(), file->fileSize());
				}
				else
				{
					// Use the in-memory buffer that we loaded in EmscriptenResourceDownloader
					if(!loaded_buffer)
						conPrint("LoadModelTask: loaded_buffer is null for resource with URL '" + toStdString(lod_model_url) + "'");
					runtimeCheck(loaded_buffer.nonNull());
					model_buffer = ArrayRef<uint8>((const uint8*)loaded_buffer->buffer, loaded_buffer->buffer_size);
				}
#else
				// We want to load and build the mesh at lod_model_url.
			
				file.set(new MemMappedFile(lod_model_path));
				model_buffer = ArrayRef<uint8>((const uint8*)file->fileData(), file->fileSize());
#endif

				if(hasExtension(lod_model_path, "sog"))
				{
					// Gaussian splat cloud: just decode it (CPU-only work, no GL calls) and send it straight back - no mesh/physics geometry to build, so skip the rest of the pipeline below
					// (vert/index data extraction, upload_thread/VBO-pool path) entirely. See GUIClient::handleUploadedGaussianSplat() for the consuming side.
					Reference<ModelLoadedThreadMessage> msg = new ModelLoadedThreadMessage();

					const std::string cache_key = toStdString(lod_model_url);
					GaussianSplatDataRef cached_splat_data;
					{
						Lock lock(g_gaussian_splat_cache_mutex);
						auto it = g_gaussian_splat_cache.find(cache_key);
						if(it != g_gaussian_splat_cache.end())
							cached_splat_data = it->second;
					}

					if(cached_splat_data.nonNull())
					{
						// See the cache's declaration comment (top of file) for why this reload is happening at all (a raw object-LOD-level change near a boundary, not an actual content change) and why
						// skipping straight to the already-built result - no re-decode, no LoD rebuild, no "Building..." indicator - is correct here, not just faster: same URL guarantees same file bytes.
						msg->splat_data = cached_splat_data;
					}
					else
					{
						msg->splat_data = GaussianSplatLoader::loadFromBuffer(model_buffer.data(), model_buffer.size()); // Can throw - if it does, none of the LoD-build code below ever runs, so there's no
							// Msg_GaussianSplatLodBuildStatusMessage(starting=true) to reconcile; the outer catch blocks handle this exactly as they did before this feature existed.

						// Build the on-the-fly LoD tree (Claude_LOD_plan.md, stage 3) - also CPU-only, safe to do right here on this worker thread, still before result_msg_queue->enqueue(msg) below hands the
						// splat cloud back to the main thread. Bracketed with start/finish status messages so GUIClient can show/hide a "Building..." indicator for however long this takes (see
						// GUIClient::num_gaussian_splat_lod_builds_in_progress and ThreadMessages.h's GaussianSplatLodBuildStatusMessage) - large/dense scenes are the whole reason this feature exists, so
						// unlike the fast (already-been-here-for-months) decode step above, this step can genuinely take long enough to be worth telling the user about.
						if(msg->splat_data.nonNull() && msg->splat_data->numSplats() > 0)
						{
							result_msg_queue->enqueue(new GaussianSplatLodBuildStatusMessage(/*starting=*/true));
							try
							{
								msg->splat_data->lod_tree = buildGaussianSplatLodTree(msg->splat_data->positions.data(), msg->splat_data->scales.data(), msg->splat_data->rotations.data(),
									msg->splat_data->colours.data(), msg->splat_data->numSplats());
							}
							catch(std::exception&)
							{
								// The LoD tree is a nice-to-have, not something the splat cloud needs in order to render at all (stage 4, GPU upload of the tree, isn't wired up yet as of this comment, and
								// even once it is, GaussianSplatRenderer must already treat an empty lod_tree as "no LoD, render every splat" for the not-yet-built case above - reuse that same fallback here
								// rather than failing the whole upload over a LoD-only problem). msg->splat_data->lod_tree is left empty (default-constructed) by this catch.
							}
							result_msg_queue->enqueue(new GaussianSplatLodBuildStatusMessage(/*starting=*/false)); // Always sent if starting=true was - the try/catch above guarantees that, whatever
								// buildGaussianSplatLodTree() does, we still reach this line and don't leave GUIClient's counter incremented forever.

							Lock lock(g_gaussian_splat_cache_mutex);
							g_gaussian_splat_cache[cache_key] = msg->splat_data;
						}
					}

					msg->lod_model_url = lod_model_url;
					msg->model_lod_level = model_lod_level;
					msg->built_dynamic_physics_ob = this->build_dynamic_physics_ob;
					result_msg_queue->enqueue(msg);
					return;
				}

				if(hasExtension(lod_model_path, "subvox"))
				{
					// TODO: lod level stuff

					SubVoxFileContents contents;
					FormatDecoderSubVox::readSubVoxFileFromData(model_buffer.data(), model_buffer.size(), contents);

					// Copy SubVoxVoxelGroup group to VoxelGroup.  Just use memcpy.
					VoxelGroup voxel_group;
					voxel_group.voxels.resize(contents.group.voxels.size());
					static_assert(sizeof(SubVoxVoxel) == sizeof(Voxel));
					std::memcpy(voxel_group.voxels.data(), contents.group.voxels.data(), contents.group.voxels.dataSizeBytes());


					gl_meshdata = ModelLoading::makeModelForVoxelGroup(voxel_group, subsample_factor, ob_to_world_matrix, /*vert_buf_allocator=*/NULL, /*do_opengl_stuff=*/false, 
						need_lightmap_uvs, mat_transparent, build_dynamic_physics_ob, worker_allocator.ptr(), /*physics shape out=*/physics_shape);
				}
				else
				{
					js::Vector<bool> create_tris_for_mat;

					gl_meshdata = ModelLoading::makeGLMeshDataAndPhysicsShape(lod_model_path,
						model_buffer,
						/*vert_buf_allocator=*/NULL, 
						true, // skip_opengl_calls - we need to do these on the main thread.
						build_physics_ob,
						build_dynamic_physics_ob,
						create_tris_for_mat,
						worker_allocator.ptr(),
						/*physics shape out=*/physics_shape);
				}
			}


			ArrayRef<uint8> vert_data, index_data;
			gl_meshdata->getVertAndIndexArrayRefs(vert_data, index_data);
			
			const size_t index_data_src_offset_B = Maths::roundUpToMultipleOfPowerOf2<size_t>(vert_data.size(), 16); // Offset in VBO
			const size_t total_geom_size_B = index_data_src_offset_B + index_data.size();

			if(upload_thread)
			{
				UploadGeometryMessage* upload_msg = new UploadGeometryMessage();
				upload_msg->meshdata = gl_meshdata;
				upload_msg->index_data_src_offset_B = index_data_src_offset_B;
				upload_msg->total_geom_size_B = total_geom_size_B;
				upload_msg->vert_data_size_B = vert_data.size();
				upload_msg->index_data_size_B = index_data.size();

				LoadModelTaskUploadingUserInfo* user_info = new LoadModelTaskUploadingUserInfo();
				user_info->physics_shape = physics_shape;
				user_info->lod_model_url = lod_model_url;
				user_info->model_lod_level = model_lod_level;
				user_info->built_dynamic_physics_ob = this->build_dynamic_physics_ob;
				user_info->voxel_subsample_factor = subsample_factor;
				user_info->voxel_hash = voxel_hash;

				upload_msg->user_info = user_info;

				// Null out references to gl_meshdata and jolt shape here, before we pass to another thread.
				// This is important for gl_meshdata, since the main thread may set gl_meshdata->individual_vao, which could then be destroyed on this thread, which is invalid.
				gl_meshdata = NULL;
				physics_shape.jolt_shape = NULL;

				upload_thread->getMessageQueue().enqueue(upload_msg);
			}
			else
			{
				// Send a ModelLoadedThreadMessage back to main window.
				Reference<ModelLoadedThreadMessage> msg = new ModelLoadedThreadMessage();
				msg->gl_meshdata = gl_meshdata;
				msg->physics_shape = physics_shape;
				msg->lod_model_url = lod_model_url;
				msg->model_lod_level = model_lod_level;
				msg->voxel_hash = voxel_hash;
				msg->subsample_factor = subsample_factor;
				msg->built_dynamic_physics_ob = this->build_dynamic_physics_ob;
				msg->index_data_src_offset_B = index_data_src_offset_B;
				msg->total_geom_size_B = total_geom_size_B;
				msg->vert_data_size_B = vert_data.size();
				msg->index_data_size_B = index_data.size();

				// Null out references to gl_meshdata and jolt shape here, before we pass to another thread.
				// This is important for gl_meshdata, since the main thread may set gl_meshdata->individual_vao, which could then be destroyed on this thread, which is invalid.
				gl_meshdata = NULL;
				physics_shape.jolt_shape = NULL;

				result_msg_queue->enqueue(msg);
			}

			return;
		}
		catch(glare::LimitedAllocatorAllocFailed& e)
		{
			const int wait_time_ms = 1 << attempt;
			conPrint("LoadModelTask: Got LimitedAllocatorAllocFailed, trying again in " + toString(wait_time_ms) + " ms: " + e.what());
			// Loop and try again, wait with exponential back-off.
			PlatformUtils::Sleep(wait_time_ms);
		}
		catch(glare::Exception& e)
		{
			//conPrint("LoadModelTask: excep: " + e.what());
			const std::string model_URL = compressed_voxels ? "[voxel_ob]" : toStdString(this->lod_model_url);
			result_msg_queue->enqueue(new LogMessage("Error while loading model '" + model_URL + "': " + e.what()));
			return;
		}
		catch(std::bad_alloc&)
		{
			//conPrint("LoadModelTask: excep: " + e.what());
			const std::string model_URL = compressed_voxels ? "[voxel_ob]" : toStdString(this->lod_model_url);
			result_msg_queue->enqueue(new LogMessage("Error while loading model '" + model_URL + "': failed to allocate mem (bad_alloc)"));
			return;
		}
	}

	// We tried N times but each time we got an LimitedAllocatorAllocFailed exception.
	const std::string model_URL = compressed_voxels ? "[voxel_ob]" : toStdString(this->lod_model_url);
	result_msg_queue->enqueue(new LogMessage("Failed to load model '" + model_URL + "': failed after multiple LimitedAllocatorAllocFailed"));
}
