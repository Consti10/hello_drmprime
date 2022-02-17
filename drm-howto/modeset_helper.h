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

#include <arm_neon.h>
static inline void memset32_neon(uint32_t* dest,const uint32_t value,int num){
    assert(num % 4 ==0);
    uint32x4_t in=vdupq_n_u32(value);
    for ( ; num ; dest+=4, num-=4) {
        vst1q_u32(dest,in);
    }
}

static inline void memset32_fast(uint32_t* dest,const uint32_t value,int num){
    memset32_neon(dest,value,num);
}


// fill a RGBA frame buffer with a specific color, taking stride into account
static void fillFrame(uint8_t* dest,const int width,const int height,const int stride,const uint32_t rgb){
    /*for (int j = 0; j < height; ++j) {
        const int offsetStride=stride * j;
        uint32_t* lineStart=(uint32_t*)&dest[offsetStride];
        memset32_fast(lineStart,rgb,width);
    }*/
    memset32_fast((uint32_t*)dest,rgb,10);
}

static void fillFrame2(uint8_t* dest,const int width,const int height,const int stride,const uint32_t rgb){
    // with NEON we can write chunks of
}


#endif //HELLO_DRMPRIME_MODESET_ARGS_H
