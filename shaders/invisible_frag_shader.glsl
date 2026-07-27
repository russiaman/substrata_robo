
// invisible_frag_shader.glsl
// coded by AI agent under @russiaman supervision -
// Generated at Mon Jul 27 2026
//
// Discards every fragment - makes the object using this shader fully invisible (no diffuse, no specular/Fresnel reflection at all,
// unlike opacity=0 on the standard phong material, which still lets Fresnel-reflection terms through regardless of alpha - see
// phong_frag_shader.glsl's `colour_out = vec4(col, alpha)`, where `col` is not premultiplied by alpha). Physics/collision for the
// object is entirely separate (WorldObject::isCollidable(), PhysicsObject) and unaffected by this - see class comment in
// server/WorldCreation.cpp::ensureTestGroundPlatformExists().

void main()
{
	discard;
}
