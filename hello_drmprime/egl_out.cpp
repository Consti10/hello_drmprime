//
// Created by consti10 on 30.08.22.
//

#include "egl_out.h"
#include "../common_consti/TimeHelper.hpp"
#include "ffmpeg_workaround_api_version.h"

#include <cassert>

static EGLint texgen_attrs[] = {
	EGL_DMA_BUF_PLANE0_FD_EXT,
	EGL_DMA_BUF_PLANE0_OFFSET_EXT,
	EGL_DMA_BUF_PLANE0_PITCH_EXT,
	EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
	EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
	EGL_DMA_BUF_PLANE1_FD_EXT,
	EGL_DMA_BUF_PLANE1_OFFSET_EXT,
	EGL_DMA_BUF_PLANE1_PITCH_EXT,
	EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
	EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT,
	EGL_DMA_BUF_PLANE2_FD_EXT,
	EGL_DMA_BUF_PLANE2_OFFSET_EXT,
	EGL_DMA_BUF_PLANE2_PITCH_EXT,
	EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT,
	EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT,
};

static const GLchar* vertex_shader_source =
	"#version 300 es\n"
	"in vec3 position;\n"
	"in vec2 tx_coords;\n"
	"out vec2 v_texCoord;\n"
	"void main() {  \n"
	"	gl_Position = vec4(position, 1.0);\n"
	"	v_texCoord = tx_coords;\n"
	"}\n";

static const GLchar* fragment_shader_source_GL_OES_EGL_IMAGE_EXTERNAL =
	"#version 300 es\n"
	"#extension GL_OES_EGL_image_external : require\n"
	"precision mediump float;\n"
	"uniform samplerExternalOES texture;\n"
	"in vec2 v_texCoord;\n"
	"out vec4 out_color;\n"
	"void main() {	\n"
	"	out_color = texture2D( texture, v_texCoord );\n"
	"}\n";

static const GLchar* fragment_shader_source_RGB =
	"#version 300 es\n"
	"precision mediump float;\n"
	"uniform sampler2D texture;\n"
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

EGLOut::EGLOut(int width, int height) :window_width(width),window_height(height){
  render_thread=std::make_unique<std::thread>([this](){
	render_thread_run();
  });
}

EGLOut::~EGLOut() {
  terminate=true;
  if(render_thread->joinable()){
	render_thread->join();
  }
}

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
  //glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

  window = glfwCreateWindow(window_width, window_height, __FILE__,NULL, NULL);

  glfwMakeContextCurrent(window);

  //EGLDisplay egl_display = glfwGetEGLDisplay();
  EGLDisplay egl_display = eglGetCurrentDisplay();
  if(egl_display == EGL_NO_DISPLAY) {
	printf("error: glfwGetEGLDisplay no EGLDisplay returned\n");
  }

  printf("GL_VERSION  : %s\n", glGetString(GL_VERSION) );
  printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER) );

  shader_program_egl_external = common_get_shader_program(vertex_shader_source, fragment_shader_source_GL_OES_EGL_IMAGE_EXTERNAL);
  pos = glGetAttribLocation(shader_program_egl_external, "position");
  uvs = glGetAttribLocation(shader_program_egl_external, "tx_coords");

  shader_program_rgb = common_get_shader_program(vertex_shader_source,fragment_shader_source_RGB);
  pos_rgb = glGetAttribLocation(shader_program_rgb, "position");
  uvs_rgb = glGetAttribLocation(shader_program_rgb, "tx_coords");

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
  //
  {
	glEnableVertexAttribArray(pos_rgb);
	glEnableVertexAttribArray(uvs_rgb);
	glVertexAttribPointer(pos_rgb, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);
	glVertexAttribPointer(uvs_rgb, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid*)sizeof(vertices)); /// last is offset to loc in buf memory
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void EGLOut::update_egl_texture_cuda(EGLDisplay *egl_display, FrameTexture &frame_texture, AVFrame *frame) {
  assert(frame);
  assert(frame->format==AV_PIX_FMT_CUDA);
  MLOGD<<"update_egl_texture_cuda\n";
  // We can now also give the frame back to av, since we are updating to a new one.
  if(frame_texture.av_frame!= nullptr){
	av_frame_free(&frame_texture.av_frame);
  }
  if(frame_texture.texture==0){
	glGenTextures(1, &frame_texture.texture);
  }
  if(m_cuda_gl_interop_helper== nullptr){
	AVHWDeviceContext* tmp=((AVHWFramesContext*)frame->hw_frames_ctx->data)->device_ctx;
	AVCUDADeviceContext* avcuda_device_context=(AVCUDADeviceContext*)tmp->hwctx;
	assert(avcuda_device_context);
	m_cuda_gl_interop_helper = std::make_unique<CUDAGLInteropHelper>(avcuda_device_context);
  }
  glBindTexture(GL_TEXTURE_2D, frame_texture.texture);
  m_cuda_gl_interop_helper->registerBoundTextures();
  m_cuda_gl_interop_helper->copyCudaFrameToTextures(frame);
}


