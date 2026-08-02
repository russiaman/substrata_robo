
// gaussian_splat_frag_shader.glsl
// coded by AI agent under @russiaman supervision -
// Generated at Mon Jul 27 06:16:15 2026
//
// See class comment in gui_client/gaussian_splats/GaussianSplatRenderer.h for the overall design this shader is part of.

precision highp float; // Override the engine's default "precision mediump float;" for Emscripten (see OpenGLEngine.cpp) - the Gaussian exponent below is sensitive to precision for large splats/radii.

in vec2 frag_screen_offset_px;
in vec3 frag_conic;
in vec4 frag_colour;

// Single colour output for all platforms.  Gaussian splats are now drawn through the alpha-blended pass
// (mat.alpha_blend = true), which uses glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA).  The fragment
// colour must therefore be *non-premultiplied* – the hardware will multiply the RGB by alpha during blending.
out vec4 colour_out;


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

	// Non-premultiplied output for the alpha-blended pass (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA).
	// The hardware multiplies RGB by alpha automatically, so we output raw colour + alpha.
	colour_out = vec4(frag_colour.rgb, alpha);
}
