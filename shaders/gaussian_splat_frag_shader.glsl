
// gaussian_splat_frag_shader.glsl
// coded by AI agent under @russiaman supervision -
// Generated at Mon Jul 27 06:16:15 2026
//
// TEMPORARY DIAGNOSTIC VERSION (architecture contract task #9 debugging) - matches the simplified fixed-radius-billboard
// vertex shader. Real version backed up at gaussian_splat_frag_shader.glsl.ewa_backup.

precision highp float;

in vec2 frag_local;
in vec4 frag_colour;

out vec4 colour_out;


void main()
{
	float r2 = dot(frag_local, frag_local);
	if(r2 > 1.0)
		discard; // Outside the circle - simple sphere/disc mask, no Gaussian falloff.

	float alpha = frag_colour.a;
	colour_out = vec4(frag_colour.rgb * alpha, alpha);
}
