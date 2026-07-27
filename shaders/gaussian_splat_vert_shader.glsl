
// See class comment in gui_client/gaussian_splats/GaussianSplatRenderer.h for the overall design this shader is part of.

precision highp float; // Override the engine's default "precision mediump float;" for Emscripten (see OpenGLEngine.cpp) - splat covariance math is precision-sensitive (squared pixel offsets, small determinants).

in vec3 position_in; // Local quad corner, in [-1, 1] x [-1, 1], z = 0. Scaled/oriented per-splat below based on the projected 2D covariance (EWA splatting).
in uint splat_index_in; // Per-instance index into albedo_texture (identity order until a CPU depth-sort reorders this VBO - see GaussianSplatRenderer::createObject(), architecture contract task #7).

uniform mat4 model_matrix;
uniform mat4 view_matrix;
uniform mat4 proj_matrix;

uniform sampler2D albedo_texture; // Packed splat data: 4 RGBA32F texels per splat - see GaussianSplatRenderer.cpp::packSplatDataToTexels() for the exact layout.
uniform vec2 viewport_dims_px;
uniform vec2 focal_len_px;
uniform int splat_tex_width;

out vec2 frag_screen_offset_px; // Pixel-space offset of this vertex from the splat's projected centre.
out vec3 frag_conic; // Inverse 2D covariance (A, B, C) of [[A, B], [B, C]], for the per-pixel Gaussian evaluation in the fragment shader.
out vec4 frag_colour; // (r, g, b, opacity)


ivec2 splatTexelCoord(int texel_index)
{
	return ivec2(texel_index % splat_tex_width, texel_index / splat_tex_width);
}


