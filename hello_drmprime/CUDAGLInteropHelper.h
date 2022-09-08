//
// Created by consti10 on 06.09.22.
//

#ifndef HELLO_DRMPRIME__CUDAGLINTEROPHELPER_H_
#define HELLO_DRMPRIME__CUDAGLINTEROPHELPER_H_

#include <ffnvcodec/dynlink_loader.h>

extern "C" {
#include <libavutil/hwcontext_cuda.h>
#include "libavutil/frame.h"
}

#define NV12_PLANES 2

// Helper class to CUDA memcpy a ffmpeg CUDA (decoded) image into a NV12 OpenGL Texture
// ( Actually, 2 textures with 1xY and 1xU,V interleaved, the NV12 to RGB conversion is then done in a custom OpenGL shader)
// NOTE: Has nothing to do with RPI ;)
class CUDAGLInteropHelper {
 public:
  explicit CUDAGLInteropHelper(AVCUDADeviceContext* context);
  ~CUDAGLInteropHelper();

  bool registerTextures(int tex1,int tex2);
  void unregisterTextures();

  bool copyCudaFrameToTextures(AVFrame* frame);
 private:
  CudaFunctions* m_Funcs;
  AVCUDADeviceContext* m_Context;
  CUgraphicsResource m_Resources[NV12_PLANES];
};

#endif //HELLO_DRMPRIME__CUDAGLINTEROPHELPER_H_
