/*
 * Copyright (c) 2017 Jun Zhao
 * Copyright (c) 2017 Kaixuan Liu
 *
 * HW Acceleration API (video decoding) decode sample
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * HW-Accelerated decoding and display example.
 *
 * @example hw_decode.c
 * This example shows how to do HW-accelerated decoding with output
 * frames from the HW video surfaces.
 */

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

#include "drmprime_out.h"
#include "egl_out.h"

#include <chrono>
#include <iostream>
#include <cassert>

#include <memory>
#include <vector>
#include "extra.h"
#include "../common_consti/TimeHelper.hpp"
#include "../common_consti/LEDSwap.h"
#include "../common_consti/Logger.hpp"
//

#include "MMapFrame.h"
#include "SaveFramesToFile.hpp"
#include "ffmpeg_workaround_api_version.hpp"

static enum AVPixelFormat wanted_hw_pix_fmt;

static AvgCalculator avgDecodeTime{"DecodeTime"};
static Chronometer mmapBuffer{"mmapBuffer"};
static Chronometer copyMmappedBuffer{"copyMmappedBuffer"};

static void print_codecs_h264_h265_mjpeg(){
  void *iter = NULL;
  std::stringstream ss;
  ss<<"Codecs:\n";
  for (;;) {
	const AVCodec *cur = av_codec_iterate(&iter);
	if (!cur)
	  break;
	if(av_codec_is_decoder(cur)!=0){
	  if(cur->id==AV_CODEC_ID_H264 || cur->id==AV_CODEC_ID_H265 || cur->id==AV_CODEC_ID_MJPEG){
		ss<<"["<<cur->name<<":"<<cur->long_name<<" "<<all_formats_to_string(cur->pix_fmts)<<"]\n";
	  }
	}
  }
  std::cout<<ss.str()<<"\n";
}

static int hw_decoder_init(AVCodecContext *ctx, const enum AVHWDeviceType type){
    int err = 0;
    ctx->hw_frames_ctx = NULL;
    // ctx->hw_device_ctx gets freed when we call avcodec_free_context
    if ((err = av_hwdevice_ctx_create(&ctx->hw_device_ctx, type,
                                      NULL, NULL, 0)) < 0) {
        fprintf(stderr, "Failed to create specified HW device.\n");
        return err;
    }
    return err;
}

static enum AVPixelFormat get_hw_format(AVCodecContext *ctx,const enum AVPixelFormat *pix_fmts){
    const enum AVPixelFormat *p;
	AVPixelFormat ret=AV_PIX_FMT_NONE;
	std::stringstream supported_formats;
    for (p = pix_fmts; *p != -1; p++) {
	    const int tmp=(int)*p;
	  	supported_formats<<safe_av_get_pix_fmt_name(*p)<<"("<<tmp<<"),";
        if (*p == wanted_hw_pix_fmt){
		  // matches what we want
		  ret=*p;
		}
    }
	std::cout<<"Supported (HW) pixel formats: "<<supported_formats.str()<<"\n";
	if(ret==AV_PIX_FMT_NONE){
	  fprintf(stderr, "Failed to get HW surface format. Wanted: %s\n", av_get_pix_fmt_name(wanted_hw_pix_fmt));
	}
    return ret;
}

static void map_frame_test(AVFrame* frame){
  	static std::unique_ptr<std::vector<uint8_t>> copyBuffer=std::make_unique<std::vector<uint8_t>>(1920*1080*10);
    MLOGD<<"map_frame_test\n";
    MLOGD<<"Frame W:"<<frame->width<<" H:"<<frame->height
    <<" Cropped W:"<<av_frame_cropped_width(frame)<<" H:"<<av_frame_cropped_height(frame)<<"\n";
    mmapBuffer.start();
    const AVDRMFrameDescriptor *desc = (AVDRMFrameDescriptor *)frame->data[0];
    MMapFrame mapFrame(frame);
    copyMmappedBuffer.start();
    //memcpy(copyBuffer->data(),buffMapped,obj->size);
    memcpy_uint8(copyBuffer->data(),mapFrame.map,mapFrame.map_size);
    copyMmappedBuffer.stop();
    copyMmappedBuffer.printInIntervals(CALCULATOR_LOG_INTERVAL);
    mapFrame.unmap();
    mmapBuffer.stop();
    mmapBuffer.printInIntervals(CALCULATOR_LOG_INTERVAL);
}

