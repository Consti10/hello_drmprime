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
#include <libavutil/imgutils.h>
//
#include "libavutil/hwcontext_drm.h"
}

#include <thread>
#include <memory>
#include "../common_consti/ThreadsafeQueue.hpp"

class XAVFrameHolder{
 public:
  XAVFrameHolder(AVFrame* f):frame(f){};
  ~XAVFrameHolder(){
	//av_frame_free(f)
  };
  AVFrame* frame;
};

class EGLOut {
 public:
  EGLOut(int width,int height):window_width(width),window_height(height){
	render_thread=std::make_unique<std::thread>([this](){
	  initializeWindowRender();
	  while (!glfwWindowShouldClose(window)){
		glfwPollEvents();  /// for mouse window closing
		std::this_thread::sleep_for(std::chrono::seconds(1));
		render_once();
	  }
	  glDeleteBuffers(1, &vbo);
	  glfwTerminate();
	});
  }
  void initializeWindowRender();
  void render_once();
  /**
	* Display this frame via egl / OpenGL render
	*/
  int out_display(struct AVFrame * frame);
 private:
  std::unique_ptr<std::thread> render_thread;
  const int window_width;
  const int window_height;
  //
  GLuint shader_program;
  GLuint vbo;
  GLint pos;
  GLint uvs;
  GLFWwindow* window;
  //
  // allows frame drops (higher video fps than display refresh).
  std::unique_ptr<ThreadsafeQueue<XAVFrameHolder>> queue;
};

#endif //HELLO_DRMPRIME_HELLO_DRMPRIME_EGL_OUT_H_
