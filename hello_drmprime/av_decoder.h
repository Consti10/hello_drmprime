//
// Created by consti10 on 29.08.22.
//

#ifndef HELLO_DRMPRIME_HELLO_DRMPRIME_AV_DECODER_H_
#define HELLO_DRMPRIME_HELLO_DRMPRIME_AV_DECODER_H_

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/avassert.h>
#include <libavutil/imgutils.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
//
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_drm.h"
#include "libavutil/pixdesc.h"
}

class AVDecoder {

};

#endif //HELLO_DRMPRIME_HELLO_DRMPRIME_AV_DECODER_H_
