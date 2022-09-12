//
// Created by consti10 on 30.08.22.
//

#ifndef HELLO_DRMPRIME_HELLO_DRMPRIME_EGL_OUT_H_
#define HELLO_DRMPRIME_HELLO_DRMPRIME_EGL_OUT_H_

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
#include "GL_shaders.h"

//#define X_HAS_LIB_CUDA
#ifdef X_HAS_LIB_CUDA
#include "CUDAGLInteropHelper.h"
#endif
// XXX
//#include <SDL.h>
#define X_USE_SDL
#ifdef X_USE_SDL
#include <SDL2/SDL.h>
#endif

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
  // Since we memcpy with cuda, we do not need to keep the av_frame around
  //AVFrame* av_frame=nullptr;
  GLuint textures[2]={0,0};
  bool has_valid_image=false;
};

struct YUV420PSwFrameTexture{
  // Since we copy the data, we do not need to keep the av frame around
  //AVFrame* av_frame=nullptr;
  GLuint textures[3]={0,0,0};
  bool has_valid_image=false;
  // Allows us t use glTextSubImaage instead
  int last_width=-1;
  int last_height=-1;
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
// Nice tool: https://www.vsynctester.com/https
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
  // Set to true when the window and OpenGL context creation is finished (we can accept frames)
  bool render_ready=false;
  void wait_until_ready(){
	while (!render_ready) {
	  std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
  }
 private:
  void initializeWindowRenderGlfw();
  void initializeWindowRendererSDL();
  // setup everyting that needs to be done once, but with gl context bound
  void setup_gl();
  void render_once();
  void render_thread_run();
 private:
  std::unique_ptr<std::thread> render_thread;
  bool terminate=false;
  int window_width;
  int window_height;
  //
  GLFWwindow* window= nullptr;
  //
  SDL_Window* win;
  SDL_Renderer* rend;
  // always called with the OpenGL context bound.
  void update_texture(AVFrame* frame);
  // allows frame drops (higher video fps than display refresh).
  //std::unique_ptr<ThreadsafeQueue<XAVFrameHolder>> queue=std::make_unique<ThreadsafeQueue<XAVFrameHolder>>();
  // Holds shaders for common video formats / upload techniques
  // Needs to be initialized on the GL thread.
  std::unique_ptr<GL_shaders> gl_shaders=nullptr;
  //
#ifdef X_HAS_LIB_CUDA
  std::unique_ptr<CUDAGLInteropHelper> m_cuda_gl_interop_helper=nullptr;
#endif
  void update_texture_cuda(AVFrame* frame);
  void update_texture_yuv420p(AVFrame* frame);
  void update_texture_vdpau(AVFrame* frame);
  // green/ blue RGB(A) textures, for testing. Uploaded once, then never modified.
  GLuint texture_rgb_green=0;
  GLuint texture_rgb_blue=0;
  int frameCount=0;
  //
  EGLFrameTexture egl_frame_texture{};
  CUDAFrameTexture cuda_frametexture{};
  YUV420PSwFrameTexture yuv_420_p_sw_frame_texture{};
  //
  // Time between frames (frame time)
  std::unique_ptr<Chronometer> frame_delta_chrono=nullptr;
  // Here we measure the time the CPU spends to build the command buffer
  // (including any conversions when they are done in the GL loop).
  // Does NOT include GL rendering time !
  Chronometer cpu_frame_time{"CPU frame time"};
  Chronometer cpu_swap_time{"CPU swap time"};
  Chronometer cpu_glclear_time{"CPU glClear time"};
  AvgCalculator avg_delay_before_display_queue{"Delay before display queue"};
  //
  Chronometer av_hframe_transfer_data{"AV HWFrame transfer data"};
  Chronometer update_frame_producer{"Update frame producer"};
 private:
  std::mutex latest_frame_mutex;
  AVFrame* m_latest_frame=nullptr;
 private:
  struct DisplayStats{
	int n_frames_rendered=0;
	int n_frames_dropped=0;
	// Delay between frame was given to the egl render <-> we uploaded it to the texture (if not dropped)
	//AvgCalculator delay_until_uploaded{"Delay until uploaded"};
	// Delay between frame was given to the egl renderer <-> swap operation returned (it is handed over to the hw composer)
	//AvgCalculator delay_until_swapped{"Delay until swapped"};
  };
  DisplayStats m_display_stats{};
  static std::string display_stats_to_string(const DisplayStats& display_stats){
	std::stringstream ss;
	ss<<"DisplayStats{rendered:"<<display_stats.n_frames_rendered<<" dropped:"<<display_stats.n_frames_dropped<<"}\n";
	return ss.str();
  }
};

#endif //HELLO_DRMPRIME_HELLO_DRMPRIME_EGL_OUT_H_
