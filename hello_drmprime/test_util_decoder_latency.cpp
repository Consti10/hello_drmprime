//
// Created by consti10 on 14.09.22.
//

// Don't let the name fool you ;)
// This is a (really special) program that allows you to measure the
// "decode - to - display" delay of another application that receives video data via udp/rtp.
// (e.g. rtp in, decode frame -> frame is shown on screen)
// OpenHD defaults to QOpenHD as the UI application, but feel free to test things out / use some alternate form of Video+OSD compositor.
// This test program works as following:
// (If option keyboard & led toggle is set)
// Each time a key on the keyboard is pressed
// SW encode a new colored frame
// Once the frame has been encoded (we get the raw encoded data for one frame)
// toggle the LED on/off (almost instantaneous)
// send the encoded frame data out via RTP
// By mapping the LED toggles to the changes on the display (Warning: easy to mix up, keyword: rtp/encoder buffering
// You can then measure the delta between <this application sends out a new encoded frame via RTP> -> <the display application to
// test (e.g. QOpenHD) is done decoding AND displaying> (Decode delay,Opt OpenGL render delay, HVS delay, HDMI transmission (negligible),
// "monitor processing delay" (negligible on most recent high quality monitors), pixel response time).

// NOTE: Since the LED toggle happens after a frame has been encoded (before it is sent out via udp/rtp) the encode
// time doesn't matter in the context of this text. This is why it is okay to always use SW encoding here.

// TODOs: MJPEG still doesn't work (some weird internal av pixel format rtp parser bug)

#include <stdio.h>
#include <sys/mman.h>
#include <getopt.h>

/*extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/avassert.h>
#include <libavutil/imgutils.h>
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
}*/
#include "avcodec_helper.hpp"

#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>
#include <vector>
#include "../common_consti/LEDSwap.h"
#include "../common_consti/StringHelper.hpp"

struct Options{
  int width=1280;
  int height=720;
  int limitedFrameRate=-1;
  bool keyboard_led_toggle=false;
  int codec_type=0; //H264=0,H265=1,MJPEG=2
  int n_frames=-1;
};
static const char optstr[] = "?:w:h:f:kc:n:";
static const struct option long_options[] = {
	{"width", required_argument, NULL, 'w'},
	{"height", required_argument, NULL, 'w'},
	{"framerate", no_argument, NULL, 'f'},
	{"keyboard_led_toggle", no_argument, NULL, 'k'},
	{"codec_type", no_argument, NULL, 'c'},
	{"n_frames", required_argument, NULL, 'n'},
	{NULL, 0, NULL, 0},
};

static Options parse_options(int argc, char *argv[]){
  Options options{};
  options.keyboard_led_toggle= false;
  int c;
  while ((c = getopt_long(argc, argv, optstr, long_options, NULL)) != -1) {
	const char *tmp_optarg = optarg;
	switch (c) {
	  case 'k':
		options.keyboard_led_toggle=true;
		break;
	  case 'f':
		options.limitedFrameRate= atoi(tmp_optarg);
		break;
	  case 'w':
		options.width= atoi(tmp_optarg);
		break;
	  case 'h':
		options.height= atoi(tmp_optarg);
		break;
	  case 'c':
		options.codec_type= atoi(tmp_optarg);
		break;
	  case 'n':
		options.n_frames= atoi(tmp_optarg);
		break;
	  case '?':
	  default:
		std::cout<<"Usage: "<<" -f --framerate [limit framerate]"<<"\n";
		exit(0);
	}
  }
  return options;
}
static AVCodecID av_codec_id_from_user(const Options& options){
  if(options.codec_type==0)return AV_CODEC_ID_H264;
  if(options.codec_type==1)return AV_CODEC_ID_H265;
  return AV_CODEC_ID_MJPEG;
}

static void always_av_opt_set(void *obj, const char *name, const char *val, int search_flags){
  auto ret = av_opt_set(obj, name,val,search_flags);
  assert(ret==0);
}
static void always_av_dict_set(AVDictionary **pm, const char *key, const char *value, int flags){
  auto ret= av_dict_set(pm,key,value,flags);
  assert(ret==0);
}

static bool encode_one_frame(AVCodecContext *c,AVFrame *frame,AVPacket* out_packet){
  // encoding should never fail
  const auto ret1 = avcodec_send_frame(c, frame);
  assert(ret1==0);
  // We should always get a encoded packet out when we encode one frame
  // (since we configure the encoder this way, and need this "property" for the test anyways.
  const auto poll_frames_begin=std::chrono::steady_clock::now();
  bool got_frame=false;
  while (!got_frame){
	const auto ret2 = avcodec_receive_packet(c, out_packet);
	if(ret2==0){
	  break;
	}else if(ret2==AVERROR(EAGAIN)){
	  //std::cout<<"EAGAIN\n";
	}else{
	  std::cout<<"Error:"<<ret2<<"\n";
	}
	if((std::chrono::steady_clock::now()-poll_frames_begin)>std::chrono::seconds(1)){
	  std::cout<<"No frame after X seconds\n";
	  return false;
	}
  }
  return true;
}

