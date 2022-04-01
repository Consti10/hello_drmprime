#ifndef CONSTI10_DRMPRIME_OUT
#define CONSTI10_DRMPRIME_OUT

struct AVFrame;

#include <memory>
#include <stdio.h>
#include <stdlib.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "common_consti/ThreadsafeQueue.hpp"

class AVFrameHolder{
public:
    AVFrameHolder(AVFrame* f):frame(f){};
    ~AVFrameHolder(){
        //av_frame_free(f)
    };
    AVFrame* frame;
};

class DRMPrimeOut{
public:
    DRMPrimeOut(int renderMode);
    ~DRMPrimeOut();
    int drmprime_out_display(struct AVFrame * frame);
    // Aux size should only need to be 2, but on a few streams (Hobbit) under FKMS
    // we get initial flicker probably due to dodgy drm timing
    static constexpr auto AUX_SIZE=5;
    // --------
    struct drm_setup{
        int conId;
        uint32_t crtcId;
        int crtcIdx;
        uint32_t planeId;
        unsigned int out_fourcc;
        struct{
            int x, y, width, height;
        } compose;
    };
    typedef struct drm_aux_s{
        unsigned int fb_handle;
        // buffer out handles - set to the drm prime handles of the frame
        uint32_t bo_handles[AV_DRM_MAX_PLANES];
        AVFrame *frame;
    } drm_aux_t;
    // --------
    int drm_fd;
    uint32_t con_id;
    struct drm_setup setup;
    enum AVPixelFormat avfmt;
    // multiple (frame buffer?) objects such that we can create a new one without worrying about the last one still in use.
    unsigned int ano;
    drm_aux_t aux[AUX_SIZE];
    // the thread hat handles the drm display update, decoupled from decoder thread
    pthread_t q_thread;
    bool terminate=false;
    // used when frame drops are not wanted, aka how the original implementation was done
    std::unique_ptr<ThreadsafeSingleBuffer<AVFrame*>> sbQueue;
    // allows frame drops (higher video fps than display refresh).
    std::unique_ptr<ThreadsafeQueue<AVFrameHolder>> queue;
    // extra
    //drm_aux_t extraAux;
};

static int CALCULATOR_LOG_INTERVAL=10;

#endif //CONSTI10_DRMPRIME_OUT