/*
 * Copyright (c) 2020 John Cox for Raspberry Pi Trading
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */


// *** This module is a work in progress and its utility is strictly
//     limited to testing.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <pthread.h>
#include <semaphore.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

extern "C" {
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_drm.h"
#include "libavutil/pixdesc.h"
}
#include "common_consti/TimeHelper.hpp"
#include "common_consti/Logger.hpp"
#include "common_consti/ThreadsafeQueue.hpp"

static int CALCULATOR_LOG_INTERVAL=10;
AvgCalculator avgDisplayThreadQueueLatency{"DisplayThreadQueue"};
AvgCalculator avgTotalDecodeAndDisplayLatency{"TotalDecodeDisplayLatency"};
Chronometer chronoVsync{"VSYNC"};
Chronometer chronometerDaUninit{"DA_UNINIT"};
Chronometer chronometer2{"X2"};
Chronometer chronometer3{"X3"};
Chronometer chronometerDaInit{"DA_INIT"};
static bool DROP_FRAMES= false;

#define TRACE_ALL 0

#define DRM_MODULE "vc4"

#define ERRSTR strerror(errno)

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


class AVFrameHolder{
public:
    AVFrameHolder(AVFrame* f):frame(f){};
    ~AVFrameHolder(){
        //av_frame_free(f)
    };
    AVFrame* frame;
};

// Aux size should only need to be 2, but on a few streams (Hobbit) under FKMS
// we get initial flicker probably due to dodgy drm timing
//#define AUX_SIZE 3
#define AUX_SIZE 5
typedef struct drmprime_out_env_s{
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
    drm_aux_t extraAux;
} drmprime_out_env_t;


static int find_plane(const int drmfd, const int crtcidx, const uint32_t format,
                      uint32_t *const pplane_id){
    drmModePlaneResPtr planes;
    drmModePlanePtr plane;
    unsigned int i;
    unsigned int j;
    int ret = 0;
    planes = drmModeGetPlaneResources(drmfd);
    if (!planes) {
        fprintf(stderr, "drmModeGetPlaneResources failed: %s\n", ERRSTR);
        return -1;
    }
    for (i = 0; i < planes->count_planes; ++i) {
        plane = drmModeGetPlane(drmfd, planes->planes[i]);
        if (!planes) {
            fprintf(stderr, "drmModeGetPlane failed: %s\n", ERRSTR);
            break;
        }
        if (!(plane->possible_crtcs & (1 << crtcidx))) {
            drmModeFreePlane(plane);
            continue;
        }
        for (j = 0; j < plane->count_formats; ++j) {
            if (plane->formats[j] == format) break;
        }
        if (j == plane->count_formats) {
            drmModeFreePlane(plane);
            continue;
        }
        *pplane_id = plane->plane_id;
        drmModeFreePlane(plane);
        break;
    }
    if (i == planes->count_planes) ret = -1;
    drmModeFreePlaneResources(planes);
    return ret;
}

