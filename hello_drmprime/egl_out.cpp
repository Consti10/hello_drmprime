//
// Created by consti10 on 30.08.22.
//

#include "egl_out.h"
#include "ffmpeg_workaround_api_version.h"

#include <cassert>
#include "extra_drm.h"

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

static void create_rgba_texture(GLuint& tex_id,uint32_t color_rgba){
  assert(tex_id==0);
  glGenTextures(1,&tex_id);
  assert(tex_id>=0);
  glBindTexture(GL_TEXTURE_2D, tex_id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  const int width=1280;
  const int height=720;
  uint8_t pixels[4*width*height];
  fillFrame(pixels,width,height,width*4, color_rgba);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glBindTexture(GL_TEXTURE_2D,0);
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

  gl_shader=std::make_unique<GL_shader>();
  gl_shader->initialize();

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glViewport(0, 0, window_width, window_height);

  create_rgba_texture(texture_rgb_green, createColor(1,255));
  create_rgba_texture(texture_rgb_blue, createColor(2,255));
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
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
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
	printf("Failed to create EGLImage %s\n", strerror(errno));
	egl_frame_texture.has_valid_image= false;
	print_hwframe_transfer_formats(frame->hw_frames_ctx);
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

//https://registry.khronos.org/OpenGL/extensions/NV/NV_vdpau_interop.txt
void EGLOut::update_texture_vdpau(AVFrame* hw_frame) {
  assert(hw_frame);
  av_frame_free(&hw_frame);
}

// "Consumes" the given hw_frame (makes sure it is freed at the apropriate time / the previous one
// is freed when updating to a new one.
void EGLOut::update_texture(AVFrame *hw_frame) {
  if(hw_frame->format == AV_PIX_FMT_DRM_PRIME){
	EGLDisplay egl_display=eglGetCurrentDisplay();
	update_drm_prime_to_egl_texture(&egl_display, egl_frame_texture,hw_frame);
  }else if(hw_frame->format==AV_PIX_FMT_CUDA){
	update_texture_cuda(hw_frame);
  }else if(hw_frame->format==AV_PIX_FMT_YUV420P){
	update_texture_yuv420p(hw_frame);
  }
  else{
	std::cerr<<"Unimplemented to texture:"<<av_get_pix_fmt_name((AVPixelFormat)hw_frame->format)<<"\n";
	print_hwframe_transfer_formats(hw_frame->hw_frames_ctx);
	av_frame_free(&hw_frame);
  }
}

void EGLOut::render_once() {
  cpu_frame_time.start();
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
  // We use Red as the clear color such that it is easier t debug (black) video textures.
  glClearColor(1.0, 0.0, 0.0, 1.0);
  glClear(GL_COLOR_BUFFER_BIT |GL_DEPTH_BUFFER_BIT| GL_STENCIL_BUFFER_BIT);
  // Only render the texture if we have one (aka we have gotten at least one frame from the decoder)
  // Note that otherwise, if we render via OpenGL but the texture has no backing, nothing really happens ;)
  if(egl_frame_texture.has_valid_image){
	gl_shader->draw_egl(egl_frame_texture.texture);
  }else if(cuda_frametexture.has_valid_image) {
	gl_shader->draw_NV12(cuda_frametexture.textures[0],cuda_frametexture.textures[1]);
  }else if(yuv_420_p_sw_frame_texture.has_valid_image){
	gl_shader->draw_YUV420P(yuv_420_p_sw_frame_texture.textures[0],
							yuv_420_p_sw_frame_texture.textures[1],
							yuv_420_p_sw_frame_texture.textures[2]);
  }
  else{
	const auto rgb_texture=frameCount%2==0? texture_rgb_blue:texture_rgb_green;
	gl_shader->draw_rgb(rgb_texture);
  }
  cpu_frame_time.stop();
  cpu_frame_time.printInIntervalls(std::chrono::seconds(3), false);
  glfwSwapBuffers(window);
  frameCount++;
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
	// ? https://gist.github.com/kajott/d1b29c613be30893c855621edd1f212e ?
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
  const auto delayBeforeDisplayQueueUs=getTimeUs()-frame->pts;
  avg_delay_before_display_queue.addUs(delayBeforeDisplayQueueUs);
  avg_delay_before_display_queue.printInIntervals(std::chrono::seconds(3));
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
  glfwTerminate();
}