bool update_drm_prime_to_egl_texture(EGLDisplay *egl_display,FrameTexture& frame_texture,AVFrame* frame){
  assert(frame);
  assert(frame->format==AV_PIX_FMT_DRM_PRIME);
  auto before=std::chrono::steady_clock::now();
  // We can now also give the frame back to av, since we are updating to a new one.
  if(frame_texture.av_frame!= nullptr){
	av_frame_free(&frame_texture.av_frame);
  }
  frame_texture.av_frame=frame;
  const AVDRMFrameDescriptor *desc = (AVDRMFrameDescriptor*)frame->data[0];
  // Writing all the EGL attribs - I just copied and pasted it, and it works.
  EGLint attribs[50];
  EGLint * a = attribs;
  const EGLint * b = texgen_attrs;
  *a++ = EGL_WIDTH;
  *a++ = av_frame_cropped_width(frame);
  *a++ = EGL_HEIGHT;
  *a++ = av_frame_cropped_height(frame);
  *a++ = EGL_LINUX_DRM_FOURCC_EXT;
  *a++ = desc->layers[0].format;
  int i, j;
  for (i = 0; i < desc->nb_layers; ++i) {
	for (j = 0; j < desc->layers[i].nb_planes; ++j) {
	  const AVDRMPlaneDescriptor * const p = desc->layers[i].planes + j;
	  const AVDRMObjectDescriptor * const obj = desc->objects + p->object_index;
	  *a++ = *b++;
	  *a++ = obj->fd;
	  *a++ = *b++;
	  *a++ = p->offset;
	  *a++ = *b++;
	  *a++ = p->pitch;
	  if (obj->format_modifier == 0) {
		b += 2;
	  }
	  else {
		*a++ = *b++;
		*a++ = (EGLint)(obj->format_modifier & 0xFFFFFFFF);
		*a++ = *b++;
		*a++ = (EGLint)(obj->format_modifier >> 32);
	  }
	}
  }
  *a = EGL_NONE;
  const EGLImage image = eglCreateImageKHR(*egl_display,
										   EGL_NO_CONTEXT,
										   EGL_LINUX_DMA_BUF_EXT,
										   NULL, attribs);
  if (!image) {
	printf("Failed to create EGLImage\n");
	frame_texture.has_valid_image= false;
	return false;
  }
  // Note that we do not have to delete and generate the texture (ID) every time we update the egl image backing.
  if(frame_texture.texture==0){
	glGenTextures(1, &frame_texture.texture);
  }
  glEnable(GL_TEXTURE_EXTERNAL_OES);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, frame_texture.texture);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, image);
  // I do not know exactly how that works, but we seem to be able to immediately delete the EGL image, as long as we don't give the frame
  // back to the decoder I assume
  eglDestroyImageKHR(*egl_display, image);
  auto delta=std::chrono::steady_clock::now()-before;
  std::cout<<"Creating texture took:"<<std::chrono::duration_cast<std::chrono::milliseconds>(delta).count()<<"ms\n";
  frame_texture.has_valid_image= true;
  return true;
}

