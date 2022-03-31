//
// Created by consti10 on 16.02.22.
//

#ifndef HELLO_DRMPRIME_MODESET_ARGS_H
#define HELLO_DRMPRIME_MODESET_ARGS_H

#include <stdint.h>
#include <stdio.h>
#include <assert.h>

//args pretty much stay the same
struct modeset_options{
    const char *card="/dev/dri/card0";
    bool drawFramesOnKeyboardClick;
};

static int modesetParseArguments(int argc, char **argv,modeset_options& options){
    //modeset_options options;
    options.card="/dev/dri/card0";
    int opt;
    while ((opt = getopt(argc, argv, "d:l")) != -1) {
        switch (opt) {
            case 'd':
                options.card=optarg;
                break;
            case 'l':
                options.drawFramesOnKeyboardClick=true;
                break;
            default: /* '?' */
            show_usage:
                fprintf(stderr,"Usage: -d device/card to open -l enable LED\n");
                return -1;
        }
    }
    fprintf(stderr,"using card '%s'\n",options.card);
    fprintf(stderr,"Enable led and redraw on keyboard click:%s\n",options.drawFramesOnKeyboardClick ? "Y":"N");
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

/*#include <arm_neon.h>
static inline void memset32_neon(uint32_t* dest,const uint32_t value,int num){
    assert(num % 4 ==0);
    uint32x4_t in=vdupq_n_u32(value);
    for ( ; num ; dest+=4, num-=4) {
        vst1q_u32(dest,in);
    }
}*/

static inline void memset32_fast(uint32_t* dest,const uint32_t value,int num){
    //memset32_neon(dest,value,num);
    memset32_loop(dest,value,num);
}


// fill a RGBA frame buffer with a specific color, taking stride into account
static void fillFrame(uint8_t* dest,const int width,const int height,const int stride,const uint32_t rgb){
    if(stride==width*4){
        memset32_fast((uint32_t*)dest,rgb,height*width);
    }else{
        //std::cout<<stride<<" "<<width<<"\n";
        for (int j = 0; j < height; ++j) {
            const int offsetStride=stride * j;
            uint32_t* lineStart=(uint32_t*)&dest[offsetStride];
            memset32_fast(lineStart,rgb,width);
        }
    }
}

/*static void createDumpBuffer(int fd){
    struct drm_mode_create_dumb creq;
    // create dumb buffer
    memset(&creq, 0, sizeof(creq));
    creq.width = dev->width;
    creq.height = dev->height;
    creq.bpp = 32;
    ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
    if (ret < 0) {
        fprintf(stderr, "cannot create dumb buffer (%d): %m\n",errno);
        return;
    }

}*/

// from https://chromium.googlesource.com/chromiumos/third_party/drm/+/9b44fbd393b8db571badae41881f490145404ae0/tests/modetest/modetest.c
static void fill420(unsigned char *y, unsigned char *u, unsigned char *v,
        int cs /*chroma pixel stride */,
        int n, int width, int height, int stride){
    int i, j;
    /* paint the buffer with colored tiles, in blocks of 2x2 */
    for (j = 0; j < height; j+=2) {
        unsigned char *y1p = y + j * stride;
        unsigned char *y2p = y1p + stride;
        unsigned char *up = u + (j/2) * stride * cs / 2;
        unsigned char *vp = v + (j/2) * stride * cs / 2;
        for (i = 0; i < width; i+=2) {
            div_t d = div(n+i+j, width);
            uint32_t rgb = 0x00130502 * (d.quot >> 6) + 0x000a1120 * (d.rem >> 6);
            unsigned char *rgbp = (unsigned char *)&rgb;
            unsigned char y = (0.299 * rgbp[RED]) + (0.587 * rgbp[GREEN]) + (0.114 * rgbp[BLUE]);
            *(y2p++) = *(y1p++) = y;
            *(y2p++) = *(y1p++) = y;
            *up = (rgbp[BLUE] - y) * 0.565 + 128;
            *vp = (rgbp[RED] - y) * 0.713 + 128;
            up += cs;
            vp += cs;
        }
    }
}


#endif //HELLO_DRMPRIME_MODESET_ARGS_H
