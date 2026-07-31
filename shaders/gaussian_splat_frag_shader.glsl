
// gaussian_splat_frag_shader.glsl
// coded by AI agent under @russiaman supervision -
// Generated at Mon Jul 27 06:16:15 2026
//
// See class comment in gui_client/gaussian_splats/GaussianSplatRenderer.h for the overall design this shader is part of.

precision highp float; // Override the engine's default "precision mediump float;" for Emscripten (see OpenGLEngine.cpp) - the Gaussian exponent below is sensitive to precision for large splats/radii.

in vec2 frag_screen_offset_px;
in vec3 frag_conic;
in vec4 frag_colour;

// Two output paths, matching OpenGLEngine::drawTransparentMaterialBatches(): web (Emscripten) always renders transparent batches straight to a single
// colour buffer with glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); native builds default to order-independent transparency (use_order_indep_transparency,
// set from settings.render_to_offscreen_renderbuffers), which instead binds TWO colour buffers (total_transmittance @ location 0, transparent_accum @
// location 1) with per-buffer blend funcs (glBlendFunci: buf 0 = dest*=source i.e. multiplicative transmittance, buf 1 = dest+=source i.e. additive
// accum) - see glare-core/opengl/OpenGLEngine.cpp ~9693-9718 and glare-core/opengl/shaders/transparent_frag_shader.glsl for the reference pattern this
// mirrors. Originally this shader only had the single-output path, which on native left buffer 1 (accum) permanently at its cleared (0,0,0,0) value and
// fed straight premultiplied colour into buffer 0 as if it were a transmittance value - the OIT composite pass then produced solid black. That was the
// root cause of the "splats render as solid black" bug from session022.
#if ORDER_INDEPENDENT_TRANSPARENCY
layout(location = 0) out vec4 transmittance_out;
layout(location = 1) out vec4 accum_out;
#else
out vec4 colour_out;
#endif


void main()
{
	// Evaluate the 2D Gaussian at this pixel: exponent = -0.5 * offset^T * conic * offset, where conic is the inverse 2D covariance (see gaussian_splat_vert_shader.glsl).
	float power = -0.5 * (frag_conic.x * frag_screen_offset_px.x * frag_screen_offset_px.x
	                     + 2.0 * frag_conic.y * frag_screen_offset_px.x * frag_screen_offset_px.y
	                     + frag_conic.z * frag_screen_offset_px.y * frag_screen_offset_px.y);
	if(power > 0.0)
		discard;

	float alpha = frag_colour.a * exp(power);
	if(alpha < (1.0 / 255.0))
		discard;

#if ORDER_INDEPENDENT_TRANSPARENCY
	// Buffer 0: how much of what's behind this splat still shows through (multiplicative across overlapping transparent fragments via glBlendFunci(0, GL_ZERO, GL_SRC_COLOR)).
	float T = 1.0 - alpha;
	transmittance_out = vec4(T, T, T, T);
	// Buffer 1: this splat's own premultiplied colour contribution (additive via glBlendFunci(1, GL_ONE, GL_ONE)).
	accum_out = vec4(frag_colour.rgb * alpha, alpha);
#else
	// Single premultiplied-alpha output for the web build (single draw buffer, glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)).
	colour_out = vec4(frag_colour.rgb * alpha, alpha);
#endif
}
