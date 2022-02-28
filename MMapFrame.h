//
// Created by consti10 on 28.02.22.
//

#ifndef HELLO_DRMPRIME_MMAPFRAME_H
#define HELLO_DRMPRIME_MMAPFRAME_H

#include <sys/mman.h>

extern "C" {
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_drm.h"
#include "libavutil/pixdesc.h"
}
#include "extra.h"

// MMap a drm prime ffmpeg frame
class MMapFrame{
public:
    uint8_t* map=NULL;
    int map_size=0;
    MMapFrame(AVFrame* frame){
        mapFrame(frame);
    }
    void mapFrame(AVFrame* frame){
        const AVDRMFrameDescriptor *desc = (AVDRMFrameDescriptor *)frame->data[0];
        mapFrameDescriptor(desc);
    }
    void mapFrameDescriptor(const AVDRMFrameDescriptor * desc){
        if(desc->nb_objects!=1){
            fprintf(stderr,"Unexpected desc->nb_objects: %d\n",desc->nb_objects);
        }
        const AVDRMObjectDescriptor *obj = &desc->objects[0];
        map = (uint8_t *) mmap(0, obj->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                               obj->fd, 0);
        if (map == MAP_FAILED) {
            fprintf(stderr,"Cannot map buffer\n");
            map = NULL;
            return;
        }
        map_size=obj->size;
        MLOGD<<"Mapped buffer size:" << obj->size << "\n";
    }
    void unmap(){
        if(map!=NULL){
            const auto ret=munmap(map,map_size);
            if(ret!=0){
                MLOGD<<"unmap failed:"<<ret<<"\n";
            }
        }
    }
};

// copy data from one AVFrame drm prime frame to another via mmap
static void workaround_copy_frame_data(AVFrame* dst, AVFrame* src){
    MMapFrame dstMap(dst);
    MMapFrame srcMap(src);
    if(dstMap.map_size!=srcMap.map_size){
        fprintf(stderr,"Cannot copy data from mapped buffer size %d to buff size %d",srcMap.map_size,dstMap.map_size);
    }else{
        printf("Copying start\n");
        memcpy_uint8(dstMap.map,srcMap.map,srcMap.map_size);
        printf("Copying stop\n");
    }
    //copy data
    dstMap.unmap();
    srcMap.unmap();
}


#endif //HELLO_DRMPRIME_MMAPFRAME_H
