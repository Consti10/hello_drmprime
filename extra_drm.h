//
// Created by consti10 on 01.03.22.
//

#ifndef HELLO_DRMPRIME_EXTRA_DRM_H
#define HELLO_DRMPRIME_EXTRA_DRM_H

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

#endif //HELLO_DRMPRIME_EXTRA_DRM_H