//static std::array<uint8_t,3> rgb_to_YCbCr(uint32_t rgb){
//}

static std::array<uint8_t,3> YCbCr_from_index(int index){
  // https://tvone.com/tech-support/faqs/120-ycrcb-values-for-various-colors
  //std::array<uint8_t,3> red{81,240,90};
  //std::array<uint8_t,3> green{145,34,54};
  //std::array<uint8_t,3> blue{41,110,240};
  // https://www.mikekohn.net/file_formats/yuv_rgb_converter.php
  std::array<uint8_t,3> red{67,90,240};
  std::array<uint8_t,3> green{149,43,21};
  std::array<uint8_t,3> blue{29,255,107};
  const int index_mod=index % 3;
  if(index_mod==0){
	std::cout<<"RED\n";
	return red;
  }
  if(index_mod==1){
	std::cout<<"GREEN\n";
	return green;
  }
  std::cout<<"BLUE\n";
  return blue;
}

static void fill_image2(AVFrame* frame,int index){
  std::cout<<"Fill image "<<frame->width<<"x"<<frame->height<<"index:"<<index<<"\n";
  const auto YCbCr= YCbCr_from_index(index);
  for (int y = 0; y < frame->height; y++) {
	for (int x = 0; x < frame->width; x++) {
	  frame->data[0][y * frame->linesize[0] + x] = YCbCr[0];
	}
  }
  // Cb and Cr
  for (int y = 0; y < frame->height / 2; y++) {
	for (int x = 0; x < frame->width / 2; x++) {
	  frame->data[1][y * frame->linesize[1] + x] = YCbCr[1];
	  frame->data[2][y * frame->linesize[2] + x] = YCbCr[2];
	}
  }
}

// The h265 SW encoder is so "smart" to detect that nothing has "changed" if we
// just clear the frame with a solid color. Draw "something" to avoid that
static void intentionally_draw_some_random_data(AVFrame* frame){
  srand(time(NULL));
  uint8_t rand_y=rand() % (255+1);
  uint8_t rand_u=rand() % (255+1);
  uint8_t rand_v=rand() % (255+1);
  const int area_divider=6;
  for (int y = 0; y < frame->height / area_divider; y++) {
	for (int x = 0; x < frame->width / area_divider; x++) {
	  frame->data[0][y * frame->linesize[0] + x] = rand_y;
	  //rand_y++;
	}
  }
  // Cb and Cr
  for (int y = 0; y < frame->height / 2 / area_divider; y++) {
	for (int x = 0; x < frame->width / 2 / area_divider; x++) {
	  frame->data[1][y * frame->linesize[1] + x] = rand_u;
	  frame->data[2][y * frame->linesize[2] + x] = rand_v;
	  //rand_u++;
	  //rand_v++;
	}
  }
}

