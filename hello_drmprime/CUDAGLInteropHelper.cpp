//
// Created by consti10 on 06.09.22.
//

#include "CUDAGLInteropHelper.h"
#include <SDL.h>

extern "C" {
#include <GLFW/glfw3.h>
}

#include <cstring>
#include <iostream>

CUDAGLInteropHelper::CUDAGLInteropHelper(AVCUDADeviceContext* context)
    : m_Funcs(nullptr),
      //m_Context((AVCUDADeviceContext*)context->hwctx)
	  m_Context(context)
{
    memset(m_Resources, 0, sizeof(m_Resources));

    // One-time init of CUDA library
    cuda_load_functions(&m_Funcs, nullptr);
    if (m_Funcs == nullptr) {
        std::cerr<<"Failed to initialize CUDA library\n";
        return;
    }
	std::cout<<"Successfully initialized CUDAGLInteropHelper\n";
}

CUDAGLInteropHelper::~CUDAGLInteropHelper()
{
    unregisterTextures();

    if (m_Funcs != nullptr) {
        cuda_free_functions(&m_Funcs);
    }
}

//bool CUDAGLInteropHelper::registerBoundTextures()
bool CUDAGLInteropHelper::registerTextures(int tex1,int tex2)
{
    int err;

    if (m_Funcs == nullptr) {
        // Already logged in constructor
        return false;
    }

    // Push FFmpeg's CUDA context to use for our CUDA operations
    err = m_Funcs->cuCtxPushCurrent(m_Context->cuda_ctx);
    if (err != CUDA_SUCCESS) {
        fprintf(stderr, "cuCtxPushCurrent() failed: %d\n", err);
        return false;
    }

    // Register each plane as a separate resource
    for (int i = 0; i < NV12_PLANES; i++) {
        GLint tex=i==0 ? tex1:tex2;

        // Get the ID of this plane's texture
        //glActiveTexture(GL_TEXTURE0 + i);
	    //glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);

        // Register it with CUDA
        err = m_Funcs->cuGraphicsGLRegisterImage(&m_Resources[i], tex, GL_TEXTURE_2D, CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD);
        if (err != CUDA_SUCCESS) {
		    fprintf(stderr,"cuGraphicsGLRegisterImage() failed: %d %d\n",i, err);
            m_Resources[i] = 0;
            unregisterTextures();
            goto Exit;
        }
    }

Exit:
    {
        CUcontext dummy;
        m_Funcs->cuCtxPopCurrent(&dummy);
    }
    return err == CUDA_SUCCESS;
}

void CUDAGLInteropHelper::unregisterTextures()
{
    int err;

    if (m_Funcs == nullptr) {
        // Already logged in constructor
        return;
    }

    // Push FFmpeg's CUDA context to use for our CUDA operations
    err = m_Funcs->cuCtxPushCurrent(m_Context->cuda_ctx);
    if (err != CUDA_SUCCESS) {
	    fprintf(stderr,"cuCtxPushCurrent() failed: %d", err);
        return;
    }

    for (int i = 0; i < NV12_PLANES; i++) {
        if (m_Resources[i] != 0) {
            m_Funcs->cuGraphicsUnregisterResource(m_Resources[i]);
            m_Resources[i] = 0;
        }
    }

    {
        CUcontext dummy;
        m_Funcs->cuCtxPopCurrent(&dummy);
    }
}

bool CUDAGLInteropHelper::copyCudaFrameToTextures(AVFrame* frame)
{
    int err;

    if (m_Funcs == nullptr) {
        // Already logged in constructor
        return false;
    }

    // Push FFmpeg's CUDA context to use for our CUDA operations
    err = m_Funcs->cuCtxPushCurrent(m_Context->cuda_ctx);
    if (err != CUDA_SUCCESS) {
	    fprintf(stderr, "cuCtxPushCurrent() failed: %d", err);
        return false;
    }

    // Map our resources
    err = m_Funcs->cuGraphicsMapResources(NV12_PLANES, m_Resources, m_Context->stream);
    if (err != CUDA_SUCCESS) {
	    fprintf(stderr,"cuGraphicsMapResources() failed: %d\n", err);
        goto PopCtxExit;
    }

    for (int i = 0; i < NV12_PLANES; i++) {
        CUarray cudaArray;

        // Get a pointer to the mapped array for this plane
        err = m_Funcs->cuGraphicsSubResourceGetMappedArray(&cudaArray, m_Resources[i], 0, 0);
        if (err != CUDA_SUCCESS) {
		    fprintf(stderr,"cuGraphicsSubResourceGetMappedArray() failed: %d", err);
            goto UnmapExit;
        }

        // Do the copy
        CUDA_MEMCPY2D cu2d = {
            .srcMemoryType = CU_MEMORYTYPE_DEVICE,
            .srcDevice = (CUdeviceptr)frame->data[i],
            .srcPitch = (size_t)frame->linesize[i],
            .dstMemoryType = CU_MEMORYTYPE_ARRAY,
            .dstArray = cudaArray,
            .dstPitch = (size_t)frame->width >> i,
            .WidthInBytes = (size_t)frame->width,
            .Height = (size_t)frame->height >> i
        };
        err = m_Funcs->cuMemcpy2D(&cu2d);
        if (err != CUDA_SUCCESS) {
		    fprintf(stderr, "cuMemcpy2D() failed: %d", err);
            goto UnmapExit;
        }
    }

UnmapExit:
    m_Funcs->cuGraphicsUnmapResources(NV12_PLANES, m_Resources, m_Context->stream);
PopCtxExit:
    {
        CUcontext dummy;
        m_Funcs->cuCtxPopCurrent(&dummy);
    }
    return err == CUDA_SUCCESS;
}