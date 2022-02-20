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
 * HW-Accelerated decoding example.
 *
 * @example hw_decode.c
 * This example shows how to do HW-accelerated decoding with output
 * frames from the HW video surfaces.
 */

#include <stdio.h>
#include <stdbool.h>

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
}

#include "drmprime_out.h"

#include <chrono>
#include <iostream>
#include <cassert>

#include <memory>
#include <vector>
#include "extra.h"
#include "common_consti/TimeHelper.hpp"
#include "common_consti/LEDSwap.h"
#include "common_consti/Logger.hpp"

static enum AVPixelFormat hw_pix_fmt;
static FILE *output_file = NULL;
static long frames = 0;

static AVFilterContext *buffersink_ctx = NULL;
static AVFilterContext *buffersrc_ctx = NULL;
static AVFilterGraph *filter_graph = NULL;

static Chronometer transferCpuGpu{"Transfer"};
static Chronometer copyDataChrono{"CopyData"};

static int hw_decoder_init(AVCodecContext *ctx, const enum AVHWDeviceType type)
{
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

static enum AVPixelFormat get_hw_format(AVCodecContext *ctx,
                                        const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat *p;

    for (p = pix_fmts; *p != -1; p++) {
        if (*p == hw_pix_fmt)
            return *p;
    }

    fprintf(stderr, "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

static std::unique_ptr<std::vector<uint8_t>> copyBuffer=std::make_unique<std::vector<uint8_t>>(1920*1080*10);

static void save_frame_to_file_if_enabled(AVFrame *frame){
    //if (output_file != NULL) {
    if(true){
        V4L2Buffer buff;
        ff_v4l2_buffer_avframe_to_buf(frame,&buff);
    }
    copyDataChrono.start();
        std::cout<<"Saving frame to file\n";
        int ret=0;
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
            transferCpuGpu.printInIntervals(1);
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
        /*ret = av_image_copy_to_buffer(copyBuffer->data(), size,
                                      (const uint8_t * const *)tmp_frame->data,
                                      (const int *)tmp_frame->linesize, (AVPixelFormat)tmp_frame->format,
                                      tmp_frame->width, tmp_frame->height, 1);*/
        if (ret < 0) {
            fprintf(stderr, "Can not copy image to buffer\n");
            av_frame_free(&sw_frame);
            return;
        }
        copyDataChrono.stop();
        copyDataChrono.printInIntervals(1);
        /*if ((ret = fwrite(buffer, 1, size, output_file)) < 0) {
            fprintf(stderr, "Failed to dump raw data.\n");
            av_frame_free(&sw_frame);
            return;
        }*/
        av_frame_free(&sw_frame);
    //}
}

static void x_push_into_filter_graph(drmprime_out_env_t * const dpo,AVFrame *frame){
    int size;
    int ret=0;
    // push the decoded frame into the filtergraph if it exists
    if (filter_graph != NULL &&
        (ret = av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF)) < 0) {
        fprintf(stderr, "Error while feeding the filtergraph\n");
        //goto fail;
        return;
    }
    do {
        if (filter_graph != NULL) {
            av_frame_unref(frame);
            ret = av_buffersink_get_frame(buffersink_ctx, frame);
            if (ret == AVERROR(EAGAIN)) {
                ret = 0;
                break;
            }
            if (ret < 0) {
                if (ret != AVERROR_EOF){
                    // compile error fprintf(stderr, "Failed to get frame: %s", av_err2str(ret));
                    fprintf(stderr, "Failed to get frame: ");
                }
                return;
            }
        }
        std::stringstream ss;
        ss<<"x_push_into_filter_graph:pts:"<<frame->pts<<"\n";
        std::cout<<ss.str();
        /drmprime_out_display(dpo, frame);
        save_frame_to_file_if_enabled(frame);

    } while (buffersink_ctx != NULL);  // Loop if we have a filter to drain
}

static AvgCalculator avgDecodeTime{"DecodeTime"};
//Sends one frame to the decoder, then waits for the output frame to become available
static int decode_and_wait_for_frame(AVCodecContext * const avctx,
                                     drmprime_out_env_t * const dpo,
                                     AVPacket *packet){
    AVFrame *frame = NULL;
    // testing
    check_single_nalu(packet->data,packet->size);
    std::stringstream ss;
    ss<<"Decode packet:"<<packet->pos<<" size:"<<packet->size<<" B\n";
    std::cout<<ss.str();
    const auto before=std::chrono::steady_clock::now();
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
    bool gotFrame=false;
    while (!gotFrame){
        ret = avcodec_receive_frame(avctx, frame);
        if(ret == AVERROR_EOF){
            std::cout<<"Got EOF\n";
            break;
        }else if(ret==0){
            // we got a new frame
            const auto x_delay=std::chrono::steady_clock::now()-before;
            ss.str("");
            ss<<"(True) decode delay:"<<((float)std::chrono::duration_cast<std::chrono::microseconds>(x_delay).count()/1000.0f)<<" ms\n";
            avgDecodeTime.add(x_delay);
            avgDecodeTime.printInIntervals(CALCULATOR_LOG_INTERVAL);
            gotFrame=true;
            const auto now=getTimeUs();
            ss<<"Frame pts:"<<frame->pts<<" Set to:"<<now<<"\n";
            std::cout<<ss.str();
            frame->pts=now;
            // display frame
            x_push_into_filter_graph(dpo,frame);
        }else{
            std::cout<<"avcodec_receive_frame returned:"<<ret<<"\n";
        }
    }
    av_frame_free(&frame);
    return 0;
}

std::vector<std::chrono::steady_clock::time_point> feedDecoderTimePoints;
int nTotalPulledFrames=0;
// testing, obsolete
static int decode_write(AVCodecContext * const avctx,
                        drmprime_out_env_t * const dpo,
                        AVPacket *packet)
{
    AVFrame *frame = NULL, *sw_frame = NULL;
    uint8_t *buffer_for_something = NULL;
    int size;
    int ret = 0;
    unsigned int i;
    std::cout<<"Currently fed frames: "<<feedDecoderTimePoints.size()<<" Currently outputed frames:"<<nTotalPulledFrames<<"\n";
    check_single_nalu(packet->data,packet->size);

    std::cout<<"Decode packet:"<<packet->pos<<" size:"<<packet->size<<" B\n";
    const auto before=std::chrono::steady_clock::now();
    ret = avcodec_send_packet(avctx, packet);
    if (ret < 0) {
        fprintf(stderr, "Error during decoding\n");
        return ret;
    }
    //
    feedDecoderTimePoints.push_back(before);

    int nPulledFramesThisIteraton=0;
    const std::chrono::steady_clock::time_point pullFramesStartTimePoint=std::chrono::steady_clock::now();

    for (;;) {
        if (!(frame = av_frame_alloc()) || !(sw_frame = av_frame_alloc())) {
            fprintf(stderr, "Can not alloc frame\n");
            ret = AVERROR(ENOMEM);
            av_frame_free(&frame);
            av_frame_free(&sw_frame);
            av_freep(&buffer_for_something);
            return ret;
        }

        ret = avcodec_receive_frame(avctx, frame);

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_frame_free(&frame);
            av_frame_free(&sw_frame);
            //fprintf(stderr, "Got eagain. N Pulled frames: %d\n",nPulledFramesThisIteraton);
            /*const auto timePulling=std::chrono::steady_clock::now()-pullFramesStartTimePoint;
            if(timePulling>std::chrono::milliseconds(1000)){
                std::cout<<"Timeout of 1 second reached\n";
                return 0;
            }*/
            std::cout<<"This time got no frame\n";
            //return 0; // Consti10
            continue;
        } else if (ret < 0) {
            fprintf(stderr, "Error while decoding\n");
            goto fail;
        }
        assert(ret==0);
        if(ret!=0){
            std::cerr<<"This should never happen\n";
        }
        nPulledFramesThisIteraton++;
        nTotalPulledFrames++;
        {
            //const auto decode_delay=std::chrono::steady_clock::now()-before;
            //std::cout<<"Decode delay:"<<((float)std::chrono::duration_cast<std::chrono::microseconds>(decode_delay).count()/1000.0f)<<" ms\n";
            // check if we have the (put in) time stamp for this frame
            if(feedDecoderTimePoints.size()>=nTotalPulledFrames){
                const auto thisFrameFeedDecoderTimePoint=feedDecoderTimePoints.at(nTotalPulledFrames-1);
                const auto x_delay=std::chrono::steady_clock::now()-thisFrameFeedDecoderTimePoint;
                std::cout<<"(True) decode delay:"<<((float)std::chrono::duration_cast<std::chrono::microseconds>(x_delay).count()/1000.0f)<<" ms\n";
            }
        }
        // tmp test disable
        x_push_into_filter_graph(dpo,frame);

        if (frames == 0 || --frames == 0)
            ret = -1;
        // we got a frame, return (it is 100% sequential this way)
        return 0;
    fail:
        av_frame_free(&frame);
        av_frame_free(&sw_frame);
        av_freep(&buffer_for_something);
        if (ret < 0)
            return ret;
    }
    return 0;
}

// Copied almost directly from ffmpeg filtering_video.c example
static int init_filters(const AVStream * const stream,
                        const AVCodecContext * const dec_ctx,
                        const char * const filters_descr)
{
    char args[512];
    int ret = 0;
    const AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVRational time_base = stream->time_base;
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_DRM_PRIME, AV_PIX_FMT_NONE };

    filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
            time_base.num, time_base.den,
            dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                       args, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                       NULL, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        goto end;
    }

    ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts,
                              AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
        goto end;
    }

    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */

    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filters_descr,
                                    &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

