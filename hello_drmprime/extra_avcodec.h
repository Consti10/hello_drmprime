//
// Created by consti10 on 29.08.22.
//

#ifndef HELLO_DRMPRIME_HELLO_DRMPRIME_EXTRA_FILTER_GRAPH_H_
#define HELLO_DRMPRIME_HELLO_DRMPRIME_EXTRA_FILTER_GRAPH_H_
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

#include "../common_consti/Logger.hpp"
#include "../common_consti/TimeHelper.hpp"
#include <memory>
#include <vector>

// Save ffmpeg AV frames to file
class SaveFramesToFile{
 public:
  SaveFramesToFile(const char* out_filename){
	std::cout<<"Opening output fle:"<<std::string(out_filename)<<"\n";
	if ((output_file = fopen(mXOptions.out_filename, "w+")) == NULL) {
	  fprintf(stderr, "Failed to open output file %s: %s\n", out_filename, strerror(errno));
	  output_file= nullptr;
	}
  }
  ~SaveFramesToFile(){
	if (output_file){
	  fclose(output_file);
	}
  }
  void save_frame_to_file(AVFrame *frame){
	if(output_file== nullptr){
	  std::cerr<<"Output file not open,cannot save frame\n";
	  return;
	}
	const AVPixelFormat hw_pix_fmt=AV_PIX_FMT_DRM_PRIME;
	copyDataChrono.start();
	std::cout<<"Saving frame to file\n";
	AVFrame* sw_frame=NULL;
	AVFrame *tmp_frame=NULL;
	if (frame->format == hw_pix_fmt) {
	  MLOGD<<"Is hw_pix_fmt"<<frame->format<<"\n";
	  if(!(sw_frame = av_frame_alloc())){
		fprintf(stderr,"Cannot alloc frame\n");
		return;
	  }
	  transferCpuGpu.start();
	  // retrieve data from GPU to CPU
	  if (av_hwframe_transfer_data(sw_frame, frame, 0) !=0) {
		fprintf(stderr, "Error transferring the data to system memory\n");
		av_frame_free(&sw_frame);
		return;
	  }
	  transferCpuGpu.stop();
	  transferCpuGpu.printInIntervals(10);
	  tmp_frame = sw_frame;
	} else
	  tmp_frame = frame;
	const int size = av_image_get_buffer_size((AVPixelFormat)tmp_frame->format, tmp_frame->width,
											  tmp_frame->height, 1);
	MLOGD<<"Frame size in Bytes:"<<size<<"\n";
	if(size>copyBuffer->size()){
	  MLOGD<<"Resize to "<<size<<"\n";
	  copyBuffer->resize(size);
	}
	//uint8_t buffer[size];
	int ret = av_image_copy_to_buffer(copyBuffer->data(), size,
									  (const uint8_t * const *)tmp_frame->data,
									  (const int *)tmp_frame->linesize, (AVPixelFormat)tmp_frame->format,
									  tmp_frame->width, tmp_frame->height, 1);
	if (ret < 0) {
	  fprintf(stderr, "Can not copy image to buffer\n");
	  av_frame_free(&sw_frame);
	  return;
	}
	copyDataChrono.stop();
	copyDataChrono.printInIntervals(10);
	if ((ret = fwrite(copyBuffer->data(), 1, size, output_file)) < 0) {
	  fprintf(stderr, "Failed to dump raw data.\n");
	  av_frame_free(&sw_frame);
	  return;
	}
	av_frame_free(&sw_frame);
  }
 private:
  FILE *output_file = nullptr;
  Chronometer transferCpuGpu{"Transfer"};
  Chronometer copyDataChrono{"CopyData"};
  // used for testing
  std::unique_ptr<std::vector<uint8_t>> copyBuffer=std::make_unique<std::vector<uint8_t>>(1920*1080*10);
};


#endif //HELLO_DRMPRIME_HELLO_DRMPRIME_EXTRA_FILTER_GRAPH_H_
