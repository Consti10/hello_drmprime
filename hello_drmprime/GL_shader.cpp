//
// Created by consti10 on 08.09.22.
//

#include <cstdio>
#include <string>
#include <sstream>
#include <iostream>
#include <cassert>
#include "GL_shader.h"

static const char *GlErrorString(GLenum error ){
  switch ( error ){
	case GL_NO_ERROR:						return "GL_NO_ERROR";
	case GL_INVALID_ENUM:					return "GL_INVALID_ENUM";
	case GL_INVALID_VALUE:					return "GL_INVALID_VALUE";
	case GL_INVALID_OPERATION:				return "GL_INVALID_OPERATION";
	case GL_INVALID_FRAMEBUFFER_OPERATION:	return "GL_INVALID_FRAMEBUFFER_OPERATION";
	case GL_OUT_OF_MEMORY:					return "GL_OUT_OF_MEMORY";
	  //
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
	default: return "unknown";
  }
}
static void checkGlError(const std::string& caller) {
  GLenum error;
  std::stringstream ss;
  ss<<"GLError:"<<caller.c_str();
  ss<<__FILE__<<__LINE__;
  bool anyError=false;
  while ((error = glGetError()) != GL_NO_ERROR) {
	ss<<" |"<<GlErrorString(error);
	anyError=true;
  }
  if(anyError){
	std::cout<<ss.str()<<"\n";
	// CRASH_APPLICATION_ON_GL_ERROR
	if(false){
	  std::exit(-1);
	}
  }
}

// We always use the same vertex shader code - full screen texture.
// (Adjust ratio by setting the OpenGL viewport)
static const GLchar* vertex_shader_source =
	"#version 300 es\n"
	"in vec3 position;\n"
	"in vec2 tex_coords;\n"
	"out vec2 v_texCoord;\n"
	"void main() {  \n"
	"	gl_Position = vec4(position, 1.0);\n"
	"	v_texCoord = tex_coords;\n"
	"}\n";
static const GLchar* fragment_shader_source_GL_OES_EGL_IMAGE_EXTERNAL =
	"#version 300 es\n"
	"#extension GL_OES_EGL_image_external : require\n"
	"precision mediump float;\n"
	"uniform samplerExternalOES texture;\n"
	"in vec2 v_texCoord;\n"
	"void main() {	\n"
	"	gl_FragColor = texture2D( texture, v_texCoord );\n"
	"}\n";
static const GLchar* fragment_shader_source_RGB =
	"#version 300 es\n"
	"precision mediump float;\n"
	"uniform sampler2D s_texture;\n"
	"in vec2 v_texCoord;\n"
	"void main() {	\n"
	"	gl_FragColor = texture2D( s_texture, v_texCoord );\n"
	//"	out_color = vec4(v_texCoord.x,1.0,0.0,1.0);\n"
	"}\n";
static const GLchar* fragment_shader_source_YUV420P =
	"#version 300 es\n"
	"precision highp float;\n"
	"uniform sampler2D s_texture_y;\n"
	"uniform sampler2D s_texture_u;\n"
	"uniform sampler2D s_texture_v;\n"
	"in vec2 v_texCoord;\n"
	"void main() {	\n"
	"	float Y = texture2D(s_texture_y, v_texCoord).x;\n"
	"	float U = texture2D(s_texture_u, v_texCoord).x;\n"
	"	float V = texture2D(s_texture_v, v_texCoord).x;\n"
	"	vec3 YUV = vec3(Y, U, V);"
	"	mat3 colorMatrix = mat3(\n"
	"		1.1644f, 1.1644f, 1.1644f,\n"
	"        0.0f, -0.3917f, 2.0172f,\n"
	"        1.5960f, -0.8129f, 0.0f"
	"		);\n"
	/*"	mat3 colorMatrix = mat3(\n"
	"		1,   0,       1.402,\n"
	"		1,  -0.344,  -0.714,\n"
	"		1,   1.772,   0);\n"*/
	"	gl_FragColor = vec4(YUV*colorMatrix, 1.0);\n"
	"}\n";
