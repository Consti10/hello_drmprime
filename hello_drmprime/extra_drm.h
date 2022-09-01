//
// Created by consti10 on 01.03.22.
//

#ifndef HELLO_DRMPRIME_EXTRA_DRM_H
#define HELLO_DRMPRIME_EXTRA_DRM_H

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <cstdio>
#include <string>
#include <sys/mman.h>

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
    //
    uint32_t xPixelFormat;
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
        ret = drmModeAddFB(fd, buf->width, buf->height, 24, 32, buf->stride,buf->bo_handle, &buf->fb);
        //ret= drmModeAddFB2(fd, buf->width, buf->height,buf->xPixelFormat, 32, buf->stride,buf->bo_handle, &buf->fb)
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
        dreq.handle = buf->bo_handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        fprintf(stderr,"Deleted dump buffer\n");
    }
};

// --------------------------------------------------- from drm-howto ---------------------------------------------------
static int modeset_open(int *out, const char *node)
{
    int fd, ret;
    uint64_t has_dumb;

    fd = open(node, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        ret = -errno;
        fprintf(stderr, "cannot open '%s': %m\n", node);
        return ret;
    }

    if (drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0 ||
        !has_dumb) {
        fprintf(stderr, "drm device '%s' does not support dumb buffers\n",
                node);
        close(fd);
        return -EOPNOTSUPP;
    }

    *out = fd;
    return 0;
}

// returns fully red,green,blue color alternating and wrapping around
static uint32_t createColor(const int idx){
  uint8_t r,g,b;
  int colorIdx= idx % 3;
  if(colorIdx==0){
	r=255;
	g=0;
	b=0;
  }else if(colorIdx==1){
	r=0;
	g=255;
	b=0;
  }else{
	r=0;
	g=0;
	b=255;
  }
  const uint32_t rgb=(r << 16) | (g << 8) | b;
  return rgb;
}

static inline void memset32_loop(uint32_t* dest,const uint32_t value,int num){
  for ( ; num ; dest+=1, num-=1) {
	*dest=value;
  }
}

// fill a RGB(A) frame buffer with a specific color, taking stride into account
static void fillFrame(uint8_t* dest,const int width,const int height,const int stride,const uint32_t rgb){
  if(stride==width*4){
	memset32_loop((uint32_t*)dest,rgb,height*width);
  }else{
	//std::cout<<stride<<" "<<width<<"\n";
	for (int j = 0; j < height; ++j) {
	  const int offsetStride=stride * j;
	  uint32_t* lineStart=(uint32_t*)&dest[offsetStride];
	  memset32_loop(lineStart,rgb,width);
	}
  }
}

// frm librepi
/*std::string connectorTypeToStr(uint32_t type) {
  switch (type) {
	case DRM_MODE_CONNECTOR_HDMIA: // 11
	  return "HDMIA";
	case DRM_MODE_CONNECTOR_DSI: // 16
	  return "DSI";
  }
  return "unknown";
}

void printDrmModes(int fd) {
  drmVersionPtr version = drmGetVersion(fd);
  printf("version %d.%d.%d\nname: %s\ndate: %s\ndescription: %s\n", version->version_major, version->version_minor, version->version_patchlevel, version->name, version->date, version->desc);
  drmFreeVersion(version);
  drmModeRes * modes = drmModeGetResources(fd);
  for (int i=0; i < modes->count_fbs; i++) {
	printf("FB#%d: %x\n", i, modes->fbs[i]);
  }
  for (int i=0; i < modes->count_crtcs; i++) {
	printf("CRTC#%d: %d\n", i, modes->crtcs[i]);
	drmModeCrtcPtr crtc = drmModeGetCrtc(fd, modes->crtcs[i]);
	printf("  buffer_id: %d\n", crtc->buffer_id);
	printf("  position: %dx%d\n", crtc->x, crtc->y);
	printf("  size: %dx%d\n", crtc->width, crtc->height);
	printf("  mode_valid: %d\n", crtc->mode_valid);
	printf("  gamma_size: %d\n", crtc->gamma_size);
	printf("  Mode\n    clock: %d\n", crtc->mode.clock);
	drmModeModeInfo &mode = crtc->mode;
	printf("    h timings: %d %d %d %d %d\n", mode.hdisplay, mode.hsync_start, mode.hsync_end, mode.htotal, mode.hskew);
	printf("    v timings: %d %d %d %d %d\n", mode.vdisplay, mode.vsync_start, mode.vsync_end, mode.vtotal, mode.vscan);
	printf("    vrefresh: %d\n", mode.vrefresh);
	printf("    flags: 0x%x\n", mode.flags);
	printf("    type: %d\n", mode.type);
	printf("    name: %s\n", mode.name);
	drmModeFreeCrtc(crtc);
  }
  for (int i=0; i < modes->count_connectors; i++) {
	printf("Connector#%d: %d\n", i, modes->connectors[i]);
	drmModeConnectorPtr connector = drmModeGetConnector(fd, modes->connectors[i]);
	if (connector->connection == DRM_MODE_CONNECTED) puts("  connected!");
	std::string typeStr = connectorTypeToStr(connector->connector_type);
	printf("  ID: %d\n  Encoder: %d\n  Type: %d %s\n  type_id: %d\n  physical size: %dx%d\n", connector->connector_id, connector->encoder_id, connector->connector_type, typeStr.c_str(), connector->connector_type_id, connector->mmWidth, connector->mmHeight);
	for (int j=0; j < connector->count_encoders; j++) {
	  printf("  Encoder#%d:\n", j);
	  drmModeEncoderPtr enc = drmModeGetEncoder(fd, connector->encoders[j]);
	  printf("    ID: %d\n    Type: %d\n    CRTCs: 0x%x\n    Clones: 0x%x\n", enc->encoder_id, enc->encoder_type, enc->possible_crtcs, enc->possible_clones);
	  drmModeFreeEncoder(enc);
	}
	printf("  Modes: %d\n", connector->count_modes);
	for (int j=0; j < connector->count_modes; j++) {
	  printf("  Mode#%d:\n", j);
	  if (j > 1) break;
	  drmModeModeInfo &mode = connector->modes[j];
	  printf("    clock: %d\n", mode.clock);
	  printf("    h timings: %d %d %d %d %d\n", mode.hdisplay, mode.hsync_start, mode.hsync_end, mode.htotal, mode.hskew);
	  printf("    v timings: %d %d %d %d %d\n", mode.vdisplay, mode.vsync_start, mode.vsync_end, mode.vtotal, mode.vscan);
	  printf("    vrefresh: %d\n", mode.vrefresh);
	  printf("    flags: 0x%x\n", mode.flags);
	  printf("    type: %d\n", mode.type);
	  printf("    name: %s\n", mode.name);
	}
	drmModeFreeConnector(connector);
  }
  for (int i=0; i < modes->count_encoders; i++) {
	printf("Encoder#%d: %d\n", i, modes->encoders[i]);
  }
  printf("min size: %dx%d\n", modes->min_width, modes->min_height);
  printf("max size: %dx%d\n", modes->max_width, modes->max_height);
  drmModeFreeResources(modes);
}*/

#endif //HELLO_DRMPRIME_EXTRA_DRM_H