//Sends one frame to the decoder, then waits for the output frame to become available
static int decode_and_wait_for_frame(AVCodecContext * const avctx,AVPacket *packet,DRMPrimeOut * const drm_prime_out,EGLOut* const egl_out){
    AVFrame *frame = nullptr;
    // testing
    //check_single_nalu(packet->data,packet->size);
    MLOGD<<"Decode packet:"<<packet->pos<<" size:"<<packet->size<<" B\n";
    const auto beforeFeedFrame=std::chrono::steady_clock::now();
    const auto beforeFeedFrameUs=getTimeUs();
    int ret = avcodec_send_packet(avctx, packet);
    if (ret < 0) {
        fprintf(stderr, "Error during decoding\n");
        return ret;
    }
    // alloc output frame(s)
    if (!(frame = av_frame_alloc())) {
        fprintf(stderr, "Can not alloc frame\n");
        ret = AVERROR(ENOMEM);
        av_frame_free(&frame);
        return ret;
    }
    // Poll until we get the frame out
	const auto loopUntilFrameBegin=std::chrono::steady_clock::now();
    bool gotFrame=false;
    while (!gotFrame){
        ret = avcodec_receive_frame(avctx, frame);
        if(ret == AVERROR_EOF){
            std::cout<<"Got EOF\n";
            break;
        }else if(ret==0){
            // we got a new frame
            const auto x_delay=std::chrono::steady_clock::now()-beforeFeedFrame;
            MLOGD<<"(True) decode delay:"<<((float)std::chrono::duration_cast<std::chrono::microseconds>(x_delay).count()/1000.0f)<<" ms\n";
            avgDecodeTime.add(x_delay);
            avgDecodeTime.printInIntervals(CALCULATOR_LOG_INTERVAL);
            gotFrame=true;
            const auto now=getTimeUs();
            //MLOGD<<"Frame pts:"<<frame->pts<<" Set to:"<<now<<"\n";
            //frame->pts=now;
            frame->pts=beforeFeedFrameUs;
		  	std::cout<<"Got frame format:"<<safe_av_get_pix_fmt_name((AVPixelFormat)frame->format)<<"\n";
            // display frame
			if(drm_prime_out!= nullptr){
			  drm_prime_out->queue_new_frame_for_display(frame);
			}
			if(egl_out!= nullptr){
			  egl_out->queue_new_frame_for_display(frame);
			}
        }else{
            std::cout<<"avcodec_receive_frame returned:"<<ret<<"\n";
			// for some video files, the decoder does not output a frame every time a h264 frame has been fed
			// In this case, I unblock after X seconds, but we cannot measure the decode delay by using the before-after
			// approach. We can still measure it using the pts timestamp from av, but this one cannot necessarily be trusted 100%
			if(std::chrono::steady_clock::now()-loopUntilFrameBegin > std::chrono::seconds(2)){
			  std::cout<<"Go no frame after X seconds. Break, but decode delay will be reported wrong\n";
			  break;
			}
        }
    }
    av_frame_free(&frame);
    return 0;
}

struct Options{
    const char* in_filename=NULL;
    const char* out_filename=NULL;
    // removed deinterlace for simplicity bool deinterlace=false;
    bool keyboard_led_toggle=false;
    int render_mode=1; //default to 1, whcih measn no CPU copy or similar, but dropping frames if encoder prodcues them faster than display
    int limitedFrameRate=-1;
	bool drm_add_dummy_overlay=false;
	bool use_page_flip_on_second_frame=false;
};
static const char optstr[] = "?:i:o:kr:f:zy";
static const struct option long_options[] = {
        {"in_filename", required_argument, NULL, 'i'},
        {"out_filename", required_argument, NULL, 'o'},
        //{"deinterlace", no_argument, NULL, 'y'},
        {"keyboard_led_toggle", no_argument, NULL, 'k'},
        {"render_mode", required_argument, NULL, 'r'},
        {"framerate", no_argument, NULL, 'f'},
		{"drm_add_dummy_overlay", no_argument, NULL, 'z'},
		{"use_page_flip_on_second_frame", no_argument, NULL, 'y'},
        {NULL, 0, NULL, 0},
};

