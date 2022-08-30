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
#include "include/LEDSwap.h"

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


class EGLOut {
 public:
 private:
};

#endif //HELLO_DRMPRIME_HELLO_DRMPRIME_EGL_OUT_H_
