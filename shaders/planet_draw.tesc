// define the number of CPs in the output patch
layout (vertices = 3) out;

uniform float obj_radius;
uniform vec3 camera_pos; // in world space
uniform mat4 fg_ViewMatrix;

// attributes of the input CPs
in vec3 vertex[];

// attributes of the output CPs
out vec3 vertex_ES[];

float get_tess_level(float d1, float d2) {
	float dav = 0.5*(d1 + d2);
	float v   = 60.0*obj_radius/dav;
	if (v < 1.5 ) {return 1; }
	if (v < 3.0 ) {return 2; }
	if (v < 6.0 ) {return 4; }
	if (v < 10.0) {return 8; }
	if (v < 14.0) {return 12;}
	if (v < 18.0) {return 16;}
	return 20;
}
float dist_to_camera(in vec3 pos, in mat4 vm_inv) {
	vec4 epos = fg_ModelViewMatrix * vec4(pos, 1.0);
	vec3 world_space_pos = (vm_inv * epos).xyz;
	return distance(camera_pos, world_space_pos);
}

void main() {
	// Set the control points of the output patch
	vertex_ES[gl_InvocationID] = vertex[gl_InvocationID];

	// Calculate the distance from the camera to the three control points
	mat4 vm_inv = inverse(fg_ViewMatrix);
	float dist0 = dist_to_camera(vertex[0], vm_inv);
	float dist1 = dist_to_camera(vertex[1], vm_inv);
	float dist2 = dist_to_camera(vertex[2], vm_inv);

	// Calculate the tessellation levels
	gl_TessLevelOuter[0] = get_tess_level(dist1, dist2);
	gl_TessLevelOuter[1] = get_tess_level(dist2, dist0);
	gl_TessLevelOuter[2] = get_tess_level(dist0, dist1);
	gl_TessLevelInner[0] = gl_TessLevelOuter[2];
}                                                                                        