void EGLOut::render_once() {
  // update the frame to the most recent one
  // A bit overkill, but it was quicker to just copy paste the logic from hello_drmprime.
  const auto allBuffers=queue->getAllAndClear();
  if(allBuffers.size()>0) {
	const int nDroppedFrames = allBuffers.size() - 1;
	if (nDroppedFrames != 0) {
	  MLOGD << "N dropped:" << nDroppedFrames << "\n";
	}
	// don't forget to free the dropped frames
	for (int i = 0; i < nDroppedFrames; i++) {
	  av_frame_free(&allBuffers[i]->frame);
	}
	const auto latest_new_frame = allBuffers[nDroppedFrames];
	EGLDisplay egl_display=eglGetCurrentDisplay();
	// This will free the last av frame if given.
	if(latest_new_frame->frame->format==AV_PIX_FMT_CUDA){
	  update_egl_texture_cuda(&egl_display,frame_texture,latest_new_frame->frame);
	}else if(latest_new_frame->frame->format==AV_PIX_FMT_DRM_PRIME){
	  update_drm_prime_to_egl_texture(&egl_display,frame_texture,latest_new_frame->frame);
	}else{
	  std::cerr<<"Unimplemented to texture\n";
	}
  }
  glClearColor(1.0, 0.0, 0.0, 1.0);
  glClear(GL_COLOR_BUFFER_BIT |GL_DEPTH_BUFFER_BIT| GL_STENCIL_BUFFER_BIT);
  // Only render the texture if we have one (aka we have gotten at least one frame from the decoder)
  // Note that otherwise, if we render via OpenGL but the texture has no backing, nothing really happens ;)
  if(frame_texture.has_valid_image){
	glUseProgram(shader_program_egl_external);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, frame_texture.texture);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  }else{
	glUseProgram(shader_program_rgb);

  }
  glfwSwapBuffers(window);
}


int EGLOut::queue_new_frame_for_display(struct AVFrame *src_frame) {
  if(true){
	return 0;
  }
  assert(src_frame);
  //std::cout<<"DRMPrimeOut::drmprime_out_display "<<src_frame->width<<"x"<<src_frame->height<<"\n";
  AVFrame *frame;
  if ((src_frame->flags & AV_FRAME_FLAG_CORRUPT) != 0) {
	fprintf(stderr, "Discard corrupt frame: fmt=%d, ts=%" PRId64 "\n", src_frame->format, src_frame->pts);
	return 0;
  }
  if (src_frame->format == AV_PIX_FMT_DRM_PRIME) {
	frame = av_frame_alloc();
	assert(frame);
	av_frame_ref(frame, src_frame);
	//printf("format == AV_PIX_FMT_DRM_PRIME\n");
  } else if (src_frame->format == AV_PIX_FMT_VAAPI) {
	//printf("format == AV_PIX_FMT_VAAPI\n");
	frame = av_frame_alloc();
	assert(frame);
	frame->format = AV_PIX_FMT_DRM_PRIME;
	if (av_hwframe_map(frame, src_frame, 0) != 0) {
	  fprintf(stderr, "Failed to map frame (format=%d) to DRM_PRiME\n", src_frame->format);
	  av_frame_free(&frame);
	  return AVERROR(EINVAL);
	}
  }else if(src_frame->format==AV_PIX_FMT_CUDA){
	// We have a special logic for CUDA
	frame = av_frame_alloc();
	assert(frame);
	av_frame_ref(frame, src_frame);
	MLOGD<<"Warning stored CUDA frame, needs special conversion to OpenGL\n";
	/*frame = av_frame_alloc();
	assert(frame);
	frame->format = AV_PIX_FMT_NV12;
	Chronometer tmp{"AV hwframe transfer"};
	tmp.start();
	if (av_hwframe_transfer_data(frame, src_frame,0) != 0) {
	  fprintf(stderr, "Failed to transfer frame (format=%d) to DRM_PRiME %s\n", src_frame->format,strerror(errno));
	  av_frame_free(&frame);
	  return AVERROR(EINVAL);
	}
	tmp.stop();
	MLOGD<<""<<tmp.getAvgReadable()<<"\n";*/
  }
  else {
	fprintf(stderr, "Frame (format=%d) not DRM_PRiME / cannot be converted to DRM_PRIME\n", src_frame->format);
	return AVERROR(EINVAL);
  }
  // Here the delay is still neglegible,aka ~0.15ms
  const auto delayBeforeDisplayQueueUs=getTimeUs()-frame->pts;
  MLOGD<<"delayBeforeDisplayQueue:"<<frame->pts<<" delay:"<<(delayBeforeDisplayQueueUs/1000.0)<<" ms\n";
  // push it immediately, even though frame(s) might already be inside the queue
  queue->push(std::make_shared<XAVFrameHolder>(frame));
  return 0;
}

void EGLOut::render_thread_run() {
  initializeWindowRender();
  while (!glfwWindowShouldClose(window) && !terminate){
	glfwPollEvents();  /// for mouse window closing
	render_once();
  }
  glDeleteBuffers(1, &vbo);
  glfwTerminate();
}
