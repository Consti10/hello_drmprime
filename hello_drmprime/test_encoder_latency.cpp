//
// Created by consti10 on 14.09.22.
//

// Don't let the name fool you ;)
// This is a (really special) program that allows you to measure the
// decode - to - display(e.g. decode frame -> frame is shown on screen)
// latency of an (OpenHD) ground station which always accepts UDP RTP h264/5/mjpeg data.
// It works as following:

#include <stdio.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <getopt.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/avassert.h>
#include <libavutil/imgutils.h>
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
//
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_drm.h"
#include "libavutil/pixdesc.h"
}

#include <iostream>
#include <thread>
#include <chrono>


int main(int argc, char *argv[]){
  std::cout<<"Test encoder latency begin\n";
  //
  avcodec_register_all();
  av_register_all();
  avformat_network_init();

  AVCodecID codec_id = AV_CODEC_ID_H264;
  AVCodec *codec;
  AVCodecContext *c = NULL;
  int i, ret, x, y, got_output;
  AVFrame *frame;
  AVPacket pkt;

  codec = avcodec_find_encoder(codec_id);
  c = avcodec_alloc_context3(codec);

  c->bit_rate = 400000;
  c->width = 352;
  c->height = 288;
  c->time_base.num = 1;
  c->time_base.den = 25;
  c->gop_size = 25;
  c->max_b_frames = 1;
  c->pix_fmt = AV_PIX_FMT_YUV420P;
  c->codec_type = AVMEDIA_TYPE_VIDEO;
  //c->flags = CODEC_FLAG_GLOBAL_HEADER;

  if (codec_id == AV_CODEC_ID_H264) {
	ret = av_opt_set(c->priv_data, "preset", "ultrafast", 0);
	ret = av_opt_set(c->priv_data, "tune", "zerolatency", 0);
  }

  avcodec_open2(c, codec, NULL);

  frame = av_frame_alloc();
  frame->format = c->pix_fmt;
  frame->width = c->width;
  frame->height = c->height;
  ret = av_image_alloc(frame->data, frame->linesize, c->width, c->height,
					   c->pix_fmt, 32);

  AVFormatContext* avfctx;
  AVOutputFormat* fmt = av_guess_format("rtp", NULL, NULL);

  ret = avformat_alloc_output_context2(&avfctx, fmt, fmt->name,
									   "rtp://127.0.0.1:5600");

  printf("Writing to %s\n", avfctx->filename);

  avio_open(&avfctx->pb, avfctx->filename, AVIO_FLAG_WRITE);

  AVStream* stream = avformat_new_stream(avfctx, codec);
  stream->codecpar->bit_rate = 400000;
  stream->codecpar->width = 352;
  stream->codecpar->height = 288;
  stream->codecpar->codec_id = AV_CODEC_ID_H264;
  stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
  stream->time_base.num = 1;
  stream->time_base.den = 25;

  avformat_write_header(avfctx, NULL);
  char buf[200000];
  AVFormatContext *ac[] = { avfctx };
  av_sdp_create(ac, 1, buf, 20000);
  printf("sdp:\n%s\n", buf);

  int j = 0;
  for (i = 0; i < 10000; i++) {
	av_init_packet(&pkt);
	pkt.data = NULL;    // packet data will be allocated by the encoder
	pkt.size = 0;
	fflush(stdout);
	/* prepare a dummy image */
	/* Y */
	for (y = 0; y < c->height; y++) {
	  for (x = 0; x < c->width; x++) {
		frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
	  }
	}
	/* Cb and Cr */
	for (y = 0; y < c->height / 2; y++) {
	  for (x = 0; x < c->width / 2; x++) {
		frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
		frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
	  }
	}
	frame->pts = i;
	/* encode the image */
	ret = avcodec_send_frame(c, frame);
	ret = avcodec_receive_packet(c, &pkt);

	if (ret == AVERROR_EOF) {
	  got_output = false;
	  printf("Stream EOF\n");
	} else if(ret == AVERROR(EAGAIN)) {
	  got_output = false;
	  printf("Stream EAGAIN\n");
	} else {
	  got_output = true;
	}

	if (got_output) {
	  printf("Write frame %3d (size=%5d)\n", j++, pkt.size);
	  av_interleaved_write_frame(avfctx, &pkt);
	  av_packet_unref(&pkt);
	}

	std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // end
  ret = avcodec_send_frame(c, NULL);

  /* get the delayed frames */
  for (; ; i++) {
	fflush(stdout);
	ret = avcodec_receive_packet(c, &pkt);
	if (ret == AVERROR_EOF) {
	  printf("Stream EOF\n");
	  break;
	} else if (ret == AVERROR(EAGAIN)) {
	  printf("Stream EAGAIN\n");
	  got_output = false;
	} else {
	  got_output = true;
	}

	if (got_output) {
	  printf("Write frame %3d (size=%5d)\n", j++, pkt.size);
	  av_interleaved_write_frame(avfctx, &pkt);
	  av_packet_unref(&pkt);
	}
  }

  avcodec_close(c);
  av_free(c);
  av_freep(&frame->data[0]);
  av_frame_free(&frame);
  printf("\n");
  system("pause");
  return 0;
  //
  return 0;
}