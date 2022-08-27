#ifndef CONSTI10_DRMPRIME_OUT
#define CONSTI10_DRMPRIME_OUT

struct AVFrame;

#include <memory>
#include <stdio.h>
#include <stdlib.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

extern "C" {
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_drm.h"
#include "libavutil/pixdesc.h"
}

#include "../common_consti/ThreadsafeQueue.hpp"
#include "MMapFrame.h"
#include <memory>

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
    explicit DRMPrimeOut(int renderMode);
    ~DRMPrimeOut();
    /**
     * Display this frame via drm prime
     * @param frame the frame to display
     */
    int drmprime_out_display(struct AVFrame * frame);

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
    struct drm_aux{
        unsigned int fb_handle;
        // buffer out handles - set to the drm prime handles of the frame
        uint32_t bo_handles[AV_DRM_MAX_PLANES];
        AVFrame *frame;
        std::unique_ptr<MMapFrame> mappedFrame;
    };
    // --------
    int drm_fd;
    uint32_t con_id;
    struct drm_setup setup{};
    enum AVPixelFormat avfmt;
    // multiple (frame buffer?) objects such that we can create a new one without worrying about the last one still in use.
    unsigned int ano=0;
    // Aux size should only need to be 2, but on a few streams (Hobbit) under FKMS
    // we get initial flicker probably due to dodgy drm timing
    static constexpr auto AUX_SIZE=5;
    drm_aux aux[AUX_SIZE];
    // the thread hat handles the drm display update, decoupled from decoder thread
    pthread_t q_thread;
    bool terminate=false;
    // used when frame drops are not wanted, aka how the original implementation was done
    std::unique_ptr<ThreadsafeSingleBuffer<AVFrame*>> sbQueue;
    // allows frame drops (higher video fps than display refresh).
    std::unique_ptr<ThreadsafeQueue<AVFrameHolder>> queue;
    // extra
    //drm_aux_t extraAux;
    const int renderMode=0;
};

static int CALCULATOR_LOG_INTERVAL=10;

#endif //CONSTI10_DRMPRIME_OUT