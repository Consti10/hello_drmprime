//
// Created by consti10 on 30.08.22.
//

#include "egl_out.h"
#include <cassert>

static const GLchar* vertex_shader_source =
	"#version 300 es\n"
	"in vec3 position;\n"
	"in vec2 tx_coords;\n"
	"out vec2 v_texCoord;\n"
	"void main() {  \n"
	"	gl_Position = vec4(position, 1.0);\n"
	"	v_texCoord = tx_coords;\n"
	"}\n";

static const GLchar* fragment_shader_source =
	"#version 300 es\n"
	"#extension GL_OES_EGL_image_external : require\n"
	"precision mediump float;\n"
	"uniform samplerExternalOES texture;\n"
	"in vec2 v_texCoord;\n"
	"out vec4 out_color;\n"
	"void main() {	\n"
	"	out_color = texture2D( texture, v_texCoord );\n"
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

void EGLOut::initializeWindowRender() {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
  glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

  window = glfwCreateWindow(window_width, window_height, __FILE__,NULL, NULL);

  glfwMakeContextCurrent(window);

  //EGLDisplay egl_display = glfwGetEGLDisplay();
  EGLDisplay egl_display = eglGetCurrentDisplay();
  if(egl_display == EGL_NO_DISPLAY) {
	printf("error: glfwGetEGLDisplay no EGLDisplay returned\n");
  }

  printf("GL_VERSION  : %s\n", glGetString(GL_VERSION) );
  printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER) );

  shader_program = common_get_shader_program(vertex_shader_source, fragment_shader_source);
  pos = glGetAttribLocation(shader_program, "position");
  uvs = glGetAttribLocation(shader_program, "tx_coords");

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glViewport(0, 0, window_width, window_height);

  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices)+sizeof(uv_coords), 0, GL_STATIC_DRAW);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
  glBufferSubData(GL_ARRAY_BUFFER, sizeof(vertices), sizeof(uv_coords), uv_coords);
  glEnableVertexAttribArray(pos);
  glEnableVertexAttribArray(uvs);
  glVertexAttribPointer(pos, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);
  glVertexAttribPointer(uvs, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid*)sizeof(vertices)); /// last is offset to loc in buf memory
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void EGLOut::render_once() {
  glClearColor(1.0, 0.0, 0.0, 1.0);
  glClear(GL_COLOR_BUFFER_BIT |GL_DEPTH_BUFFER_BIT| GL_STENCIL_BUFFER_BIT);
  glfwSwapBuffers(window);
}


int EGLOut::out_display(struct AVFrame *src_frame) {
  assert(src_frame);
  //std::cout<<"DRMPrimeOut::drmprime_out_display "<<src_frame->width<<"x"<<src_frame->height<<"\n";
  AVFrame *frame;
  if ((src_frame->flags & AV_FRAME_FLAG_CORRUPT) != 0) {
	fprintf(stderr, "Discard corrupt frame: fmt=%d, ts=%" PRId64 "\n", src_frame->format, src_frame->pts);
	return 0;
  }
  if (src_frame->format == AV_PIX_FMT_DRM_PRIME) {
	frame = av_frame_alloc();
	av_frame_ref(frame, src_frame);
	//printf("format == AV_PIX_FMT_DRM_PRIME\n");
  } else if (src_frame->format == AV_PIX_FMT_VAAPI) {
	//printf("format == AV_PIX_FMT_VAAPI\n");
	frame = av_frame_alloc();
	frame->format = AV_PIX_FMT_DRM_PRIME;
	if (av_hwframe_map(frame, src_frame, 0) != 0) {
	  fprintf(stderr, "Failed to map frame (format=%d) to DRM_PRiME\n", src_frame->format);
	  av_frame_free(&frame);
	  return AVERROR(EINVAL);
	}
  } else {
	fprintf(stderr, "Frame (format=%d) not DRM_PRiME\n", src_frame->format);
	return AVERROR(EINVAL);
  }
  // Here the delay is still neglegible,aka ~0.15ms
  const auto delayBeforeDisplayQueueUs=getTimeUs()-frame->pts;
  MLOGD<<"delayBeforeDisplayQueue:"<<frame->pts<<" delay:"<<(delayBeforeDisplayQueueUs/1000.0)<<" ms\n";
  // push it immediately, even though frame(s) might already be inside the queue
  queue->push(std::make_shared<XAVFrameHolder>(frame));
  return 0;
}