static const GLchar* fragment_shader_source_NV12 =
	"#version 300 es\n"
	"precision mediump float;\n"
	"uniform sampler2D s_texture_y;\n"
	"uniform sampler2D s_texture_uv;\n"
	"in vec2 v_texCoord;\n"
	"void main() {	\n"
	"	vec3 YCbCr = vec3(\n"
	"		texture2D(s_texture_y, v_texCoord).x,\n"
	"		texture2D(s_texture_uv,v_texCoord).xy\n"
	"	);"
	"	mat3 colorMatrix = mat3(\n"
	"		1.1644f, 1.1644f, 1.1644f,\n"
	"        0.0f, -0.3917f, 2.0172f,\n"
	"        1.5960f, -0.8129f, 0.0f"
	"		);\n"
	"	gl_FragColor = vec4(clamp(YCbCr*colorMatrix,0.0,1.0), 1.0);\n"
	"}\n";

/// negative x,y is bottom left and first vertex
//Consti10: Video was flipped horizontally (at least big buck bunny)
static const GLfloat vertices[][4][3] =
	{
		{ {-1.0, -1.0, 0.0}, { 1.0, -1.0, 0.0}, {-1.0, 1.0, 0.0}, {1.0, 1.0, 0.0} }
	};
static const GLfloat uv_coords[][4][2] =
	{
		//{ {0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}, {1.0, 1.0} }
		{ {1.0, 1.0}, {0.0, 1.0}, {1.0, 0.0}, {0.0, 0.0} }
	};

GLint common_get_shader_program(const char *vertex_shader_source, const char *fragment_shader_source) {
  enum Consts {INFOLOG_LEN = 512};
  GLchar infoLog[INFOLOG_LEN];
  GLint fragment_shader;
  GLint shader_program;
  GLint success;
  GLint vertex_shader;

  /* Vertex shader */
  vertex_shader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL);
  glCompileShader(vertex_shader);
  glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
  if (!success) {
	glGetShaderInfoLog(vertex_shader, INFOLOG_LEN, NULL, infoLog);
	printf("ERROR::SHADER::VERTEX::COMPILATION_FAILED\n%s\n", infoLog);
  }

  /* Fragment shader */
  fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragment_shader, 1, &fragment_shader_source, NULL);
  glCompileShader(fragment_shader);
  glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
  if (!success) {
	glGetShaderInfoLog(fragment_shader, INFOLOG_LEN, NULL, infoLog);
	printf("ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n%s\n", infoLog);
  }

  /* Link shaders */
  shader_program = glCreateProgram();
  glAttachShader(shader_program, vertex_shader);
  glAttachShader(shader_program, fragment_shader);
  glLinkProgram(shader_program);
  glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
  if (!success) {
	glGetProgramInfoLog(shader_program, INFOLOG_LEN, NULL, infoLog);
	printf("ERROR::SHADER::PROGRAM::LINKING_FAILED\n%s\n", infoLog);
  }

  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);
  return shader_program;
}

