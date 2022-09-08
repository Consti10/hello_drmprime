//
// Created by consti10 on 30.08.22.
//

#include "egl_out.h"
#include "../common_consti/TimeHelper.hpp"
#include "ffmpeg_workaround_api_version.h"

#include <cassert>
#include "extra_drm.h"

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

static void print_hwframe_transfer_formats(AVBufferRef *hwframe_ctx){
  enum AVPixelFormat *formats;
  const auto err = av_hwframe_transfer_get_formats(hwframe_ctx, AV_HWFRAME_TRANSFER_DIRECTION_FROM, &formats, 0);
  if (err < 0) {
	std::cout<<"av_hwframe_transfer_get_formats error\n";
	return;
  }
  std::stringstream ss;
  ss<<"Supported transfers:";
  for (int i = 0; formats[i] != AV_PIX_FMT_NONE; i++) {
	ss<<i<<":"<<av_get_pix_fmt_name(formats[i])<<",";
  }
  ss<<"\n";
  std::cout<<ss.str();
  av_freep(&formats);
}

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
	"	float Y = texture2D(s_texture_y, v_texCoord).r;\n"
	"	float U = texture2D(s_texture_u, v_texCoord).r;\n"
	"	float V = texture2D(s_texture_v, v_texCoord).r;\n"
	"	vec3 color = vec3(Y, U, V);"
	"	mat3 colorMatrix = mat3(\n"
	"		1,   0,       1.402,\n"
	"		1,  -0.344,  -0.714,\n"
	"		1,   1.772,   0);\n"
	"	gl_FragColor = vec4(color*colorMatrix, 1.0);\n"
	"}\n";
static const GLchar* fragment_shader_source_NV12 =
	"#version 300 es\n"
	"precision mediump float;\n"
	"uniform sampler2D s_texture_y;\n"
	"uniform sampler2D s_texture_uv;\n"
	"in vec2 v_texCoord;\n"
	"void main() {	\n"
	"	vec3 YCbCr = vec3(\n"
	"		texture2D(s_texture_y, v_texCoord)[0],\n"
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
  // Shader 1
  egl_shader.program = common_get_shader_program(vertex_shader_source, fragment_shader_source_GL_OES_EGL_IMAGE_EXTERNAL);
  egl_shader.pos = glGetAttribLocation(egl_shader.program, "position");
  egl_shader.uvs = glGetAttribLocation(egl_shader.program, "tx_coords");
  // Shader 2
  rgba_shader.program = common_get_shader_program(vertex_shader_source,fragment_shader_source_RGB);
  rgba_shader.pos = glGetAttribLocation(rgba_shader.program, "position");
  assert(rgba_shader.pos>=0);
  rgba_shader.uvs = glGetAttribLocation(rgba_shader.program, "tx_coords");
  assert(rgba_shader.uvs>=0);
  rgba_shader.sampler = glGetUniformLocation(rgba_shader.program, "s_texture" );
  assert(rgba_shader.sampler>=0);
  // Shader 3
  nv_12_shader.program= common_get_shader_program(vertex_shader_source, fragment_shader_source_NV12);
  nv_12_shader.pos = glGetAttribLocation(nv_12_shader.program, "position");
  assert(nv_12_shader.pos>=0);
  nv_12_shader.uvs = glGetAttribLocation(nv_12_shader.program, "tx_coords");
  assert(nv_12_shader.uvs>=0);
  nv_12_shader.s_texture_y=glGetUniformLocation(nv_12_shader.program, "s_texture_y");
  nv_12_shader.s_texture_uv=glGetUniformLocation(nv_12_shader.program, "s_texture_uv");
  //nv_12_shader.s_texture_v=glGetUniformLocation(nv_12_shader.program, "s_texture_v");
  assert(nv_12_shader.s_texture_y>=0);
  assert(nv_12_shader.s_texture_uv>=0);
  //assert(nv_12_shader.s_texture_v>=0);
  checkGlError("NV12");
  // Shader 4
  {
	yuv_420_p_shader.program= common_get_shader_program(vertex_shader_source, fragment_shader_source_YUV420P);
	yuv_420_p_shader.pos = glGetAttribLocation(yuv_420_p_shader.program, "position");
	assert(yuv_420_p_shader.pos>=0);
	yuv_420_p_shader.uvs = glGetAttribLocation(yuv_420_p_shader.program, "tx_coords");
	assert(yuv_420_p_shader.uvs>=0);
	yuv_420_p_shader.s_texture_y=glGetUniformLocation(yuv_420_p_shader.program, "s_texture_y");
	yuv_420_p_shader.s_texture_u=glGetUniformLocation(yuv_420_p_shader.program, "s_texture_u");
	yuv_420_p_shader.s_texture_v=glGetUniformLocation(yuv_420_p_shader.program, "s_texture_v");
	assert(yuv_420_p_shader.s_texture_y>=0);
	assert(yuv_420_p_shader.s_texture_u>=0);
	assert(yuv_420_p_shader.s_texture_v>=0);
	checkGlError("YUV420P");
  }

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glViewport(0, 0, window_width, window_height);

  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices)+sizeof(uv_coords), 0, GL_STATIC_DRAW);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
  glBufferSubData(GL_ARRAY_BUFFER, sizeof(vertices), sizeof(uv_coords), uv_coords);
  glEnableVertexAttribArray(egl_shader.pos);
  glEnableVertexAttribArray(egl_shader.uvs);
  glVertexAttribPointer(egl_shader.pos, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);
  glVertexAttribPointer(egl_shader.uvs, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid*)sizeof(vertices)); /// last is offset to loc in buf memory
  //
  {
	glEnableVertexAttribArray(rgba_shader.pos);
	glEnableVertexAttribArray(rgba_shader.uvs);
	glVertexAttribPointer(rgba_shader.pos, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);
	glVertexAttribPointer(rgba_shader.uvs, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid*)sizeof(vertices)); /// last is offset to loc in buf memory
  }
  {
	glEnableVertexAttribArray(nv_12_shader.pos);
	glEnableVertexAttribArray(nv_12_shader.uvs);
	glVertexAttribPointer(nv_12_shader.pos, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);
	glVertexAttribPointer(nv_12_shader.uvs, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid*)sizeof(vertices)); /// last is offset to loc in buf memory
  }
  {
	glEnableVertexAttribArray(yuv_420_p_shader.pos);
	glEnableVertexAttribArray(yuv_420_p_shader.uvs);
	glVertexAttribPointer(yuv_420_p_shader.pos, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);
	glVertexAttribPointer(yuv_420_p_shader.uvs, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid*)sizeof(vertices)); /// last is offset to loc in buf memory
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  if(true){
	glGenTextures(1,&texture_rgb);
	assert(texture_rgb>=0);
	glBindTexture(GL_TEXTURE_2D, texture_rgb);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	uint8_t pixels[4*100*100];
	fillFrame(pixels,100,100,100*4, createColor(2,255));
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 100, 100, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glBindTexture(GL_TEXTURE_2D,0);
	checkGlError("Create texture extra");
  }
}

