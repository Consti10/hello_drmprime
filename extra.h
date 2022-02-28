//
// Created by consti10 on 03.02.22.
//

#ifndef HELLO_DRMPRIME_EXTRA_H
#define HELLO_DRMPRIME_EXTRA_H

// extra code to validate the "no frame buffering" decoder properties

#include <array>
#include <cassert>
#include <iostream>

static constexpr auto NALU_MAXLEN=1024*1024*10;

static void check_single_nalu(const uint8_t* data,const size_t data_length){
    size_t nalu_data_position=4;
    int nalu_search_state=0;

    int nNALUs=0;
    //if(nalu_data== nullptr){
    //    nalu_data=new uint8_t[NALU::NALU_MAXLEN];
    //}
    //MLOGD<<"NALU data "<<data_length;
    for (size_t i = 0; i < data_length; ++i) {
        //nalu_data[nalu_data_position++] = data[i];
        nalu_data_position++;
        if (nalu_data_position >= NALU_MAXLEN - 1) {
            // This should never happen, but rather continue parsing than
            // possibly raising an 'memory access' exception
            nalu_data_position = 0;
        }
        // Since the '0,0,0,1' is written by the loop,
        // The 5th byte is the first byte that is actually 'parsed'
        switch (nalu_search_state) {
            case 0:
            case 1:
            case 2:
                if (data[i] == 0)
                    nalu_search_state++;
                else
                    nalu_search_state = 0;
                break;
            case 3:
                if (data[i] == 1) {
                    //nalu_data[0] = 0;
                    //nalu_data[1] = 0;
                    //nalu_data[2] = 0;
                    //nalu_data[3] = 1;
                    // Forward NALU only if it has enough data
                    //if(cb!=nullptr && nalu_data_position>=4){
                    /*if(cb!=nullptr && nalu_data_position>=4 ){
                        const size_t naluLen=nalu_data_position-4;
                        const size_t minNaluSize=NALU::getMinimumNaluSize(isH265);
                        if(naluLen>=minNaluSize){
                            NALU nalu(nalu_data,naluLen,isH265);
                            cb(nalu);
                        }
                    }*/
                    const size_t naluLen=nalu_data_position-4;
                    std::cout<<"Found nalu of len:"<<naluLen<<"\n";
                    nNALUs++;
                    nalu_data_position = 4;
                }
                nalu_search_state = 0;
                break;
            default:
                break;
        }
    }
    std::cout<<"N nalus in this buffer:"<<nNALUs<<"\n";
    //assert(nNALUs==1);
}

static void memcpy_uint8_loop(uint8_t* dst,const uint8_t* src,size_t length){
    for(int i=0;i<length;i++){
        dst[i]=src[i];
    }
}

/*#include <arm_neon.h>
static void memcpy_uint8_neon(uint8_t* dst,const uint8_t* src,size_t length){
    assert(length % 8 ==0);
    uint8x8_t in;
    for (uint8_t* end=dst+length; dst<end; dst+=8,src+=8) {
        in  = vld1_u8((const uint8_t*)src);
        vst1_u8(dst,in);
    }
}

static void memcpy_uint8_neon2(uint8_t* dst,const uint8_t* src,size_t length){
    assert(length % 16 ==0);
    uint8x8_t in1,in2;
    for (uint8_t* end=dst+length; dst<end; dst+=16,src+=16) {
        in1  = vld1_u8((const uint8_t*)src);
        in2  = vld1_u8((const uint8_t*)src+8);
        vst1_u8(dst,in1);
        vst1_u8(dst+8,in2);
    }
}*/

static void memcpy_uint8(uint8_t* dst,const uint8_t* src,size_t length){
    //memcpy_uint8_neon(dst,src,length);
    //memcpy_uint8_neon(dst,src,length);
    memcpy_uint8_loop(dst,src,length);
}

#endif //HELLO_DRMPRIME_EXTRA_H
