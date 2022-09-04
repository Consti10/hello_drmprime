//
// Created by consti10 on 04.09.22.
//

#ifndef HELLO_DRMPRIME__FFMPEG_WORKAROUND_API_VERSION_H_
#define HELLO_DRMPRIME__FFMPEG_WORKAROUND_API_VERSION_H_

// For some reaseon av_frame_cropped_width doesn't exit on ffmpeg default on ubuntu
static int av_frame_cropped_width(const AVFrame* frame){
  return frame->width;
}

static int av_frame_cropped_height(const AVFrame* frame){
  return frame->height;
}

#endif //HELLO_DRMPRIME__FFMPEG_WORKAROUND_API_VERSION_H_