// https://stackoverflow.com/questions/9413845/ffmpeg-avframe-to-opengl-texture-without-yuv-to-rgb-soft-conversion
// https://bugfreeblog.duckdns.org/2022/01/yuv420p-opengl-shader-conversion.html
// https://stackoverflow.com/questions/30191911/is-it-possible-to-draw-yuv422-and-yuv420-texture-using-opengl
void EGLOut::update_texture_yuv420p(AVFrame* frame) {
  assert(frame);
  assert(frame->format==AV_PIX_FMT_YUV420P);
  MLOGD<<"update_texture_yuv420p\n";
  for(int i=0;i<3;i++){
	if(yuv_420_p_sw_frame_texture.textures[i]==0){
	  glGenTextures(1,&yuv_420_p_sw_frame_texture.textures[i]);
	}
	glBindTexture(GL_TEXTURE_2D, yuv_420_p_sw_frame_texture.textures[i]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	if(i==0){
	  // Full Y plane
	  glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, frame->width, frame->height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->data[0]);
	}else{
	  // half size U,V planes
	  glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, frame->width/2, frame->height/2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->data[i]);
	}
	glBindTexture(GL_TEXTURE_2D,0);
  }
  yuv_420_p_sw_frame_texture.has_valid_image= true;
  av_frame_free(&frame);
}

