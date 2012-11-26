#if defined(USING_GLES2)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#elif defined(IOS)
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
typedef char GLchar;
#else
#include <GL/glew.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

extern const GLint aLookup[];
extern const GLint bLookup[];
extern const GLint eqLookup[];
extern const GLint cullingMode[];
extern const GLuint ztests[];
