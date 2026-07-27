
// See class comment in gui_client/gaussian_splats/GaussianSplatRenderer.h for the overall design this shader is part of.

precision highp float; // Override the engine's default "precision mediump float;" for Emscripten (see OpenGLEngine.cpp) - the Gaussian exponent below is sensitive to precision for large splats/radii.

in vec2 frag_screen_offset_px;
in vec3 frag_conic;
in vec4 frag_colour;

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

	// Single premultiplied-alpha output, matching the blend mode OpenGLEngine::drawTransparentMaterialBatches() always uses on the web build (glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA), single draw buffer).
	// NOTE: this deliberately differs from parcel_frag_shader.glsl's dual transmittance/accum output pattern for native order-independent-transparency support - see class comment in GaussianSplatRenderer.h for why (that pattern has a known,
	// commented TODO gap for the non-OIT case in the upstream shader, which is the only case this module currently targets).
	colour_out = vec4(frag_colour.rgb * alpha, alpha);
}