void EGLOut::update_texture_cuda(AVFrame *frame) {
  assert(frame);
  assert(frame->format==AV_PIX_FMT_CUDA);
  MLOGD<<"update_egl_texture_cuda\n";
  // We can now also give the frame back to av, since we are updating to a new one.
  if(cuda_frametexture.av_frame!= nullptr){
	av_frame_free(&cuda_frametexture.av_frame);
  }
  cuda_frametexture.av_frame=frame;
  if(m_cuda_gl_interop_helper== nullptr){
	AVHWDeviceContext* tmp=((AVHWFramesContext*)frame->hw_frames_ctx->data)->device_ctx;
	AVCUDADeviceContext* avcuda_device_context=(AVCUDADeviceContext*)tmp->hwctx;
	assert(avcuda_device_context);
	m_cuda_gl_interop_helper = std::make_unique<CUDAGLInteropHelper>(avcuda_device_context);
  }
  bool fresh=false;
  if(cuda_frametexture.textures[0]==0){
	std::cout<<"Creating cuda textures\n";
	glGenTextures(2, cuda_frametexture.textures);
	fresh= true;
  }
  if(fresh){
	for(int i=0;i<2;i++){
	  glBindTexture(GL_TEXTURE_2D,cuda_frametexture.textures[i]);
	  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	  // The first plane (Y) is full width and therefore needs 8 bits * width * height
	  // The second plane is U,V interleaved and therefre needs 8 bits * width * height, too.
	  glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, frame->width,frame->height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);
	  glBindTexture(GL_TEXTURE_2D,0);
	}
  }
  if(fresh){
	m_cuda_gl_interop_helper->registerTextures(cuda_frametexture.textures[0],cuda_frametexture.textures[1]);
  }
  glBindTexture(GL_TEXTURE_2D, cuda_frametexture.textures[0]);
  Chronometer cuda_memcpy_time{"CUDA memcpy"};
  cuda_memcpy_time.start();
  if(m_cuda_gl_interop_helper->copyCudaFrameToTextures(frame)){
	cuda_frametexture.has_valid_image= true;
  }
  cuda_memcpy_time.stop();
  // I don't think we can measure CUDA memcpy time
  //MLOGD<<"CUDA memcpy:"<<cuda_memcpy_time.getAvgReadable()<<"\n";
}


bool update_drm_prime_to_egl_texture(EGLDisplay *egl_display, EGLFrameTexture& egl_frame_texture, AVFrame* frame){
  assert(frame);
  assert(frame->format==AV_PIX_FMT_DRM_PRIME);
  auto before=std::chrono::steady_clock::now();
  // We can now also give the frame back to av, since we are updating to a new one.
  if(egl_frame_texture.av_frame!= nullptr){
	av_frame_free(&egl_frame_texture.av_frame);
  }
  egl_frame_texture.av_frame=frame;
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
	egl_frame_texture.has_valid_image= false;
	return false;
  }
  // Note that we do not have to delete and generate the texture (ID) every time we update the egl image backing.
  if(egl_frame_texture.texture==0){
	glGenTextures(1, &egl_frame_texture.texture);
  }
  glEnable(GL_TEXTURE_EXTERNAL_OES);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, egl_frame_texture.texture);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, image);
  // I do not know exactly how that works, but we seem to be able to immediately delete the EGL image, as long as we don't give the frame
  // back to the decoder I assume
  eglDestroyImageKHR(*egl_display, image);
  auto delta=std::chrono::steady_clock::now()-before;
  std::cout<<"Creating texture took:"<<std::chrono::duration_cast<std::chrono::milliseconds>(delta).count()<<"ms\n";
  egl_frame_texture.has_valid_image= true;
  return true;
}

// "Consumes" the given hw_frame
void EGLOut::update_texture(AVFrame *hw_frame) {
  if(hw_frame->format == AV_PIX_FMT_DRM_PRIME){
	EGLDisplay egl_display=eglGetCurrentDisplay();
	update_drm_prime_to_egl_texture(&egl_display, egl_frame_texture,hw_frame);
  }else if(hw_frame->format==AV_PIX_FMT_CUDA){
	update_texture_cuda(hw_frame);
	/*AVFrame* sw_frame = av_frame_alloc();
	assert(sw_frame);
	print_hwframe_transfer_formats(hw_frame->hw_frames_ctx);
	Chronometer tmp{"AV hwframe transfer"};
	tmp.start();
	sw_frame->format = AV_PIX_FMT_NV12;
	if (av_hwframe_transfer_data(sw_frame, hw_frame,0) != 0) {
	  fprintf(stderr, "Failed to transfer frame (format=%d) to DRM_PRiME %s\n", hw_frame->format,strerror(errno));
	  av_frame_free(&sw_frame);
	  return;
	}
	tmp.stop();
	MLOGD<<"Transfer:"<<tmp.getAvgReadable()<<"\n";
	//update_texture_rgb(sw_frame);
	av_frame_free(&hw_frame);*/
  }else if(hw_frame->format==AV_PIX_FMT_YUV420P){
	update_texture_yuv420p(hw_frame);
  }
  else{
	std::cerr<<"Unimplemented to texture"<<av_get_pix_fmt_name((AVPixelFormat)hw_frame->format)<<"\n";
	av_frame_free(&hw_frame);
  }
}