void usage()
{
    fprintf(stderr, "Usage: hello_drmprime [-l loop_count] [-f <frames>] [-o yuv_output_file] [--deinterlace] [--keyboard] <input file> [<input_file> ...]\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    AVFormatContext *input_ctx = NULL;
    int video_stream, ret;
    AVStream *video = NULL;
    AVCodecContext *decoder_ctx = NULL;
    const AVCodec *decoder = NULL;
    AVPacket packet;
    enum AVHWDeviceType type;
    const char * in_file;
    char * const * in_filelist;
    unsigned int in_count;
    unsigned int in_n = 0;
    const char * hwdev = "drm";
    int i;
    drmprime_out_env_t * dpo;
    long loop_count = 1;
    long frame_count = -1;
    const char * out_name = NULL;
    bool wants_deinterlace = false;
    //
    bool feed_frames_on_keyboard_klick=false;
    bool drop_frames=false;


    {
        char * const * a = argv + 1;
        int n = argc - 1;

        while (n-- > 0 && a[0][0] == '-') {
            const char *arg = *a++;
            char *e;

            if (strcmp(arg, "-l") == 0 || strcmp(arg, "--loop") == 0) {
                if (n == 0)
                    usage();
                loop_count = strtol(*a, &e, 0);
                if (*e != 0)
                    usage();
                --n;
                ++a;
            }
            else if (strcmp(arg, "-f") == 0 || strcmp(arg, "--frames") == 0) {
                if (n == 0)
                    usage();
                frame_count = strtol(*a, &e, 0);
                if (*e != 0)
                    usage();
                --n;
                ++a;
            }
            else if (strcmp(arg, "-o") == 0) {
                if (n == 0)
                    usage();
                out_name = *a;
                --n;
                ++a;
            }
            else if (strcmp(arg, "--deinterlace") == 0) {
                wants_deinterlace = true;
            }
            else if (strcmp(arg, "--keyboard") == 0) {
                feed_frames_on_keyboard_klick=true;
                std::cout<<"Feeding frames only on keyboard input enabled\n";
            }else if(strcmp(arg,"--drop_frames")==0){
                drop_frames=true;
                std::cout<<"Drop-frames enabled\n";
            }
            else
                break;
        }

        // Last args are input files
        if (n < 0)
            usage();

        in_filelist = a;
        in_count = n + 1;
        loop_count *= in_count;
    }

    type = av_hwdevice_find_type_by_name(hwdev);
    if (type == AV_HWDEVICE_TYPE_NONE) {
        fprintf(stderr, "Device type %s is not supported.\n", hwdev);
        fprintf(stderr, "Available device types:");
        while((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
            fprintf(stderr, " %s", av_hwdevice_get_type_name(type));
        fprintf(stderr, "\n");
        return -1;
    }

    dpo = drmprime_out_new(drop_frames);
    if (dpo == NULL) {
        fprintf(stderr, "Failed to open drmprime output\n");
        return 1;
    }

    /* open the file to dump raw data */
    if (out_name != NULL) {
        std::cout<<"Opening output fle:"<<std::string(out_name)<<"\n";
        if ((output_file = fopen(out_name, "w+")) == NULL) {
            fprintf(stderr, "Failed to open output file %s: %s\n", out_name, strerror(errno));
            return -1;
        }
    }

loopy:
    in_file = in_filelist[in_n];
    if (++in_n >= in_count)
        in_n = 0;

    /* open the input file */
    if (avformat_open_input(&input_ctx, in_file, NULL, NULL) != 0) {
        fprintf(stderr, "Cannot open input file '%s'\n", in_file);
        return -1;
    }

    if (avformat_find_stream_info(input_ctx, NULL) < 0) {
        fprintf(stderr, "Cannot find input stream information.\n");
        return -1;
    }

    /* find the video stream information */
    ret = av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1,(AVCodec**) &decoder, 0);
    if (ret < 0) {
        fprintf(stderr, "Cannot find a video stream in the input file\n");
        return -1;
    }
    video_stream = ret;

    if (decoder->id == AV_CODEC_ID_H264) {
        if ((decoder = avcodec_find_decoder_by_name("h264_v4l2m2m")) == NULL) {
            fprintf(stderr, "Cannot find the h264 v4l2m2m decoder\n");
            return -1;
        }
        hw_pix_fmt = AV_PIX_FMT_DRM_PRIME;
    }
    else {
        for (i = 0;; i++) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
            if (!config) {
                fprintf(stderr, "Decoder %s does not support device type %s.\n",
                        decoder->name, av_hwdevice_get_type_name(type));
                return -1;
            }
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == type) {
                hw_pix_fmt = config->pix_fmt;
                break;
            }
        }
    }

    if (!(decoder_ctx = avcodec_alloc_context3(decoder)))
        return AVERROR(ENOMEM);

    video = input_ctx->streams[video_stream];
    if (avcodec_parameters_to_context(decoder_ctx, video->codecpar) < 0)
        return -1;

    decoder_ctx->get_format  = get_hw_format;

    if (hw_decoder_init(decoder_ctx, type) < 0)
        return -1;

    // Consti10
    //decoder_ctx->thread_count = 3;
    decoder_ctx->thread_count = 1;

    if ((ret = avcodec_open2(decoder_ctx, decoder, NULL)) < 0) {
        fprintf(stderr, "Failed to open codec for stream #%u\n", video_stream);
        return -1;
    }

    if (wants_deinterlace) {
        if (init_filters(video, decoder_ctx, "deinterlace_v4l2m2m") < 0) {
            fprintf(stderr, "Failed to init deinterlace\n");
            return -1;
        }
    }

    /* actual decoding and dump the raw data */
    frames = frame_count;
    const auto decodingStart=std::chrono::steady_clock::now();
    int nFeedFrames=0;
    while (ret >= 0) {
        if ((ret = av_read_frame(input_ctx, &packet)) < 0)
            break;

        if (video_stream == packet.stream_index){
            if(feed_frames_on_keyboard_klick){
                // wait for a keyboard input
                printf("Press ENTER key to Feed new frame\n");
                auto tmp=getchar();
                // change LED, feed one new frame
                switch_led_on_off();
            }
            ret = decode_and_wait_for_frame(decoder_ctx, dpo, &packet);

            nFeedFrames++;
            const uint64_t runTimeMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-decodingStart).count();
            const double runTimeS=runTimeMs/1000.0f;
            const double fps=nFeedFrames/runTimeS;
            std::stringstream ss;
            ss<<"Fake fps:"<<fps<<"\n";
            std::cout<<ss.str();
        }

        av_packet_unref(&packet);
    }

    /* flush the decoder */
    packet.data = NULL;
    packet.size = 0;
    ret = decode_and_wait_for_frame(decoder_ctx, dpo, &packet);
    av_packet_unref(&packet);

    if (output_file)
        fclose(output_file);
    avfilter_graph_free(&filter_graph);
    avcodec_free_context(&decoder_ctx);
    avformat_close_input(&input_ctx);

    if (--loop_count > 0)
        goto loopy;

    drmprime_out_delete(dpo);

    return 0;
}