int main(int argc, char *argv[]){
  std::cout<<"Test encoder latency begin\n";
  const auto options= parse_options(argc,argv);
  std::cout<<"Options:\n"
  <<"keyboard_led_toggle:"<<(options.keyboard_led_toggle ? "Y":"N")<<"\n";
  //
  avcodec_register_all();
  av_register_all();
  avformat_network_init();

  const AVCodecID codec_id = av_codec_id_from_user(options);
  assert(codec_id == AV_CODEC_ID_H264 || codec_id == AV_CODEC_ID_H265 || codec_id == AV_CODEC_ID_MJPEG);

  AVCodec *codec;
  AVCodecContext *c = NULL;
  int ret;
  AVFrame *frame;
  AVPacket pkt;

  codec = avcodec_find_encoder(codec_id);
  c = avcodec_alloc_context3(codec);

  const int video_width=options.width;
  const int video_height=options.height;
  c->bit_rate = 400000;
  c->width = video_width;
  c->height = video_height;
  c->time_base.num = 1;
  c->time_base.den = 25;
  c->gop_size = 25;
  if(codec_id==AV_CODEC_ID_H264 || codec_id==AV_CODEC_ID_H265){
	c->max_b_frames = 1;
  }
  if(codec_id==AV_CODEC_ID_MJPEG){
	c->pix_fmt = AV_PIX_FMT_YUVJ422P;
  }else{
	c->pix_fmt = AV_PIX_FMT_YUV420P;
  }
  c->codec_type = AVMEDIA_TYPE_VIDEO;
  //c->flags = CODEC_FLAG_GLOBAL_HEADER;
  if(codec_id!=AV_CODEC_ID_MJPEG){
	// Only h265 and h264 accept this flag, MJPEG does not.
	c->flags = AV_CODEC_FLAG_LOW_DELAY;
  }

  AVDictionary* av_dictionary=nullptr;
  if (codec_id == AV_CODEC_ID_H264) {
	always_av_opt_set(c->priv_data, "preset", "ultrafast", 0);
	always_av_opt_set(c->priv_data, "tune", "zerolatency", 0);
	always_av_opt_set(c->priv_data,"rc-lookahead","0",0);
	always_av_opt_set(c->priv_data,"profile","baseline",0);
	//always_av_opt_set(c->priv_data,"refs","1",0);
  }else if(codec_id==AV_CODEC_ID_H265){
	// In contrast to h264, h265 doesn't allow setting the options directly using the "priv_data" pointer
	always_av_dict_set(&av_dictionary, "speed-preset", "ultrafast", 0);
	always_av_dict_set(&av_dictionary,  "tune", "zerolatency", 0);
	always_av_dict_set(&av_dictionary, "rc-lookahead","0",0);
	//always_av_dict_set(&av_dictionary, "profile","baseline",0);
  }
  av_log_set_level(AV_LOG_TRACE);
  c->thread_count = 1;

  //
  av_dict_set_int(&av_dictionary, "reorder_queue_size", 1, 0);
  av_dict_set(&av_dictionary,"max_delay",0,0);

  std::cout<<"All encode formats:"<<all_formats_to_string(codec->pix_fmts)<<"\n";
  //
  avcodec_open2(c, codec, &av_dictionary);

  frame = av_frame_alloc();
  frame->format = c->pix_fmt;
  frame->width = c->width;
  frame->height = c->height;
  ret = av_image_alloc(frame->data, frame->linesize, c->width, c->height,
					   c->pix_fmt, 32);
  assert(ret>=0);

  AVFormatContext* avfctx;
  AVOutputFormat* fmt = av_guess_format("rtp", NULL, NULL);
  std::cout<<"FMT name:"<<fmt->name<<"\n";

  ret = avformat_alloc_output_context2(&avfctx, fmt, fmt->name,"rtp://127.0.0.1:5600");
  //ret = avformat_alloc_output_context2(&avfctx, fmt, fmt->name,"rtp://192.168.239.160:5600");
  assert(ret>=0);

  printf("Writing to %s\n", avfctx->filename);

  avio_open(&avfctx->pb, avfctx->filename, AVIO_FLAG_WRITE);

  AVStream* stream = avformat_new_stream(avfctx, codec);
  stream->codecpar->bit_rate = 400000;
  stream->codecpar->width = video_width;
  stream->codecpar->height = video_height;
  stream->codecpar->codec_id = codec_id;
  stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
  stream->time_base.num = 1;
  stream->time_base.den = 25;

  ret=avformat_write_header(avfctx, NULL);
  assert(ret>=0);
  {
	char buf[20000];
	AVFormatContext *ac[] = { avfctx };
	av_sdp_create(ac, 1, buf, 20000);
	printf("sdp:[\n%s]\n", buf);
  }
  int i=0;
  int frameCount=0;
  auto lastFrame=std::chrono::steady_clock::now();
  while(true) {
	if(options.n_frames>0){
	  if(frameCount>=options.n_frames){
		break;
	  }
	}
	if(options.keyboard_led_toggle){
	  // wait for a keyboard input
	  printf("Press ENTER key to encode new frame\n");
	  auto tmp=getchar();
	}else{
	  // limit frame rate if enabled
	  if(options.limitedFrameRate!=-1){
		const long frameDeltaNs=1000*1000*1000 / options.limitedFrameRate;
		while (std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-lastFrame).count()<frameDeltaNs){
		  // busy wait
		}
		lastFrame=std::chrono::steady_clock::now();
	  }
	  //std::this_thread::sleep_for(std::chrono::seconds(10));
	}
	printf("Encode one frame begin\n");
	i++;
	av_init_packet(&pkt);
	pkt.data = NULL;    // packet data will be allocated by the encoder
	pkt.size = 0;
	// Draw something into the frame
	fill_image2(frame,i);
	intentionally_draw_some_random_data(frame);
	frame->pts = i;

	const bool got_frame=encode_one_frame(c,frame,&pkt);
	assert(got_frame);
	if(got_frame){
	  std::cout<<"Write frame "<<frameCount++<<" size:"<<pkt.size<<"\n";
	  //printf("Write frame %3d (size=%5d)\n", frameCount++, pkt.size);

	  // Change led just before writing the rtp data (for one single frame)
	  if(options.keyboard_led_toggle){
		switch_led_on_off();
	  }
	  {
		std::vector<uint8_t> tmp_data(pkt.data,pkt.data+pkt.size);
		std::cout<<StringHelper::vectorAsString(tmp_data)<<"\n";
	  }
	  //ret= av_write_frame(avfctx,&pkt);
	  ret=av_interleaved_write_frame(avfctx, &pkt);
	  assert(ret==0);
	  av_packet_unref(&pkt);
	}else{
	  std::cout<<"Got no frame\n";
	}
	printf("Encode one frame end\n");
  }
  // end
  ret = avcodec_send_frame(c, NULL);
  // Note: we don't care about delayed frames
  avcodec_close(c);
  av_free(c);
  av_freep(&frame->data[0]);
  av_frame_free(&frame);
  printf("DONE\n");
  return 0;
}