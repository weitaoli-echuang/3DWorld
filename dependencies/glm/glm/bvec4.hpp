/// @ref core
/// @file glm/bvec4.hpp

#pragma once
#include "detail/type_vec4.hpp"

namespace glm
{
	/// @addtogroup core
	/// @{

#	if(defined(GLM_PRECISION_LOWP_BOOL))
		typedef vec<4, bool, lowp>		bvec4;
#	elif(defined(GLM_PRECISION_MEDIUMP_BOOL))
		typedef vec<4, bool, mediump>	bvec4;
#	else //defined(GLM_PRECISION_HIGHP_BOOL)
		/// 4 components vector of boolean.
		///
		/// @see <a href="http://www.opengl.org/registry/doc/GLSLangSpec.4.20.8.pdf">GLSL 4.20.8 specification, section 4.1.5 Vectors</a>
		typedef vec<4, bool, highp>		bvec4;
#	endif//GLM_PRECISION

	/// @}
}//namespace glm