void main()
{
	int base_texel = int(splat_index_in) * 4;

	vec4 t0 = texelFetch(albedo_texture, splatTexelCoord(base_texel + 0), 0);
	vec4 t1 = texelFetch(albedo_texture, splatTexelCoord(base_texel + 1), 0);
	vec4 t2 = texelFetch(albedo_texture, splatTexelCoord(base_texel + 2), 0);
	vec4 t3 = texelFetch(albedo_texture, splatTexelCoord(base_texel + 3), 0);

	vec3 pos_os = t0.xyz;
	vec3 scale  = vec3(t0.w, t1.x, t1.y);
	vec4 rot    = vec4(t1.z, t1.w, t2.x, t2.y); // (x, y, z, w)
	frag_colour = vec4(t2.z, t2.w, t3.x, t3.y); // (r, g, b, opacity)

	vec4 pos_vs = view_matrix * (model_matrix * vec4(pos_os, 1.0));

	// Camera space here is (x = right, y = forwards, z = up) - see OpenGLEngine.h's comment on world_to_camera_space_matrix - not the usual -Z-forward OpenGL convention. "Depth" for the projection below is therefore pos_vs.y, not pos_vs.z.
	const float near_epsilon = 0.01;
	if(pos_vs.y < near_epsilon)
	{
		gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // Push behind-camera splats outside the clip volume; simpler than a real clip-space clip for a first version.
		frag_conic = vec3(0.0);
		frag_screen_offset_px = vec2(0.0);
		return;
	}

	// Build the 3x3 rotation matrix from the quaternion (x, y, z, w). Columns are built explicitly (rather than via a 9-scalar mat3(...) literal) to avoid GLSL's column-major constructor order silently transposing this.
	float qx = rot.x, qy = rot.y, qz = rot.z, qw = rot.w;
	vec3 r_col0 = vec3(1.0 - 2.0*(qy*qy + qz*qz),       2.0*(qx*qy + qz*qw),       2.0*(qx*qz - qy*qw));
	vec3 r_col1 = vec3(      2.0*(qx*qy - qz*qw), 1.0 - 2.0*(qx*qx + qz*qz),       2.0*(qy*qz + qx*qw));
	vec3 r_col2 = vec3(      2.0*(qx*qz + qy*qw),       2.0*(qy*qz - qx*qw), 1.0 - 2.0*(qx*qx + qy*qy));
	mat3 R = mat3(r_col0, r_col1, r_col2);

	// 3D covariance in object space: Sigma = R * diag(scale^2) * R^T.
	mat3 RS = mat3(r_col0 * (scale.x*scale.x), r_col1 * (scale.y*scale.y), r_col2 * (scale.z*scale.z));
	mat3 cov_os = RS * transpose(R);

	// Transform to camera space. Assumes model_matrix has no non-uniform scale/shear (rotation + uniform scale only) - non-uniform object scaling would require the inverse-transpose here instead.
	mat3 W = mat3(view_matrix * model_matrix);
	mat3 cov_vs = W * cov_os * transpose(W);

	// EWA splatting: project the 3D covariance to a 2D screen-space covariance via the projection's Jacobian, evaluated at this splat's view-space position.
	// NOTE: the two rows below are the (pixel_x, pixel_y) Jacobian rows adapted to this engine's y-forward/z-up camera space (see comment above), not the textbook z-forward derivation.
	float vx = pos_vs.x, vy = pos_vs.y, vz = pos_vs.z;
	vec3 j_row0 = vec3(focal_len_px.x / vy, -focal_len_px.x * vx / (vy*vy), 0.0);
	vec3 j_row1 = vec3(0.0, -focal_len_px.y * vz / (vy*vy), focal_len_px.y / vy);
	// NOTE: whether j_row1's sign here matches this engine's actual screen/NDC y direction hasn't been visually verified yet (see architecture contract task #9) - a sign flip here would only show up as elongated (anisotropic) splats being tilted the mirror-opposite way; circular splats are unaffected.

	vec3 cov_vs_j0 = cov_vs * j_row0;
	vec3 cov_vs_j1 = cov_vs * j_row1;

	float cov2d_a = dot(j_row0, cov_vs_j0) + 0.3; // +0.3: low-pass filter on the diagonal, avoids degenerate sub-pixel splats popping/aliasing (standard 3DGS technique).
	float cov2d_b = dot(j_row0, cov_vs_j1);
	float cov2d_c = dot(j_row1, cov_vs_j1) + 0.3;

	float det = cov2d_a * cov2d_c - cov2d_b * cov2d_b;
	if(det <= 0.0)
	{
		gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // Degenerate covariance (shouldn't normally happen after the +0.3 dilation above) - cull.
		frag_conic = vec3(0.0);
		frag_screen_offset_px = vec2(0.0);
		return;
	}

	// Eigen-decomposition of the symmetric 2x2 [[a, b], [b, c]], to get the ellipse's screen-space axes and radii (99.7% / 3-sigma cutoff).
	float mid = 0.5 * (cov2d_a + cov2d_c);
	float half_span = sqrt(max(mid*mid - det, 0.0));
	float lambda1 = mid + half_span;
	float lambda2 = max(mid - half_span, 0.0);

	vec2 axis1 = (cov2d_b != 0.0) ? normalize(vec2(cov2d_b, lambda1 - cov2d_a)) : ((cov2d_a >= cov2d_c) ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
	vec2 axis2 = vec2(-axis1.y, axis1.x);

	float radius1 = 3.0 * sqrt(lambda1);
	float radius2 = 3.0 * sqrt(lambda2);

	vec2 screen_offset_px = position_in.x * radius1 * axis1 + position_in.y * radius2 * axis2;

	vec4 clip_pos = proj_matrix * pos_vs;
	clip_pos.xy += (screen_offset_px / viewport_dims_px) * 2.0 * clip_pos.w; // Offset in clip space, pre-multiplied by w so it survives the perspective divide unchanged (standard screen-space billboard technique).
	gl_Position = clip_pos;

	float inv_det = 1.0 / det;
	frag_conic = vec3(cov2d_c * inv_det, -cov2d_b * inv_det, cov2d_a * inv_det); // (A, B, C) of the conic Ax^2 + 2Bxy + Cy^2, evaluated per-pixel in the fragment shader.
	frag_screen_offset_px = screen_offset_px;
}