void EGLOut::render_once() {
  if(frame_delta_chrono== nullptr){
	frame_delta_chrono=std::make_unique<Chronometer>("FrameDelta");
	frame_delta_chrono->start();
  }else{
	frame_delta_chrono->stop();
	frame_delta_chrono->printInIntervalls(std::chrono::seconds(3), false);
	frame_delta_chrono->start();
  }
  //std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // update the video frame to the most recent one
  // A bit overkill, but it was quicker to just copy paste the logic from hello_drmprime.
  const auto allBuffers=queue->getAllAndClear();
  if(!allBuffers.empty()) {
	const int nDroppedFrames = (int)allBuffers.size() - 1;
	if (nDroppedFrames != 0) {
	  MLOGD << "N dropped:" << nDroppedFrames << "\n";
	}
	// don't forget to free the dropped frames
	for (int i = 0; i < nDroppedFrames; i++) {
	  av_frame_free(&allBuffers[i]->frame);
	}
	// The latest frame is the last one we did not drop
	const auto latest_new_frame = allBuffers[nDroppedFrames]->frame;
	// This will free the last (rendered) av frame if given.
	update_texture(latest_new_frame);
  }
  glClearColor(1.0, 0.0, 0.0, 1.0);
  glClear(GL_COLOR_BUFFER_BIT |GL_DEPTH_BUFFER_BIT| GL_STENCIL_BUFFER_BIT);
  // Only render the texture if we have one (aka we have gotten at least one frame from the decoder)
  // Note that otherwise, if we render via OpenGL but the texture has no backing, nothing really happens ;)
  if(egl_frame_texture.has_valid_image){
	glUseProgram(egl_shader.program);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, egl_frame_texture.texture);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES,0);
	checkGlError("Draw EGL texture");
  }else if(cuda_frametexture.has_valid_image) {
	glUseProgram(nv_12_shader.program);
	for(int i=0;i<2;i++){
	  glActiveTexture(GL_TEXTURE0 + i);
	  glBindTexture(GL_TEXTURE_2D,cuda_frametexture.textures[i]);
	}
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindTexture(GL_TEXTURE_2D, 0);
	checkGlError("Draw NV12 texture");
  }else if(yuv_420_p_sw_frame_texture.has_valid_image){
	/*glUseProgram(rgba_shader.program);
	glBindTexture(GL_TEXTURE_2D, yuv_420_p_sw_frame_texture.textures[0]);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindTexture(GL_TEXTURE_2D, 0);
	checkGlError("Draw YUV420P texture");*/
	glUseProgram(yuv_420_p_shader.program);
	for(int i=0;i<3;i++){
	  glActiveTexture(GL_TEXTURE0 + i);
	  glBindTexture(GL_TEXTURE_2D,yuv_420_p_sw_frame_texture.textures[i]);
	}
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindTexture(GL_TEXTURE_2D, 0);
	checkGlError("Draw NV12 texture");
  }
  else{
	//std::cout<<"Draw RGBA texture\n";
	glUseProgram(rgba_shader.program);
	glBindTexture(GL_TEXTURE_2D, texture_rgb);
	//glBindTexture(GL_TEXTURE_2D, cuda_frametexture.textures[0]);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindTexture(GL_TEXTURE_2D, 0);
	checkGlError("Draw RGBA texture");
  }
  glfwSwapBuffers(window);
}


int EGLOut::queue_new_frame_for_display(struct AVFrame *src_frame) {
  assert(src_frame);
  //std::cout<<"DRMPrimeOut::drmprime_out_display "<<src_frame->width<<"x"<<src_frame->height<<"\n";
  if ((src_frame->flags & AV_FRAME_FLAG_CORRUPT) != 0) {
	fprintf(stderr, "Discard corrupt frame: fmt=%d, ts=%" PRId64 "\n", src_frame->format, src_frame->pts);
	return 0;
  }
  AVFrame *frame;
  if (src_frame->format == AV_PIX_FMT_DRM_PRIME) {
	frame = av_frame_alloc();
	assert(frame);
	av_frame_ref(frame, src_frame);
	//printf("format == AV_PIX_FMT_DRM_PRIME\n");
  } else if (src_frame->format == AV_PIX_FMT_VAAPI) {
	// Apparently we can directly convert VAAPI to DRM PRIME and use it with egl
	// (At least on Pi 4 & h264! decode ?!)
	//printf("format == AV_PIX_FMT_VAAPI\n");
	frame = av_frame_alloc();
	assert(frame);
	frame->format = AV_PIX_FMT_DRM_PRIME;
	if (av_hwframe_map(frame, src_frame, 0) != 0) {
	  fprintf(stderr, "Failed to map frame (format=%d) to DRM_PRiME\n", src_frame->format);
	  av_frame_free(&frame);
	  return AVERROR(EINVAL);
	}
  }else{
	// We cannot DMABUF Map this frame to an egl image(at least not yet).
	// Any conversion has to be done later.
	frame = av_frame_alloc();
	assert(frame);
	if(av_frame_ref(frame, src_frame)!=0){
	  fprintf(stderr, "av_frame_ref error\n");
	  av_frame_free(&frame);
	  return AVERROR(EINVAL);
	}
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

