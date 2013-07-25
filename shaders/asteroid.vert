varying vec3 vpos, normal, world_normal;
varying vec4 epos;

#ifdef USE_CUSTOM_XFORM
attribute mat4 inst_xform_matrix;
#endif

void main()
{
#ifdef USE_CUSTOM_XFORM
	epos          = inst_xform_matrix * gl_Vertex;
	gl_Position   = gl_ProjectionMatrix * epos;
	normal        = normalize(transpose(inverse(mat3(inst_xform_matrix))) * gl_Normal);
#else
	epos          = gl_ModelViewMatrix * gl_Vertex;
	gl_Position   = ftransform();
	normal        = normalize(gl_NormalMatrix * gl_Normal); // for lighting
#endif
	gl_FrontColor = gl_Color;
	world_normal  = gl_Normal; // for triplanar texturing
	vpos          = gl_Vertex.xyz;
} 
