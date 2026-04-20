/*
 * Copyright (c) 2012 Stefano Sabatini
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

#include "libavcodec/avcodec.h"
//#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libavutil/samplefmt.h"
#include "../main.h"

typedef struct AVDecoderContext_
{
	AVElement_t*p_video;
	AVFormatContext *fmt_ctx;
	AVBitStreamFilterContext *h264bsfc;
	int video_stream_idx, audio_stream_idx;
	int64_t i_pts;
}AVDecoderContext_t;

//#define _DEBUG_OUT
#ifdef _DEBUG_OUT
int i_framen = 0;
#endif

static void decode_packet(AVDecoderContext_t *p_avdec, AVPacket *pkt)
{
	AVElement_t*p_video = p_avdec->p_video;

	AVStream *p_stream = p_avdec->fmt_ctx->streams[pkt->stream_index];
	recycle_block_buffer_t *p_fifo = NULL;
	block_t block_in;
	block_in.i_dts = pkt->dts != AV_NOPTS_VALUE? pkt->dts*p_stream->time_base.num*1000000/p_stream->time_base.den + 10000000 : 0;
	block_in.i_pts = pkt->pts != AV_NOPTS_VALUE? pkt->pts*p_stream->time_base.num*1000000/p_stream->time_base.den + 10000000 : 0;
	block_in.i_length = pkt->duration*p_stream->time_base.num*1000000/p_stream->time_base.den;
	block_in.i_buffer = pkt->size;
	block_in.p_buffer = pkt->data;
	block_in.i_flags  = 0;
	block_in.i_extra = 0;

	if(pkt->stream_index == p_avdec->video_stream_idx)
	{
		p_fifo = &p_video->video_fifo;

#if 0//def _DEBUG_OUT
		//if(p_encoder->i_id == 0)
		if(block_in.i_buffer > 0)
		{
			static int i_framen = 0;
			FILE *fp = fopen("tr.mpv", !i_framen? "wb":"ab");
			fwrite(block_in.p_buffer, 1, block_in.i_buffer*2/3, fp);
			fclose(fp);
			i_framen++;
		}
#endif
		//static int64_t pts = 0;
		//printf("%ld d:%ld\n", block_in.i_pts, block_in.i_pts-pts);
		//pts = block_in.i_pts;

		//if(!p_avdec->i_pts)
		//p_avdec->i_pts = block_in.i_pts;
		//block_in.i_pts = p_avdec->i_pts;
		//p_avdec->i_pts += p_video->i_frame_period;
		p_video->i_video_pts = block_in.i_pts;
	}

	//printf("%s frame dts:%ld size:%d\n", pkt->stream_index == p_avdec->video_stream_idx? "video":"audio", block_in.i_dts, pkt->size);

	if(p_video->i_video_codec != ES_V_RAW)
	{
		while(!p_video->b_die)
		{
			int ret = BlockBufferWrite( p_fifo, &block_in);
			if(ret > 0)
				break;
			usleep(1000);
		}
	}
	else
	{
		while(!p_video->b_die)
		{
			int ret = copybuffer(p_video, &block_in);
			if(ret)
			{
			    break;
			}
			usleep(1000);
		}
	}
}

static int open_codec_context(int *stream_idx,
                              AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret, stream_index;

    ret = av_find_best_stream(fmt_ctx, type, *stream_idx, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find %s stream in input file\n",
                av_get_media_type_string(type));
        return ret;
    } else {
        stream_index = ret;
        //st = fmt_ctx->streams[stream_index];
        *stream_idx = stream_index;
    }

    return 0;
}

void av_loop (AVElement_t*p_video, char *psz_url)
{
	AVStream *video_stream=NULL, *audio_stream=NULL;
	AVDecoderContext_t *p_avdec = (AVDecoderContext_t *)malloc(sizeof(AVDecoderContext_t ));

	program_t *p_program = p_video->p_program;

	memset(p_avdec, 0, sizeof(AVDecoderContext_t));

	p_avdec->p_video = p_video;
	p_avdec->audio_stream_idx = -1;
	p_avdec->video_stream_idx = -1;

	/* register all formats and codecs */
    avformat_network_init();

	av_log_set_level(AV_LOG_QUIET);//AV_LOG_QUIET AV_LOG_DEBUG AV_LOG_INFO

	//int64_t t1 = mdate();

	AVDictionary* options = NULL;

	if (avformat_open_input(&p_avdec->fmt_ctx, psz_url, NULL, &options) < 0) {
        fprintf(stderr, "Could not open source file %s\n", psz_url);
        goto end;
    }


	/* retrieve stream information */
	if (avformat_find_stream_info(p_avdec->fmt_ctx, NULL) < 0) {
		fprintf(stderr, "Could not find stream information\n");
		goto end;
	}

	int i_need_number = 0;//p_program->i_program_number;
	int i_program = 0;//MAX(0, MIN(i_need_number, p_avdec->fmt_ctx->nb_programs-1));
	for(int i = 0; i < p_avdec->fmt_ctx->nb_programs; i++)
	{
		printf("ids:%d program:%d ", i, p_avdec->fmt_ctx->programs[i]->program_num);
		if(i_need_number == p_avdec->fmt_ctx->programs[i]->program_num)
		{
			i_program = i;
			printf(" == %d Set by user\n", i_need_number);
		}
		else
		{
			printf("\n");
		}

	}

	for(int i = 0; i < p_avdec->fmt_ctx->nb_streams; i++)
	{
		AVProgram *first = av_find_program_from_stream(p_avdec->fmt_ctx, NULL, i);
		if(!first || first->program_num == p_avdec->fmt_ctx->programs[i_program]->program_num )
		{
			if(p_avdec->video_stream_idx < 0 && p_avdec->fmt_ctx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO)
			{
				p_avdec->video_stream_idx = i;
			}
			else if( p_avdec->audio_stream_idx < 0 && p_avdec->fmt_ctx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO)
			{
				p_avdec->audio_stream_idx = i;
			}
		}
	}

	if (p_avdec->video_stream_idx >= 0 ||
		open_codec_context(&p_avdec->video_stream_idx,
		p_avdec->fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0)
	{
		video_stream = p_avdec->fmt_ctx->streams[p_avdec->video_stream_idx];
		p_video->i_width  = video_stream->codecpar->width;
		p_video->i_height = video_stream->codecpar->height;
		double f_fps = 0;
		if(video_stream->avg_frame_rate.den > 0)
		{
			f_fps = (double)video_stream->avg_frame_rate.num/video_stream->avg_frame_rate.den;
			//p_video->i_frame_period = 1000000/f_fps;
		}

		printf("id:%d %dx%d fps:%.3f\n", p_avdec->video_stream_idx, p_video->i_width, p_video->i_height, f_fps);

		switch(video_stream->codecpar->codec_id)
		{
		case AV_CODEC_ID_H264:
			p_video->i_video_codec = ES_V_H264;
			break;
		case AV_CODEC_ID_H265:
			p_video->i_video_codec = ES_V_H265;
			break;
		case AV_CODEC_ID_MPEG4:
			p_video->i_video_codec = ES_V_MPG4;
			break;
		case AV_CODEC_ID_RAWVIDEO:
			p_video->i_video_codec = ES_V_RAW;
			break;
		default:
			printf("unkown codec_id:%d\n", video_stream->codecpar->codec_id);
			p_video->i_video_codec = 0;//p_avdec->video_dec_ctx->codec_id + ES_V_MAX;
			break;
		}

		if( p_video->i_video_codec > 0 && p_video->i_video_codec != ES_V_RAW)
		{
			BlockBufferInit( &p_video->video_fifo, 32, 8*1024*1024, 0);
			pthread_create(&p_video->decode_thid,NULL,video_decoder, (void *)p_video);
		}

	}

    /* dump input information to stderr */
	av_dump_format(p_avdec->fmt_ctx, 0, psz_url, 0);

    if (!video_stream)
	{
        fprintf(stderr, "Could not find audio or video stream in the input, aborting\n");
        goto end;
    }

	AVPacket pkt;

    /* initialize packet, set data to NULL, let the demuxer fill it */
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

#ifdef _DEBUG_OUT
    i_framen = 0;
#endif

	double i_send_offset = 0;
	int64_t i_last_send_time = 0;

    while(!p_video->b_die)
    {
		if(av_read_frame(p_avdec->fmt_ctx, &pkt) >= 0)
		{
			AVPacket orig_pkt = pkt;
			if((pkt.stream_index == p_avdec->video_stream_idx && p_video->i_video_codec > 0))
			decode_packet(p_avdec, &pkt);
			av_packet_unref(&orig_pkt);
		}
		else //if(0)
		{
#ifdef _DEBUG
			static int times = 0;
			printf(" ---------%d---------CLOSED\n", times++);
#endif
			//break;//test
			avformat_close_input(&p_avdec->fmt_ctx);
			usleep(1000);
			if (avformat_open_input(&p_avdec->fmt_ctx, psz_url, NULL, &options) < 0) {
				fprintf(stderr, "Could not open source file %s\n", psz_url);
				break;
			}

			i_last_send_time = mdate();
		}

		i_send_offset += p_video->i_frame_period;

		int64_t i_current = mdate();
		if(!i_last_send_time)
		i_last_send_time = i_current;
		if(i_current > i_last_send_time)
		i_send_offset -= (i_current - i_last_send_time);
		i_last_send_time = i_current;

		i_send_offset= MIN(100000, MAX(i_send_offset, -100000));

		if(i_send_offset > 1)
		msleep( (int)i_send_offset);

    }

	//if(p_avdec->h264bsfc)
	//av_bitstream_filter_close(p_avdec->h264bsfc);

	avformat_close_input(&p_avdec->fmt_ctx);

	free(p_avdec);

	if(p_video->decode_thid)
	{
		pthread_join(p_video->decode_thid, NULL);
		BlockBufferClean( &p_video->video_fifo);
	}

    return;

end:

	if(p_avdec->fmt_ctx)
	avformat_close_input(&p_avdec->fmt_ctx);

	free(p_avdec);

	return;
}
