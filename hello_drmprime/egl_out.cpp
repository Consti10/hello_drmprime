//
// Created by consti10 on 30.08.22.
//

#include "egl_out.h"
#include "avcodec_helper.hpp"

#include <cassert>
#include "extra_drm.h"



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

void EGLOut::initializeWindowRenderGlfw() {
#ifndef X_USE_SDL
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
  glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  //glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

  if(window_width==0 || window_height==0){
	const auto monitor=glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = glfwGetVideoMode(monitor);
	window = glfwCreateWindow(mode->width,mode->height,__FILE__,monitor, nullptr);
	window_width=mode->width;
	window_height=mode->height;
  }else{
	window = glfwCreateWindow(window_width, window_height, __FILE__, nullptr, nullptr);
  }

  glfwMakeContextCurrent(window);

  //EGLDisplay egl_display = glfwGetEGLDisplay();
  EGLDisplay egl_display = eglGetCurrentDisplay();
  if(egl_display == EGL_NO_DISPLAY) {
	printf("error: glfwGetEGLDisplay no EGLDisplay returned\n");
  }
  glfwSwapInterval(0);
  setup_gl();
#else
  assert(false);
#endif
}

void EGLOut::initializeWindowRendererSDL() {
#ifdef X_USE_SDL
  if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
	printf("error initializing SDL: %s\n", SDL_GetError());
	return;
  }
  const bool fullscreen=window_width==0 || window_height==0;
  const auto flags = fullscreen ? SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL : SDL_WINDOW_OPENGL;
  win = SDL_CreateWindow("GAME",
									 SDL_WINDOWPOS_CENTERED,
									 SDL_WINDOWPOS_CENTERED,
									window_width, window_height, flags);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  // triggers the program that controls
  // your graphics hardware and sets flags
  Uint32 render_flags = SDL_RENDERER_ACCELERATED;
  // creates a renderer to render our images
  rend = SDL_CreateRenderer(win, -1, render_flags);
  setup_gl();
#else
  assert(false);
#endif
}

void EGLOut::setup_gl() {
  GL_VideoRenderer::debug_info();
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glViewport(0, 0, window_width, window_height);
  gl_video_renderer=std::make_unique<GL_VideoRenderer>();
#ifdef X_USE_SDL
  gl_video_renderer->init_gl(rend);
#else
  gl_video_renderer->init_gl(nullptr);
#endif
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
  // We use Red as the clear color such that it is easier to debug (black) video textures.
  cpu_glclear_time.start();
  glClearColor(1.0, 0.0, 0.0, 1.0);
  glClear(GL_COLOR_BUFFER_BIT |GL_DEPTH_BUFFER_BIT| GL_STENCIL_BUFFER_BIT);
  cpu_glclear_time.stop();
  AVFrame* new_frame=fetch_latest_decoded_frame();
  if(new_frame!= nullptr){
	// update the texture with this frame
	m_display_stats.n_frames_rendered++;
	cpu_update_texture.start();
	gl_video_renderer->update_texture_gl(new_frame);
	cpu_update_texture.stop();
	cpu_update_texture.printInIntervalls(std::chrono::seconds(3), false);
  }
  cpu_glclear_time.printInIntervalls(std::chrono::seconds(3), false);
  // Only render the texture if we have one (aka we have gotten at least one frame from the decoder)
  // Note that otherwise, if we render via OpenGL but the texture has no backing, nothing really happens ;)
  gl_video_renderer->draw_texture_gl();
  cpu_frame_time.stop();
  cpu_frame_time.printInIntervalls(std::chrono::seconds(3), false);
  cpu_swap_time.start();
#ifdef X_USE_SDL
  SDL_RenderPresent(rend);
#else
  glfwSwapBuffers(window);
#endif
  cpu_swap_time.stop();
  cpu_swap_time.printInIntervalls(std::chrono::seconds(3), false);
  frameCount++;
}


AVFrame *EGLOut::fetch_latest_decoded_frame() {
  std::lock_guard<std::mutex> lock(latest_frame_mutex);
  if(m_latest_frame!= nullptr) {
	// Make a copy and write nullptr to the thread-shared variable such that
	// it is not freed by the providing thread.
	AVFrame* new_frame = m_latest_frame;
	m_latest_frame = nullptr;
	return new_frame;
  }
  return nullptr;
}


int EGLOut::queue_new_frame_for_display(struct AVFrame *src_frame) {
  //if(true)return 0;
  assert(src_frame);
  //std::cout<<"DRMPrimeOut::drmprime_out_display "<<src_frame->width<<"x"<<src_frame->height<<"\n";
  if ((src_frame->flags & AV_FRAME_FLAG_CORRUPT) != 0) {
	fprintf(stderr, "Discard corrupt frame: fmt=%d, ts=%" PRId64 "\n", src_frame->format, src_frame->pts);
	return 0;
  }
  update_frame_producer.start();
  latest_frame_mutex.lock();
  // We drop a frame that has (not yet) been consumed by the render thread to whatever is the newest available.
  if(m_latest_frame!= nullptr){
	av_frame_free(&m_latest_frame);
	std::cout<<"Dropping frame\n";
	m_display_stats.n_frames_dropped++;
  }
  AVFrame *frame=frame = av_frame_alloc();
  assert(frame);
  if(av_frame_ref(frame, src_frame)!=0){
	fprintf(stderr, "av_frame_ref error\n");
	av_frame_free(&frame);
	// don't forget to give up the lock
	latest_frame_mutex.unlock();
	return AVERROR(EINVAL);
  }
  m_latest_frame=frame;
  latest_frame_mutex.unlock();
  update_frame_producer.stop();
  update_frame_producer.printInIntervalls(std::chrono::seconds(3), false);
  return 0;
}

void EGLOut::render_thread_run() {
#ifdef X_USE_SDL
  initializeWindowRendererSDL();
  render_ready= true;
  int close = 0;
  while (!close && !terminate ) {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
	  switch (event.type) {
		case SDL_QUIT:
		  close = 1;
		  break;
	  }
	}
	render_once();
  }
  SDL_DestroyRenderer(rend);
  SDL_DestroyWindow(win);
  SDL_Quit();
#else
  initializeWindowRenderGlfw();
  render_ready= true;
  while (!glfwWindowShouldClose(window) && !terminate){
	glfwPollEvents();  /// for mouse window closing
	render_once();
  }
  glfwTerminate();
#endif
}


