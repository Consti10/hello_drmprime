//
// Created by consti10 on 01.03.22.
//

#ifndef HELLO_DRMPRIME_EXTRA_DRM_H
#define HELLO_DRMPRIME_EXTRA_DRM_H

#include <xf86drm.h>
#include <xf86drmMode.h>

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


struct DumpBuffer{
    uint32_t width=1920;
    uint32_t height=1080;
    uint32_t stride;
    uint32_t size;
    uint32_t bo_handle;
    uint8_t *map;
    uint32_t fb;
    static void allocateAndMap(int fd,DumpBuffer* buf){
        int ret;
        // create dumb buffer
        struct drm_mode_create_dumb creq;
        memset(&creq, 0, sizeof(creq));
        creq.width = buf->width;
        creq.height = buf->height;
        creq.bpp = 32;
        ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
        if (ret < 0) {
            fprintf(stderr, "cannot create dumb buffer (%d): %m\n",errno);
            return;
        }
        buf->stride = creq.pitch;
        buf->size = creq.size;
        buf->bo_handle = creq.handle;
        // create framebuffer object for the dumb-buffer
        ret = drmModeAddFB(fd, buf->width, buf->height, 24, 32, buf->stride,buf-bo_handle, &buf->fb);
        if (ret) {
            fprintf(stderr, "cannot create framebuffer (%d): %m\n",errno);
            return;
        }
        // prepare buffer for memory mapping
        struct drm_mode_map_dumb mreq;
        memset(&mreq, 0, sizeof(mreq));
        mreq.handle = buf->bo_handle;
        ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
        if (ret) {
            fprintf(stderr, "cannot map dumb buffer (%d): %m\n",errno);
            return;
        }
        // perform actual memory mapping
        buf->map = (uint8_t*)mmap(0, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED,fd, mreq.offset);
        if (buf->map == MAP_FAILED) {
            fprintf(stderr, "cannot mmap dumb buffer (%d): %m\n",
                    errno);
            return;
        }
        // clear the framebuffer to 0
        memset(buf->map, 0, buf->size);
        fprintf(stderr,"Created dump buffer w:%d h:%d stride:%d size:%d\n",buf->width,buf->height,buf->stride,buf->size);
    }
    //
    static void unmapAndDelete(int fd,DumpBuffer* buf){
        struct drm_mode_destroy_dumb dreq;
        // unmap buffer
        munmap(buf->map, buf->size);
        /// delete framebuffer
        drmModeRmFB(fd, buf->fb);
        // delete dumb buffer
        memset(&dreq, 0, sizeof(dreq));
        dreq.handle = buf->handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        fprintf(stderr,"Deleted dump buffer\n");
    }
};

#endif //HELLO_DRMPRIME_EXTRA_DRM_H