void GL_shader::initialize() {
  // Shader 1
  rgba_shader.program = common_get_shader_program(vertex_shader_source,fragment_shader_source_RGB);
  rgba_shader.pos = glGetAttribLocation(rgba_shader.program, "position");
  assert(rgba_shader.pos>=0);
  rgba_shader.uvs = glGetAttribLocation(rgba_shader.program, "tex_coords");
  assert(rgba_shader.uvs>=0);
  rgba_shader.sampler = glGetUniformLocation(rgba_shader.program, "s_texture" );
  assert(rgba_shader.sampler>=0);
  checkGlError("Create shader RGBA");
  // Shader 2
  egl_shader.program = common_get_shader_program(vertex_shader_source, fragment_shader_source_GL_OES_EGL_IMAGE_EXTERNAL);
  assert(egl_shader.program>=0);
  egl_shader.pos = glGetAttribLocation(egl_shader.program, "position");
  assert(egl_shader.pos>=0);
  egl_shader.uvs = glGetAttribLocation(egl_shader.program, "tex_coords");
  assert(egl_shader.uvs>=0);
  checkGlError("Create shader EGL");
  // Shader 3
  nv_12_shader.program= common_get_shader_program(vertex_shader_source, fragment_shader_source_NV12);
  nv_12_shader.pos = glGetAttribLocation(nv_12_shader.program, "position");
  assert(nv_12_shader.pos>=0);
  nv_12_shader.uvs = glGetAttribLocation(nv_12_shader.program, "tex_coords");
  assert(nv_12_shader.uvs>=0);
  nv_12_shader.s_texture_y=glGetUniformLocation(nv_12_shader.program, "s_texture_y");
  nv_12_shader.s_texture_uv=glGetUniformLocation(nv_12_shader.program, "s_texture_uv");
  assert(nv_12_shader.s_texture_y>=0);
  assert(nv_12_shader.s_texture_uv>=0);
  checkGlError("Create shader NV12");
  // Shader 4
  yuv_420_p_shader.program= common_get_shader_program(vertex_shader_source, fragment_shader_source_YUV420P);
  yuv_420_p_shader.pos = glGetAttribLocation(yuv_420_p_shader.program, "position");
  assert(yuv_420_p_shader.pos>=0);
  yuv_420_p_shader.uvs = glGetAttribLocation(yuv_420_p_shader.program, "tex_coords");
  assert(yuv_420_p_shader.uvs>=0);
  yuv_420_p_shader.s_texture_y=glGetUniformLocation(yuv_420_p_shader.program, "s_texture_y");
  yuv_420_p_shader.s_texture_u=glGetUniformLocation(yuv_420_p_shader.program, "s_texture_u");
  yuv_420_p_shader.s_texture_v=glGetUniformLocation(yuv_420_p_shader.program, "s_texture_v");
  assert(yuv_420_p_shader.s_texture_y>=0);
  assert(yuv_420_p_shader.s_texture_u>=0);
  assert(yuv_420_p_shader.s_texture_v>=0);
  checkGlError("Create shader YUV420P");
  //
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices)+sizeof(uv_coords), 0, GL_STATIC_DRAW);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
  glBufferSubData(GL_ARRAY_BUFFER, sizeof(vertices), sizeof(uv_coords), uv_coords);
  // Each shader program reads from this VBO object
  glEnableVertexAttribArray(egl_shader.pos);
  glEnableVertexAttribArray(egl_shader.uvs);
  glVertexAttribPointer(egl_shader.pos, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);
  glVertexAttribPointer(egl_shader.uvs, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid*)sizeof(vertices)); /// last is offset to loc in buf memory
  //
  glEnableVertexAttribArray(rgba_shader.pos);
  glEnableVertexAttribArray(rgba_shader.uvs);
  glVertexAttribPointer(rgba_shader.pos, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);
  glVertexAttribPointer(rgba_shader.uvs, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid*)sizeof(vertices)); /// last is offset to loc in buf memory
  //
  glEnableVertexAttribArray(nv_12_shader.pos);
  glEnableVertexAttribArray(nv_12_shader.uvs);
  glVertexAttribPointer(nv_12_shader.pos, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);
  glVertexAttribPointer(nv_12_shader.uvs, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid*)sizeof(vertices)); /// last is offset to loc in buf memory
  //
  glEnableVertexAttribArray(yuv_420_p_shader.pos);
  glEnableVertexAttribArray(yuv_420_p_shader.uvs);
  glVertexAttribPointer(yuv_420_p_shader.pos, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);
  glVertexAttribPointer(yuv_420_p_shader.uvs, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid*)sizeof(vertices)); /// last is offset to loc in buf memory
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GL_shader::draw_rgb(GLuint texture) {
  glUseProgram(rgba_shader.program);
  glBindTexture(GL_TEXTURE_2D, texture);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindTexture(GL_TEXTURE_2D, 0);
  checkGlError("Draw RGBA texture");
}

void GL_shader::draw_egl(GLuint texture) {
  glUseProgram(egl_shader.program);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES,texture);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES,0);
  checkGlError("Draw EGL texture");
}

void GL_shader::draw_YUV420P(GLuint textureY, GLuint textureU, GLuint textureV) {
  glUseProgram(yuv_420_p_shader.program);
  for(int i=0;i<3;i++){
	glActiveTexture(GL_TEXTURE0 + i);
	GLuint texture;
	if(i==0)texture=textureY;
	if(i==1)texture=textureU;
	if(i==2)texture=textureV;
	glBindTexture(GL_TEXTURE_2D,texture);
  }
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindTexture(GL_TEXTURE_2D, 0);
  checkGlError("Draw NV12 texture");
}

void GL_shader::draw_NV12(GLuint textureY, GLuint textureUV) {
  glUseProgram(nv_12_shader.program);
  glActiveTexture(GL_TEXTURE0 + 0);
  glBindTexture(GL_TEXTURE_2D,textureY);
  glActiveTexture(GL_TEXTURE0 + 1);
  glBindTexture(GL_TEXTURE_2D,textureUV);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindTexture(GL_TEXTURE_2D, 0);
  checkGlError("Draw NV12 texture");
}
