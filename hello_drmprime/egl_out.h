//
// Created by consti10 on 30.08.22.
//

#ifndef HELLO_DRMPRIME_HELLO_DRMPRIME_EGL_OUT_H_
#define HELLO_DRMPRIME_HELLO_DRMPRIME_EGL_OUT_H_

#include "GL_shader.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <linux/videodev2.h>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>

#define GLFW_INCLUDE_ES2
extern "C" {
#include <GLFW/glfw3.h>
#include "glhelp.h"
}

#include <drm_fourcc.h>

extern "C" {
/// video file decode
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/avassert.h>
//
#include "libavutil/hwcontext_drm.h"
}

#include <thread>
#include <memory>
#include "../common_consti/ThreadsafeQueue.hpp"
#include "../common_consti/TimeHelper.hpp"

// XXX
#include "CUDAGLInteropHelper.h"
#include <SDL.h>

struct EGLFrameTexture{
  // I think we need to keep the av frame reference around as long as we use the generated egl texture in opengl.
  AVFrame* av_frame= nullptr;
  // In contrast to "hwdectogl", created once, then re-used with each new egl image.
  // needs to be bound to the "EGL external image" target
  GLuint texture=0;
  // set to true if the texture currently has a egl image backing it.
  bool has_valid_image=false;
};

struct CUDAFrameTexture{
  AVFrame* av_frame=nullptr;
  GLuint textures[2]={0,0};
  bool has_valid_image=false;
};

struct YUV420PSwFrameTexture{
  // Since we copy the data, we do not need to keep the av frame around
  //AVFrame* av_frame=nullptr;
  GLuint textures[3]={0,0,0};
  bool has_valid_image=false;
};


class XAVFrameHolder{
 public:
  XAVFrameHolder(AVFrame* f):frame(f){};
  ~XAVFrameHolder(){
	//av_frame_free(f)
  };
  AVFrame* frame;
};

// Use " export DISPLAY=:0 " for ssh
// Needs to be run with X server running (at least for now), otherwise glfw cannot create a OpenGL window.
class EGLOut {
 public:
  EGLOut(int width,int height);
  ~EGLOut();
  /**
   * Display this frame via egl / OpenGL render.
   * This does not directly render the frame, but rather pushes it onto a queue
   * where it is then picked up by the render thread.
	*/
  int queue_new_frame_for_display(struct AVFrame * src_frame);
 private:
  void initializeWindowRender();
  void render_once();
  void render_thread_run();
 private:
  std::unique_ptr<std::thread> render_thread;
  bool terminate=false;
  const int window_width;
  const int window_height;
  //
  GLFWwindow* window= nullptr;
  // always called with the OpenGL context bound.
  void update_texture(AVFrame* frame);
  // allows frame drops (higher video fps than display refresh).
  std::unique_ptr<ThreadsafeQueue<XAVFrameHolder>> queue=std::make_unique<ThreadsafeQueue<XAVFrameHolder>>();
  EGLFrameTexture egl_frame_texture{};
  // Holds shaders for common video formats / upload techniques
  // Needs to be initialized on the GL thread.
  std::unique_ptr<GL_shader> gl_shader=nullptr;
  //
  std::unique_ptr<CUDAGLInteropHelper> m_cuda_gl_interop_helper=nullptr;
  void update_texture_cuda(AVFrame* frame);
  void update_texture_yuv420p(AVFrame* frame);
  // Blue RGB(A) texture, for testing. Uploaded once, then never modified.
  GLuint texture_rgb_blue=0;
  //
  CUDAFrameTexture cuda_frametexture{};
  YUV420PSwFrameTexture yuv_420_p_sw_frame_texture{};
  std::unique_ptr<Chronometer> frame_delta_chrono=nullptr;
  // Here we measure the time the CPU spends to build the command buffer
  // (including any conversions when they are done in the GL loop).
  // Does NOT include GL rendering time !
  Chronometer cpu_frame_time{"CPU frame time"};
};

#endif //HELLO_DRMPRIME_HELLO_DRMPRIME_EGL_OUT_H_
