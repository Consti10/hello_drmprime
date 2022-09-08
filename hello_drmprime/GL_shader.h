//
// Created by consti10 on 08.09.22.
//

#ifndef HELLO_DRMPRIME__GL_SHADER_H_
#define HELLO_DRMPRIME__GL_SHADER_H_

#define GLFW_INCLUDE_ES2
extern "C" {
#include <GLFW/glfw3.h>
#include "glhelp.h"
}


class GL_shader {
 private:
  // Single EGL external texture (We do not have to write our own YUV conversion(s), egl does it for us.
  // Any platform where we can get the (HW) - decoded frame to EGL (e.g. rpi) this is the easiest and best way.
  struct EGLShader{
	GLuint program=0;
	GLint pos=-1;
	GLint uvs=-1;
  };
  // Single RGB(A) texture
  struct RGBAShader{
	GLuint program=0;
	GLint pos=-1;
	GLint uvs=-1;
	GLint sampler=-1;
  };
  // NV12
  struct NV12Shader{
	GLuint program=0;
	GLint pos=-1;
	GLint uvs=-1;
	GLint s_texture_y=-1;
	GLint s_texture_uv=-1;
  };
  // YUV 420P
  struct YUV420PShader{
	GLuint program=0;
	GLint pos=-1;
	GLint uvs=-1;
	GLint s_texture_y=-1;
	GLint s_texture_u=-1;
	GLint s_texture_v=-1;
  };
  EGLShader egl_shader;
  RGBAShader rgba_shader;
  YUV420PShader yuv_420_p_shader;
  NV12Shader nv_12_shader;
  // All shaders use the same VBO for vertex / uv coordinates
  GLuint vbo=0;
 public:
  void initialize();
  void draw_egl(GLuint texture);
  void draw_rgb(GLuint texture);
  void draw_YUV420P(GLuint textureY,GLuint textureU,GLuint textureV);
  void draw_NV12(GLuint textureY,GLuint textureUV);
};

#endif //HELLO_DRMPRIME__GL_SHADER_H_
