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

// we support glfw or SDL for context creation
#define GLFW_INCLUDE_ES2
extern "C" {
#include <GLFW/glfw3.h>
#include "glhelp.h"
}
#include <SDL2/SDL.h>

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
#include "gl_shaders.h"

#include "gl_videorenderer.h"

//#define X_HAS_LIB_CUDA
#ifdef X_HAS_LIB_CUDA
#include "CUDAGLInteropHelper.h"
#endif

#define X_USE_SDL


// Use " export DISPLAY=:0 " for ssh
// Needs to be run with X server running (at least for now), otherwise glfw cannot create a OpenGL window.
// Nice tool: https://www.vsynctester.com/https
class EGLOut{
 public:
  EGLOut(int width,int height);
  ~EGLOut();
  /**
   * Display this frame via egl / OpenGL render.
   * This does not directly render the frame, but rather references it to be picked up
   * by the OpenGL thread. If you call this with a new frame before the previous frame
   * has been picked up by the OpenGL thread, the previous frame is dropped (freed) and the
   * OpenGL thread will pick up the given frame next time (unless you also override this one).
   * Thread - safe, and since we make sure to give up the mutex in the opengl thread as soon as
   * possible this call always returns immediately (unless the thread scheduler is overloaded / does something weird).
	*/
  int queue_new_frame_for_display(AVFrame * src_frame);
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
  // Fetch the latest, decoded frame. Thread-safe.
  // This method can return null (in this case no new frame is available)
  // or return the AVFrame* - in which case the caller is then responsible to free the frame
  AVFrame* fetch_latest_decoded_frame();
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
  std::unique_ptr<GL_VideoRenderer> gl_video_renderer=nullptr;
  int frameCount=0;
  //
  Chronometer cpu_update_texture{"CPU Update texture"};
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