int main(int argc, char *argv[]){
    //AVFormatContext *input_ctx = nullptr;
    int ret;
    //AVStream *video = nullptr;
    AVCodecContext *decoder_ctx = nullptr;
    const AVCodec *decoder = nullptr;
    AVPacket packet;
    //enum AVHWDeviceType type;
    const char * hwdev = "drm";
    DRMPrimeOut* drm_prime_out=nullptr;
	std::unique_ptr<EGLOut> egl_out=nullptr;

    Options mXOptions{};
    {
        int c;
        while ((c = getopt_long(argc, argv, optstr, long_options, NULL)) != -1) {
            const char *tmp_optarg = optarg;
            switch (c) {
                case 'i':
                    mXOptions.in_filename=tmp_optarg;
                    break;
                case 'o':
                    mXOptions.out_filename=tmp_optarg;
                    break;
                //case 'y':
                //    mXOptions.deinterlace=true;
                //    break;
                case 'k':
                    mXOptions.keyboard_led_toggle=true;
                    break;
                case 'r':
                    mXOptions.render_mode=atoi(tmp_optarg);
                    break;
                case 'f':
                    mXOptions.limitedFrameRate= atoi(tmp_optarg);
                    break;
			  case 'z':
					mXOptions.drm_add_dummy_overlay=true;
					break;
			  case 'y':
				mXOptions.use_page_flip_on_second_frame=true;
				break;
                case '?':
                default:
                    MLOGD<<"Usage: -i --in_filename [in_filename] -o --out_filename [optional raw out filename] "<<
                    "-y --deinterlace [enable interlacing] -k --keyboard_led_toggle [enable keyboard led toggle] "<<
                    "-r --render_mode [render mode for frames]"<<" -f --framerate [limit framerate]"
                    "\n";
                    return 0;
            }
        }
        if(mXOptions.in_filename==NULL){
            MLOGD<<"No input filename,terminating\n";
            return 0;
        }
        MLOGD<<"in_filename: "<<mXOptions.in_filename<<"\n";
        MLOGD<<"out_filename: "<<(mXOptions.out_filename==NULL ? "NONE": mXOptions.out_filename)<<"\n";
        //MLOGD<<"deinterlace: "<<(mXOptions.deinterlace ? "Y":"N")<<"\n";
        MLOGD<<"keyboard_led_toggle: "<<(mXOptions.keyboard_led_toggle ? "Y":"N")<<"\n";
        MLOGD<<"render_mode: "<<mXOptions.render_mode<<"\n";
        MLOGD<<"limited framerate: "<<mXOptions.limitedFrameRate<<"\n";
	  	MLOGD<<"drm_add_dummy_overlay: "<<(mXOptions.drm_add_dummy_overlay ? "Y":"N")<<"\n";
	  	MLOGD<<"use_page_flip_on_second_frame: "<<(mXOptions.use_page_flip_on_second_frame ? "Y":"N")<<"\n";

		MLOGD<<"FFMPEG Version:"<<av_version_info()<<"\n";
		print_av_hwdevice_types();
		print_codecs_h264_h265_mjpeg();
		// Quite nice for development !
	  	//av_log_set_level(AV_LOG_DEBUG);
    }

	if(mXOptions.render_mode==0 || mXOptions.render_mode==1 || mXOptions.render_mode==2){
	  drm_prime_out = new DRMPrimeOut(mXOptions.render_mode,mXOptions.drm_add_dummy_overlay,mXOptions.use_page_flip_on_second_frame);
	}else {
	  egl_out=std::make_unique<EGLOut>(1280,720);
	  egl_out->wait_until_ready();
	}

	std::unique_ptr<SaveFramesToFile> save_frames_to_file=nullptr;
    // open the file to dump raw data
    if (mXOptions.out_filename != nullptr) {
	  save_frames_to_file=std::make_unique<SaveFramesToFile>(mXOptions.out_filename);
    }

    const char * in_file=mXOptions.in_filename;

	// These options are needed for using the foo.sdp (rtp streaming)
	// https://stackoverflow.com/questions/20538698/minimum-sdp-for-making-a-h264-rtp-stream
	// https://stackoverflow.com/questions/16658873/how-to-minimize-the-delay-in-a-live-streaming-with-ffmpeg
	AVDictionary* av_dictionary=nullptr;
	av_dict_set(&av_dictionary, "protocol_whitelist", "file,udp,rtp", 0);
	/*av_dict_set_int(&av_dictionary, "stimeout", 1000000, 0);
	av_dict_set_int(&av_dictionary, "rw_timeout", 1000000, 0);*/
	av_dict_set_int(&av_dictionary, "reorder_queue_size", 1, 0);
  	AVFormatContext *input_ctx = nullptr;
    // open the input file
    if (avformat_open_input(&input_ctx, in_file, NULL, &av_dictionary) != 0) {
        fprintf(stderr, "Cannot open input file '%s'\n", in_file);
        return -1;
    }

    if (avformat_find_stream_info(input_ctx, NULL) < 0) {
        fprintf(stderr, "Cannot find input stream information.\n");
        return -1;
    }

    // find the video stream information
    //ret = av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1,(const AVCodec**) &decoder, 0);
	ret = av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1,(AVCodec**) &decoder, 0);
    if (ret < 0) {
        fprintf(stderr, "Cannot find a video stream in the input file\n");
        return -1;
    }
    const int video_stream = ret;

	//const AVHWDeviceType kAvhwDeviceType = av_hwdevice_find_type_by_name(hwdev);
	const AVHWDeviceType kAvhwDeviceType = AV_HWDEVICE_TYPE_DRM;
	//const AVHWDeviceType kAvhwDeviceType = AV_HWDEVICE_TYPE_VAAPI;
	//const AVHWDeviceType kAvhwDeviceType = AV_HWDEVICE_TYPE_CUDA;
	//const AVHWDeviceType kAvhwDeviceType = AV_HWDEVICE_TYPE_VDPAU;
	fprintf(stdout, "kAvhwDeviceType name: [%s]\n", safe_av_hwdevice_get_type_name(kAvhwDeviceType).c_str());
    if (decoder->id == AV_CODEC_ID_H264) {
	    std::cout<<"H264 decode\n";
        /*if ((decoder = avcodec_find_decoder_by_name("h264_v4l2m2m")) == NULL) {
            fprintf(stderr, "Cannot find the h264 v4l2m2m decoder\n");
            return -1;
        }
	  	wanted_hw_pix_fmt = AV_PIX_FMT_DRM_PRIME;*/
		wanted_hw_pix_fmt = AV_PIX_FMT_YUV420P;
    }
    else if(decoder->id==AV_CODEC_ID_H265){
	  	assert(decoder->id==AV_CODEC_ID_H265);
	  	std::cout<<"H265 decode\n";
        for (int i = 0;; i++) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
            if (!config) {
                fprintf(stderr, "Decoder %s does not support device type %s.\n",
                        decoder->name, safe_av_hwdevice_get_type_name(kAvhwDeviceType).c_str());
                //return -1;
			  	break;
            }
			std::stringstream ss;
			ss<<"HW config "<<i<<" ";
		  	ss<<"HW Device name: "<<safe_av_hwdevice_get_type_name(config->device_type);
			ss<<" PIX fmt: "<<safe_av_get_pix_fmt_name(config->pix_fmt)<<"\n";
			std::cout<<ss.str();

            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == kAvhwDeviceType) {
			  	wanted_hw_pix_fmt = config->pix_fmt;
                break;
            }
        }

		//wanted_hw_pix_fmt = AV_PIX_FMT_DRM_PRIME;
	  	//wanted_hw_pix_fmt = AV_PIX_FMT_CUDA;
		//wanted_hw_pix_fmt = AV_PIX_FMT_VAAPI;
	  	wanted_hw_pix_fmt = AV_PIX_FMT_YUV420P;
	  	//wanted_hw_pix_fmt = AV_PIX_FMT_VAAPI;
		//wanted_hw_pix_fmt = AV_PIX_FMT_VDPAU;
    }else if(decoder->id==AV_CODEC_ID_MJPEG){
	  std::cout<<"Codec mjpeg\n";
	  //wanted_hw_pix_fmt=AV_PIX_FMT_YUVJ422P;
	  wanted_hw_pix_fmt=AV_PIX_FMT_CUDA;
	}else{
	  std::cerr<<"We only do h264,h265 and mjpeg in this project\n";
	  avformat_close_input(&input_ctx);
	  return 0;
	}

    if (!(decoder_ctx = avcodec_alloc_context3(decoder))){
	  	std::cout<<"avcodec_alloc_context3 failed\n";
        return AVERROR(ENOMEM);
    }
	// From moonlight-qt. However, on PI, this doesn't seem to make any difference, at least for H265 decode.
	// (I never measured h264, but don't think there it is different).
	// Always request low delay decoding
  	decoder_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
	// Allow display of corrupt frames and frames missing references
  	decoder_ctx->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT;
  	decoder_ctx->flags2 |= AV_CODEC_FLAG2_SHOW_ALL;

    AVStream *video = input_ctx->streams[video_stream];
    if (avcodec_parameters_to_context(decoder_ctx, video->codecpar) < 0)
        return -1;

    decoder_ctx->get_format  = get_hw_format;

    if (hw_decoder_init(decoder_ctx, kAvhwDeviceType) < 0){
	  std::cerr<<"HW decoder init failed,fallback to SW decode\n";
	  // Use SW decode as fallback ?!
	  //return -1;
	  wanted_hw_pix_fmt=AV_PIX_FMT_YUV420P;
	}

    // Consti10
    decoder_ctx->thread_count = 1;

    if ((ret = avcodec_open2(decoder_ctx, decoder, nullptr)) < 0) {
        fprintf(stderr, "Failed to open codec for stream #%u\n", video_stream);
        return -1;
    }

	/*if(egl_out){
	  egl_out->set_codec_context(decoder_ctx);
	}*/
    // actual decoding and dump the raw data
    const auto decodingStart=std::chrono::steady_clock::now();
    int nFeedFrames=0;
    auto lastFrame=std::chrono::steady_clock::now();
    while (ret >= 0) {
        if ((ret = av_read_frame(input_ctx, &packet)) < 0){
            MLOGD<<"av_read_frame returned 0\n";
            break;
        }
        if (video_stream == packet.stream_index){
            if(mXOptions.keyboard_led_toggle){
                // wait for a keyboard input
                printf("Press ENTER key to Feed new frame\n");
                auto tmp=getchar();
                // change LED, feed one new frame
                switch_led_on_off();
            }else{
                // limit frame rate if enabled
                if(mXOptions.limitedFrameRate!=-1){
                    const long frameDeltaNs=1000*1000*1000 / mXOptions.limitedFrameRate;
                    while (std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-lastFrame).count()<frameDeltaNs){
                        // busy wait
                    }
                    lastFrame=std::chrono::steady_clock::now();
                }
            }
            ret = decode_and_wait_for_frame(decoder_ctx, &packet,drm_prime_out,egl_out.get());
            nFeedFrames++;
            const uint64_t runTimeMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-decodingStart).count();
            const double runTimeS=runTimeMs/1000.0f;
            const double fps=runTimeS==0 ? 0 : nFeedFrames/runTimeS;
            MLOGD<<"Fake fps:"<<fps<<"\n";
        }
        av_packet_unref(&packet);
    }

    // flush the decoder
    packet.data = NULL;
    packet.size = 0;
    ret = decode_and_wait_for_frame(decoder_ctx, &packet,drm_prime_out,egl_out.get());
    av_packet_unref(&packet);

    if (save_frames_to_file){
        save_frames_to_file=nullptr;
    }
    avcodec_free_context(&decoder_ctx);
    avformat_close_input(&input_ctx);

    if(drm_prime_out!=nullptr){
        delete drm_prime_out;
    }
  	egl_out= nullptr;

    return 0;
}
