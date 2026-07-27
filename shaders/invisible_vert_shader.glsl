
// invisible_vert_shader.glsl
// coded by AI agent under @russiaman supervision -
// Generated at Mon Jul 27 2026
//
// Trivial pass-through vertex shader for invisible_frag_shader.glsl (which discards every fragment). Used for the dev/test-only
// "test_ground_platform" object (see server/WorldCreation.cpp::ensureTestGroundPlatformExists()) - a large collidable slab we want
// to be walkable but never rendered. Assigned via material.auto_assign_shader = false (see GUIClient::makeShaders()), the same
// officially-supported custom-shader mechanism already used for parcel/portal/Gaussian splats - no engine files touched.

in vec3 position_in;

uniform mat4 model_matrix;
uniform mat4 view_matrix;
uniform mat4 proj_matrix;


void main()
{
	gl_Position = proj_matrix * (view_matrix * (model_matrix * vec4(position_in, 1.0)));
}