// ? clears up the drm_aux_t to be reused with a new frame
static void da_uninit(drmprime_out_env_t *const de, drm_aux_t *da){
    chronometerDaUninit.start();
    unsigned int i;
    if (da->fb_handle != 0) {
        drmModeRmFB(de->drm_fd, da->fb_handle);
        da->fb_handle = 0;
    }
    for (i = 0; i != AV_DRM_MAX_PLANES; ++i) {
        if (da->bo_handles[i]) {
            struct drm_gem_close gem_close = {.handle = da->bo_handles[i]};
            drmIoctl(de->drm_fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
            da->bo_handles[i] = 0;
        }
    }
    av_frame_free(&da->frame);
    chronometerDaUninit.stop();
    chronometerDaUninit.printInIntervals(CALCULATOR_LOG_INTERVAL);
}

#define Xmemclear(s) memset(&s, 0, sizeof(s))

static inline int XDRM_IOCTL(int fd, unsigned long cmd, void *arg)
{
    int ret = drmIoctl(fd, cmd, arg);
    return ret < 0 ? -errno : ret;
}

static int XdrmModeSetPlane(int fd, uint32_t plane_id, uint32_t crtc_id,
                    uint32_t fb_id, uint32_t flags,
                    int32_t crtc_x, int32_t crtc_y,
                    uint32_t crtc_w, uint32_t crtc_h,
                    uint32_t src_x, uint32_t src_y,
                    uint32_t src_w, uint32_t src_h)
{
    struct drm_mode_set_plane s;
    Xmemclear(s);
    s.plane_id = plane_id;
    s.crtc_id = crtc_id;
    s.fb_id = fb_id;
    s.flags = flags;
    s.crtc_x = crtc_x;
    s.crtc_y = crtc_y;
    s.crtc_w = crtc_w;
    s.crtc_h = crtc_h;
    s.src_x = src_x;
    s.src_y = src_y;
    s.src_w = src_w;
    s.src_h = src_h;
    return XDRM_IOCTL(fd, DRM_IOCTL_MODE_SETPLANE, &s);
}
static void modeset_page_flip_event(int fd, unsigned int frame,unsigned int sec, unsigned int usec,void *data){
    MLOGD<<"Got modeset_page_flip_event for frame "<<frame<<"\n";
}

static void registerModesetPageFlipEvent(drmprime_out_env_t *const de){
    drmEventContext ev;
    memset(&ev, 0, sizeof(ev));
    ev.version = 2;
    ev.page_flip_handler = modeset_page_flip_event;
    drmHandleEvent(de->drm_fd, &ev);
}

static int countLol=0;

// initializes the drm_aux_to for the new frame, including the raw frame data
// unfortunately blocks until the ? VSYNC or some VSYNC related time point ?
static int da_init(drmprime_out_env_t *const de, drm_aux_t *da,AVFrame* frame){
    chronometerDaInit.start();
    chronometer2.start();
    const AVDRMFrameDescriptor *desc = (AVDRMFrameDescriptor *)frame->data[0];
    uint32_t pitches[4] = { 0 };
    uint32_t offsets[4] = { 0 };
    uint64_t modifiers[4] = { 0 };
    uint32_t bo_handles[4] = { 0 };
    da->frame = frame;
    memset(da->bo_handles, 0, sizeof(da->bo_handles));
    //for (int i = 0; i < desc->nb_objects; ++i) {
    if (drmPrimeFDToHandle(de->drm_fd, desc->objects[0].fd, da->bo_handles) != 0) {
        fprintf(stderr, "drmPrimeFDToHandle[%d](%d) failed: %s\n", 0, desc->objects[0].fd, ERRSTR);
        return -1;
    }
    //}
    int n = 0;
    //for (int i = 0; i < desc->nb_layers; ++i) {
    for (int j = 0; j < desc->layers[0].nb_planes; ++j) {
        const AVDRMPlaneDescriptor *const p = desc->layers[0].planes + j;
        const AVDRMObjectDescriptor *const obj = desc->objects + p->object_index;
        MLOGD<<"Plane "<<j<<" has pitch:"<<p->pitch<<" offset:"<<p->offset<<" object_index:"<<p->object_index<<"\n";
        pitches[n] = p->pitch;
        offsets[n] = p->offset;
        modifiers[n] = obj->format_modifier;
        bo_handles[n] = da->bo_handles[p->object_index];
        ++n;
    }
    //}
    MLOGD<<"desc->nb_objects:"<<desc->nb_objects<<"desc->nb_layers"<<desc->nb_layers<<"\n";
    if (drmModeAddFB2WithModifiers(de->drm_fd,
                                   av_frame_cropped_width(frame),
                                   av_frame_cropped_height(frame),
                                   desc->layers[0].format, bo_handles,
                                   pitches, offsets, modifiers,
                                   &da->fb_handle, DRM_MODE_FB_MODIFIERS /** 0 if no mods */) != 0) {
        fprintf(stderr, "drmModeAddFB2WithModifiers failed: %s\n", ERRSTR);
        return -1;
    }
    chronometer2.stop();
    chronometer2.printInIntervals(CALCULATOR_LOG_INTERVAL);

    //countLol++;
    if(countLol>20){
        fprintf(stderr,"de->setup.crtcId: %d da->fb_handle: %d",de->setup.crtcId,da->fb_handle);
        // https://github.com/raspberrypi/linux/blob/aeaa2460db088fb2c97ae56dec6d7d0058c68294/drivers/gpu/drm/drm_ioctl.c#L686
        // https://github.com/raspberrypi/linux/blob/rpi-5.10.y/drivers/gpu/drm/drm_plane.c#L1044
        //DRM_MODE_PAGE_FLIP_EVENT
        if(drmModePageFlip(de->drm_fd,de->setup.crtcId,da->fb_handle,0,de)!=0){
            fprintf(stderr, "drmModePageFlip failed: %s %d\n", ERRSTR, errno);
            return -1;
        }else{
            fprintf(stderr, "drmModePageFlip success\n");
        }
    }else{
        // https://github.com/grate-driver/libdrm/blob/master/xf86drmMode.c#L988
        // https://github.com/raspberrypi/linux/blob/aeaa2460db088fb2c97ae56dec6d7d0058c68294/drivers/gpu/drm/drm_ioctl.c#L670
        // https://github.com/raspberrypi/linux/blob/rpi-5.10.y/drivers/gpu/drm/drm_plane.c#L800
        // https://github.com/raspberrypi/linux/blob/rpi-5.10.y/drivers/gpu/drm/drm_plane.c#L771
        if(drmModeSetPlane(de->drm_fd, de->setup.planeId, de->setup.crtcId,
                           da->fb_handle, DRM_MODE_PAGE_FLIP_ASYNC | DRM_MODE_ATOMIC_NONBLOCK,
                           de->setup.compose.x, de->setup.compose.y,
                           de->setup.compose.width,
                           de->setup.compose.height,
                           0, 0,
                           av_frame_cropped_width(frame) << 16,
                           av_frame_cropped_height(frame) << 16)!=0){
            fprintf(stderr, "drmModeSetPlane failed: %s\n", ERRSTR);
            return -1;
        }
    }
    chronometerDaInit.stop();
    chronometerDaInit.printInIntervals(CALCULATOR_LOG_INTERVAL);
    return 0;
}


// If the crtc plane we have for video is not updated to use the same frame format (yet),
// do so. Only needs to be done once.
static int updateCRTCFormatIfNeeded(drmprime_out_env_t *const de, AVFrame *frame){
    const AVDRMFrameDescriptor *desc = (AVDRMFrameDescriptor *)frame->data[0];
    const uint32_t format = desc->layers[0].format;
    if (de->setup.out_fourcc != format) {
        if (find_plane(de->drm_fd, de->setup.crtcIdx, format, &de->setup.planeId)) {
            av_frame_free(&frame);
            fprintf(stderr, "No plane for format: %#x\n", format);
            return -1;
        }
        de->setup.out_fourcc = format;
        printf("Changed drm_setup(aka CRTC) format to %#x\n",de->setup.out_fourcc);
    }
    return 0;
}
// This was in the original code, but it won't have an effect anyways since swapping the fb aparently does VSYNC anyways nowadays
static void waitForVSYNC(drmprime_out_env_t *const de){
    chronoVsync.start();
    drmVBlank vbl = {
            .request = {
                    // Consti10: I think to get the next VBLANK, we need to set sequence to 1
                    // https://docs.nvidia.com/jetson/l4t-graphics/group__direct__rendering__manager.html#gadc9d79f4d0195e60d0f8c9665da5d8b2
                    .type = DRM_VBLANK_RELATIVE,
                    //.sequence = 0
                    .sequence = 1
            }
    };
    /*while (drmWaitVBlank(de->drm_fd, &vbl)) {
        if (errno != EINTR) {
            // This always fails - don't know why
            //fprintf(stderr, "drmWaitVBlank failed: %s\n", ERRSTR);
            break;
        }
    }*/
    const int ret=drmWaitVBlank(de->drm_fd,&vbl);
    if(ret!=0){
        fprintf(stderr, "drmWaitVBlank failed:%d  %s\n",ret, ERRSTR);
    }
    chronoVsync.stop();
    chronoVsync.printInIntervals(CALCULATOR_LOG_INTERVAL);
}

//static void consti10_copy_into_curr_fb(drmprime_out_env_t *const de,AVFrame* frame,drm_aux_t *da){
//}
/*static void consti10_page_flip(drmprime_out_env_t *const de,drm_aux_t *da,AVFrame *frame){
    if (drmPrimeFDToHandle(de->drm_fd, desc->objects[0].fd, da->bo_handles) != 0) {
        fprintf(stderr, "drmPrimeFDToHandle[%d](%d) failed: %s\n", 0, desc->objects[0].fd, ERRSTR);
        return -1;
    }

    drmModePageFlip(de->drm_fd,de->setup.crtcId,da->fb_handle,DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_PAGE_FLIP_ASYNC,de);
}*/

static bool first=true;
static int do_display(drmprime_out_env_t *const de, AVFrame *frame)
{
    assert(frame!=NULL);
    avgDisplayThreadQueueLatency.addUs(getTimeUs()-frame->pts);
    avgDisplayThreadQueueLatency.printInIntervals(CALCULATOR_LOG_INTERVAL);
    drm_aux_t *da = de->aux + de->ano;
    if(updateCRTCFormatIfNeeded(de,frame)!=0){
        return -1;
    }
    //registerModesetPageFlipEvent(de);
    /*if(first){
        da_uninit(de, da);
        //
        da_init(de,da,frame);
        first=false;
    }else{
        da_init(de,de->extraAux,frame);
        sleep(1);
    }*/
    // Not needed / doesn't have the desired effect anyways
    //waitForVSYNC(de);
    //
    da_uninit(de, da);
    //
    da_init(de,da,frame);
    // use another de aux for the next frame
    de->ano = de->ano + 1 >= AUX_SIZE ? 0 : de->ano + 1;
    avgTotalDecodeAndDisplayLatency.addUs(getTimeUs()- frame->pts);
    avgTotalDecodeAndDisplayLatency.printInIntervals(CALCULATOR_LOG_INTERVAL);
    return 0;
}

static void* display_thread(void *v)
{
    drmprime_out_env_t *const de = (drmprime_out_env_t *)v;
    int i;

#if TRACE_ALL
    fprintf(stderr, "<<< %s\n", __func__);
#endif
    for (;;) {
        if(de->terminate)break;
        if(DROP_FRAMES){
            // wait until we are close to VSYNC ?!
            waitForVSYNC(de);
            const auto allBuffers=de->queue->getAllAndClear();
            if(allBuffers.size()>0){
                const int nDroppedFrames=allBuffers.size()-1;
                MLOGD<<"N dropped:"<<nDroppedFrames<<"\n";
                // don't forget to free the dropped frames
                for(int i=0;i<nDroppedFrames;i++){
                    av_frame_free(&allBuffers[i]->frame);
                }
                const auto mostRecent=allBuffers[nDroppedFrames];
                do_display(de, mostRecent->frame);
            }
        }else{
            AVFrame* frame=de->sbQueue->getBuffer();
            if(frame==NULL){
                MLOGD<<"Got NULL frame\n";
                break;
            }else{
                //MLOGD<<"Got frame\n";
            }
            do_display(de, frame);
        }
    }
#if TRACE_ALL
    fprintf(stderr, ">>> %s\n", __func__);
#endif
    for (i = 0; i != AUX_SIZE; ++i)
        da_uninit(de, de->aux + i);
    return NULL;
}

static int find_crtc(int drmfd, struct drm_setup *s, uint32_t *const pConId)
{
    int ret = -1;
    int i;
    drmModeRes *res = drmModeGetResources(drmfd);
    drmModeConnector *c;
    if (!res) {
        printf("drmModeGetResources failed: %s\n", ERRSTR);
        return -1;
    }
    if (res->count_crtcs <= 0) {
        printf("drm: no crts\n");
        goto fail_res;
    }
    if (!s->conId) {
        fprintf(stderr,
                "No connector ID specified.  Choosing default from list:\n");
        for (i = 0; i < res->count_connectors; i++) {
            drmModeConnector *con =
                drmModeGetConnector(drmfd, res->connectors[i]);
            drmModeEncoder *enc = NULL;
            drmModeCrtc *crtc = NULL;
            if (con->encoder_id) {
                enc = drmModeGetEncoder(drmfd, con->encoder_id);
                if (enc->crtc_id) {
                    crtc = drmModeGetCrtc(drmfd, enc->crtc_id);
                }
            }
            if (!s->conId && crtc) {
                s->conId = con->connector_id;
                s->crtcId = crtc->crtc_id;
            }
            fprintf(stderr, "Connector %d (crtc %d): type %d, %dx%d%s\n",
                   con->connector_id,
                   crtc ? crtc->crtc_id : 0,
                   con->connector_type,
                   crtc ? crtc->width : 0,
                   crtc ? crtc->height : 0,
                   (s->conId == (int)con->connector_id ?
                    " (chosen)" : ""));
        }
        if (!s->conId) {
            fprintf(stderr,
                   "No suitable enabled connector found.\n");
            return -1;;
        }
    }
    s->crtcIdx = -1;
    for (i = 0; i < res->count_crtcs; ++i) {
        if (s->crtcId == res->crtcs[i]) {
            s->crtcIdx = i;
            break;
        }
    }
    if (s->crtcIdx == -1) {
        fprintf(stderr, "drm: CRTC %u not found\n", s->crtcId);
        goto fail_res;
    }
    if (res->count_connectors <= 0) {
        fprintf(stderr, "drm: no connectors\n");
        goto fail_res;
    }
    c = drmModeGetConnector(drmfd, s->conId);
    if (!c) {
        fprintf(stderr, "drmModeGetConnector failed: %s\n", ERRSTR);
        goto fail_res;
    }
    if (!c->count_modes) {
        fprintf(stderr, "connector supports no mode\n");
        goto fail_conn;
    }
    {
        drmModeCrtc *crtc = drmModeGetCrtc(drmfd, s->crtcId);
        s->compose.x = crtc->x;
        s->compose.y = crtc->y;
        s->compose.width = crtc->width;
        s->compose.height = crtc->height;
        drmModeFreeCrtc(crtc);
    }
    if (pConId) *pConId = c->connector_id;
    ret = 0;
fail_conn:
    drmModeFreeConnector(c);
fail_res:
    drmModeFreeResources(res);
    return ret;
}

int drmprime_out_display(drmprime_out_env_t *de, struct AVFrame *src_frame)
{
    AVFrame *frame;
    int ret;
    if ((src_frame->flags & AV_FRAME_FLAG_CORRUPT) != 0) {
        fprintf(stderr, "Discard corrupt frame: fmt=%d, ts=%" PRId64 "\n", src_frame->format, src_frame->pts);
        return 0;
    }
    if (src_frame->format == AV_PIX_FMT_DRM_PRIME) {
        frame = av_frame_alloc();
        av_frame_ref(frame, src_frame);
        //printf("format == AV_PIX_FMT_DRM_PRIME\n");
    } else if (src_frame->format == AV_PIX_FMT_VAAPI) {
        //printf("format == AV_PIX_FMT_VAAPI\n");
        frame = av_frame_alloc();
        frame->format = AV_PIX_FMT_DRM_PRIME;
        if (av_hwframe_map(frame, src_frame, 0) != 0) {
            fprintf(stderr, "Failed to map frame (format=%d) to DRM_PRiME\n", src_frame->format);
            av_frame_free(&frame);
            return AVERROR(EINVAL);
        }
    } else {
        fprintf(stderr, "Frame (format=%d) not DRM_PRiME\n", src_frame->format);
        return AVERROR(EINVAL);
    }
    // Here the delay is still neglegible,aka ~0.15ms
    const auto delayBeforeDisplayQueueUs=getTimeUs()-frame->pts;
    MLOGD<<"delayBeforeDisplayQueue:"<<frame->pts<<" delay:"<<(delayBeforeDisplayQueueUs/1000.0)<<" ms\n";
    if(DROP_FRAMES){
        // push it immediately, even though frame(s) might already be inside the queue
        de->queue->push(std::make_shared<AVFrameHolder>(frame));
    }else{
        // wait for the last buffer to be processed, then update
        de->sbQueue->setBuffer(frame);
    }
    return 0;
}

void drmprime_out_delete(drmprime_out_env_t *de)
{
    de->terminate=true;
    de->sbQueue->terminate();
    pthread_join(de->q_thread, NULL);
    // free any frames that might be inside some queues - since the queue thread
    // is now stopped, we don't have to worry about any synchronization
    if(de->sbQueue->unsafeGetFrame()!=NULL){
        AVFrame* frame=de->sbQueue->unsafeGetFrame();
        av_frame_free(&frame);
    }
    auto tmp=de->queue->getAllAndClear();
    for(int i=0;i<tmp.size();i++){
        auto element=tmp[i];
        av_frame_free(&element->frame);
    }
    if (de->drm_fd >= 0) {
        close(de->drm_fd);
        de->drm_fd = -1;
    }
    de->sbQueue.reset();
    de->queue.reset();
    free(de);
}

drmprime_out_env_t* drmprime_out_new(bool dropFrames)
{
    int rv;
    drmprime_out_env_t* const de = (drmprime_out_env_t*)calloc(1, sizeof(*de));
    if (de == NULL)
        return NULL;

    de->sbQueue=std::make_unique<ThreadsafeSingleBuffer<AVFrame*>>();
    de->queue=std::make_unique<ThreadsafeQueue<AVFrameHolder>>();
    DROP_FRAMES=dropFrames;

    const char *drm_module = DRM_MODULE;

    de->drm_fd = -1;
    de->con_id = 0;
    de->setup = (struct drm_setup) { 0 };
    de->terminate=false;

    if ((de->drm_fd = drmOpen(drm_module, NULL)) < 0) {
        rv = AVERROR(errno);
        // comp error fprintf(stderr, "Failed to drmOpen %s: %s\n", drm_module, av_err2str(rv));
        fprintf(stderr, "Failed to drmOpen %s: \n", drm_module);
        goto fail_free;
    }

    if (find_crtc(de->drm_fd, &de->setup, &de->con_id) != 0) {
        fprintf(stderr, "failed to find valid mode\n");
        rv = AVERROR(EINVAL);
        goto fail_close;
    }

    if (pthread_create(&de->q_thread, NULL, display_thread, de)) {
        rv = AVERROR(errno);
        //comp error fprintf(stderr, "Failed to create display thread: %s\n", av_err2str(rv));
        fprintf(stderr, "Failed to create display thread:\n");
        goto fail_close;
    }

    return de;

fail_close:
    close(de->drm_fd);
    de->drm_fd = -1;
fail_free:
    free(de);
    fprintf(stderr, ">>> %s: FAIL\n", __func__);
    return NULL;
}

