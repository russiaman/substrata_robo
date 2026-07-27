
// gaussian_splat_vert_shader.glsl
// coded by AI agent under @russiaman supervision -
// Generated at Mon Jul 27 06:16:15 2026
//
// TEMPORARY DIAGNOSTIC VERSION (architecture contract task #9 debugging): the real EWA-splatting version is backed up at
// gaussian_splat_vert_shader.glsl.ewa_backup. This version replaces the 3D-covariance projection with a simple fixed-size
// billboard (a screen-facing circle of constant world-space radius) at each splat's position, to isolate whether the
// texture unpacking / instancing / VAO wiring / blending is correct, independent of the EWA covariance math.

precision highp float;

in vec3 position_in; // Local quad corner, in [-1, 1] x [-1, 1], z = 0.
in uint splat_index_in; // Per-instance index into albedo_texture.

uniform mat4 model_matrix;
uniform mat4 view_matrix;
uniform mat4 proj_matrix;

uniform sampler2D albedo_texture;
uniform vec2 viewport_dims_px;
uniform vec2 focal_len_px;
uniform int splat_tex_width;

out vec2 frag_local; // Quad-local corner in [-1, 1], for the circle mask in the fragment shader.
out vec4 frag_colour; // (r, g, b, opacity)


ivec2 splatTexelCoord(int texel_index)
{
	return ivec2(texel_index % splat_tex_width, texel_index / splat_tex_width);
}


void main()
{
	int base_texel = int(splat_index_in) * 4;

	vec4 t0 = texelFetch(albedo_texture, splatTexelCoord(base_texel + 0), 0);
	vec4 t2 = texelFetch(albedo_texture, splatTexelCoord(base_texel + 2), 0);
	vec4 t3 = texelFetch(albedo_texture, splatTexelCoord(base_texel + 3), 0);

	vec3 pos_os = t0.xyz;
	frag_colour = vec4(t2.z, t2.w, t3.x, t3.y); // (r, g, b, opacity)

	vec4 pos_vs = view_matrix * (model_matrix * vec4(pos_os, 1.0));

	// Camera space here is (x = right, y = forwards, z = up) - depth is pos_vs.y, not pos_vs.z.
	if(pos_vs.y < 0.1)
	{
		gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // Cull behind-camera splats.
		frag_local = vec2(0.0);
		return;
	}

	const float fixed_world_radius = 0.01; // A small, constant world-space radius - just enough to see the point cloud's shape, no per-splat scale/rotation/covariance involved at all.
	float radius_px = clamp(fixed_world_radius * focal_len_px.x / pos_vs.y, 1.0, 40.0);

	vec2 screen_offset_px = position_in.xy * radius_px;

	vec4 clip_pos = proj_matrix * pos_vs;
	clip_pos.xy += (screen_offset_px / viewport_dims_px) * 2.0 * clip_pos.w;
	gl_Position = clip_pos;

	frag_local = position_in.xy;
}
