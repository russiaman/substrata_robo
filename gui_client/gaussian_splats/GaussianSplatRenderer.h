/*=====================================================================
GaussianSplatRenderer.h
------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "GaussianSplatData.h"
#include <opengl/OpenGLEngine.h>
#include <opengl/OpenGLProgram.h>
#include <utils/Platform.h>
#include <utils/Reference.h>
#include <string>
#include <vector>


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
directly - this is what lets a later CPU depth-sort (architecture
contract task #7) reorder splats back-to-front just by rewriting that
VBO with VBO::updateData(), without touching the mesh or shader.

Not handled yet (see architecture contract §6.3 - documented, not silent):
 - CPU depth-sorting (task #7). think() only refreshes per-frame
   uniforms for now; instance order is always identity (splat i is
   always drawn as instance i).
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
	GLObjectRef createObject(const GaussianSplatDataRef& splat_data, OpenGLEngine& opengl_engine);

	// Per-frame update: refreshes the viewport-size / focal-length user uniforms each managed object's shader needs for the EWA covariance projection.
	// Call once per frame, after the frame's camera transform has been set on opengl_engine.
	void think(OpenGLEngine& opengl_engine);

	void shutdown();

private:
	GLARE_DISABLE_COPY(GaussianSplatRenderer);

	Reference<OpenGLProgram> shader_prog;

	std::vector<GLObjectRef> managed_objects; // Objects created by createObject(), refreshed each frame by think().
};
