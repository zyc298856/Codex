/*
 * Copyright (c) 2016-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "NvUtils.h"
#include <fstream>
#include <iostream>
#include <linux/videodev2.h>
#include <malloc.h>
#include <sstream>
#include <string.h>
#include <fcntl.h>
#include <poll.h>

#include <cuda.h>
#include <cuda_runtime.h>
#include <cuda_profiler_api.h>

extern "C"
{
#include "../main.h"
}

#include "nvbuf_utils.h"
#include "NvUtils.h"
#include "video_encode.h"
#include "cudaEGL.h"
#include "NvCudaProc.h"
//#include "hist.h"

#define TEST_ERROR(cond, str, label) if(cond) { \
                                        cerr << str << endl; \
                                        error = 1; \
                                        goto label; }

#define TEST_PARSE_ERROR(cond, label) if(cond) { \
    cerr << "Error parsing runtime parameter changes string" << endl; \
    goto label; }

#define IS_DIGIT(c) (c >= '0' && c <= '9')
#define MICROSECOND_UNIT 1000000

using namespace std;


/**
  * Abort on error.
  *
  * @param ctx : Encoder context
  */
static void
abort(context_t *ctx)
{
    ctx->got_error = true;
    ctx->enc->abort();
}

static const char*
get_pixfmt_string(uint32_t pixfmt)
{
    switch (pixfmt)
    {
        case V4L2_PIX_FMT_YUV420M:          return "V4L2_PIX_FMT_YUV420M";
        case V4L2_PIX_FMT_NV12M:            return "V4L2_PIX_FMT_NV12M";
        case V4L2_PIX_FMT_YUV444M:          return "V4L2_PIX_FMT_YUV444M";
        case V4L2_PIX_FMT_NV24M:            return "V4L2_PIX_FMT_NV24M";
        case V4L2_PIX_FMT_P010M:            return "V4L2_PIX_FMT_P010M";
        case V4L2_PIX_FMT_NV24_10LE:        return "V4L2_PIX_FMT_NV24_10LE";
        case V4L2_PIX_FMT_H264:             return "V4L2_PIX_FMT_H264";
        case V4L2_PIX_FMT_H265:             return "V4L2_PIX_FMT_H265";
        case V4L2_PIX_FMT_VP8:              return "V4L2_PIX_FMT_VP8";
        case V4L2_PIX_FMT_VP9:              return "V4L2_PIX_FMT_VP9";
        default:                            return "";
    }
}


int copybuffer(AVElement_t*p_video, block_t *p_block)
{
	uint8_t *p_outframe = FrameBufferPut( &p_video->m_preframes );
	if(p_outframe)
	{
	    cudaMemcpy(p_outframe, p_block->p_buffer, p_block->i_buffer*2/3, cudaMemcpyHostToDevice);
	    FrameBufferPutdown(&p_video->m_preframes, p_block->i_pts,0);
	    return 1;
	}
	return 0;
}
/**
  * Encoder capture-plane deque buffer callback function.
  *
  * @param v4l2_buf      : v4l2 buffer
  * @param buffer        : NvBuffer
  * @param shared_buffer : shared NvBuffer
  * @param arg           : context pointer
  */
static bool
encoder_capture_plane_dq_callback(struct v4l2_buffer *v4l2_buf, NvBuffer * buffer,
                                  NvBuffer * shared_buffer, void *arg)
{
    context_t *ctx = (context_t *) arg;
    NvVideoEncoder *enc = ctx->enc;
    AVElement_t*p_video = (AVElement_t *)ctx->p_top;

    pthread_setname_np(pthread_self(), "EncCapPlane");
    static uint32_t num_encoded_frames = 0;
    static int64_t last_time = 0;

    if (v4l2_buf == NULL)
    {
        cout << "Error while dequeing buffer from output plane" << endl;
        abort(ctx);
        return false;
    }

    /* Received EOS from encoder. Stop dqthread. */
    if (buffer->planes[0].bytesused == 0)
    {
        cout << "Got 0 size buffer in capture \n";
        return false;
    }

	block_t block_out;
	block_out.i_flags = 0;
	block_out.i_buffer = buffer->planes[0].bytesused;
	block_out.p_buffer = ctx->p_buffer; //(uint8_t *) buffer->planes[0].data;
	block_out.i_length = p_video->i_frame_period;
	block_out.i_pts = v4l2_buf->timestamp.tv_sec*MICROSECOND_UNIT + v4l2_buf->timestamp.tv_usec;
	block_out.i_dts = block_out.i_pts;
	block_out.i_extra = 0;


	int i_header = 0;
	block_t *p_obj = BlockBufferGet(&p_video->video_object);
	if(p_obj)
	{
	    uint8_t user_data[24] = {0, 0, 0, 1, 6, 5, 0x54, 0x8f, 0x83, 0x97, 0xf3, 0x23, 0x97, 0x4b,
	                            0xb7, 0xc7, 0x4f, 0x3a, 0xb5, 0x6e, 0x89, 0x52, 0, 0 };

		uint8_t *p_buffer =  ctx->p_buffer;
		user_data[22] = p_obj->i_buffer>>8;
		user_data[23] = p_obj->i_buffer& 0xff;
		memcpy(p_buffer, user_data, 24);
		memcpy(p_buffer+24, p_obj->p_buffer, p_obj->i_buffer);
		p_buffer[24 + p_obj->i_buffer] = 0x80;
		i_header = 24 + p_obj->i_buffer+1;
		BlockBufferGetOff(&p_video->video_object);
	}

	memcpy(ctx->p_buffer+i_header, buffer->planes[0].data, buffer->planes[0].bytesused);
	block_out.i_buffer += i_header;

#ifdef _DEBUG
	//if(p_encoder->i_id == 0)
    //if(block_out.i_pts > 0)
	{
		static int64_t i_pts = 0;
		FILE *fp = fopen("tr.h264", !i_pts? "wb":"ab");
		fwrite(block_out.p_buffer, 1, block_out.i_buffer, fp);
		fclose(fp);
		i_pts = 1;
	}
#endif

#if 0
	//if(p_encoder->i_id == 0)
	{
		static int64_t last_time = 0;
		static int64_t i_pts = 0;
		//int i_depth = BlockBufferGetNum(&p_video->video_buffer);
		FILE *fp = fopen("tr2.txt", !i_pts? "wt":"at");
		fprintf(fp, "index:%2d flags:%d|%2d size:%6d dts:%ld pts:%ld d:%ld pd:%ld esp:%ld\n",
				v4l2_buf->index, v4l2_buf->flags, v4l2_buf->sequence,
				block_out.i_buffer, block_out.i_dts, block_out.i_pts, block_out.i_dts - i_pts,
				block_out.i_pts - block_out.i_dts, mdate() - last_time);
		fclose(fp);
		last_time = mdate();
		i_pts = block_out.i_dts;
	}
#endif

	BlockBufferOverwrite(&p_video->video_buffer, &block_out);

	num_encoded_frames++;
	if(num_encoded_frames%30 == 0)
	{
		p_video->i_output_fps = num_encoded_frames*1000000.0/(float)(mdate()-last_time);
		last_time = mdate();
		num_encoded_frames = 0;
	}

    /* encoder qbuffer for capture plane */
    if (enc->capture_plane.qBuffer(*v4l2_buf, NULL) < 0)
    {
        cerr << "Error while Qing buffer at capture plane" << endl;
        abort(ctx);
        return false;
    }

    return true;
}

/**
  * Set encoder context defaults values.
  *
  * @param ctx : Encoder context
  */
static void
set_defaults(context_t * ctx)
{
	AVElement_t*p_video = (AVElement_t *)ctx->p_top;
	program_t *p_program = p_video->p_program;

    ctx->width = p_video->i_width*(p_video->b_compare+1);
    ctx->height = p_video->i_height;

    ctx->raw_pixfmt = V4L2_PIX_FMT_YUV420M;
    ctx->bitrate = p_program->i_video_bitrate*1000;
    ctx->peak_bitrate = 0;
	if(p_program->i_video_codec == ES_V_H264)
	{
		ctx->encoder_pixfmt = V4L2_PIX_FMT_H264;
		if(p_program->i_depth == 10)
			ctx->profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10;
		else
			ctx->profile = V4L2_MPEG_VIDEO_H264_PROFILE_MAIN;
	}
	else if(p_program->i_video_codec == ES_V_H265)
	{
		ctx->encoder_pixfmt = V4L2_PIX_FMT_H265;
		if(p_program->i_depth == 10)
			ctx->profile = V4L2_MPEG_VIDEO_H265_PROFILE_MAIN10;
		else
			ctx->profile = V4L2_MPEG_VIDEO_H265_PROFILE_MAIN;
	}

    ctx->profile = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
    ctx->ratecontrol = V4L2_MPEG_VIDEO_BITRATE_MODE_CBR;
    ctx->iframe_interval = 30;
	if(p_program->i_rc_method)
	    ctx->ratecontrol = V4L2_MPEG_VIDEO_BITRATE_MODE_CBR;
	else
	    ctx->ratecontrol = V4L2_MPEG_VIDEO_BITRATE_MODE_VBR;

    ctx->iframe_interval = p_program->i_keyint_max;

    ctx->externalRPS = false;
    ctx->enableGDR = false;
    ctx->enableROI = false;
    ctx->bnoIframe = false;
    ctx->bGapsInFrameNumAllowed = false;
    ctx->bReconCrc = false;
    ctx->enableLossless = false;
    ctx->nH264FrameNumBits = 0;
    ctx->nH265PocLsbBits = 0;
    ctx->idr_interval = p_program->i_keyint_max;
    ctx->level = -1;
    ctx->fps_n = 30;
    ctx->fps_d = 1;
    ctx->gdr_start_frame_number = 0xffffffff;
    ctx->gdr_num_frames = 0xffffffff;
    ctx->gdr_out_frame_number = 0xffffffff;
    ctx->num_b_frames = (uint32_t) -1;
    ctx->nMinQpI = (uint32_t)QP_RETAIN_VAL;
    ctx->nMaxQpI = (uint32_t)QP_RETAIN_VAL;
    ctx->nMinQpP = (uint32_t)QP_RETAIN_VAL;
    ctx->nMaxQpP = (uint32_t)QP_RETAIN_VAL;
    ctx->nMinQpB = (uint32_t)QP_RETAIN_VAL;
    ctx->nMaxQpB = (uint32_t)QP_RETAIN_VAL;
    ctx->use_gold_crc = false;
    ctx->pBitStreamCrc = NULL;
    ctx->externalRCHints = false;
    ctx->input_metadata = false;
    ctx->sMaxQp = 51;
    ctx->stats = false;
    ctx->stress_test = 1;
    ctx->output_memory_type = V4L2_MEMORY_DMABUF;
    ctx->capture_memory_type = V4L2_MEMORY_MMAP;
    ctx->cs = V4L2_COLORSPACE_SMPTE170M;
    ctx->copy_timestamp = true;
    ctx->sar_width = 0;
    ctx->sar_height = 0;
    ctx->start_ts = 0;
    ctx->max_perf = 0;
    ctx->blocking_mode = 1;
    ctx->startf = 0;
    ctx->endf = 0;
    ctx->num_output_buffers = 6;
    ctx->num_frames_to_encode = -1;
    ctx->poc_type = 0;
    ctx->chroma_format_idc = -1;
    ctx->bit_depth = 8;
    ctx->is_semiplanar = false;
    ctx->insert_sps_pps_at_idr = 1;
    ctx->enable_initQP = false;
    ctx->IinitQP = 0;
    ctx->PinitQP = 0;
    ctx->BinitQP = 0;
    ctx->enable_ratecontrol = true;
    ctx->enable_av1tile = false;
    ctx->log2_num_av1rows = 0;
    ctx->log2_num_av1cols = 0;
    ctx->enable_av1ssimrdo = (uint8_t)-1;
    ctx->disable_av1cdfupdate = (uint8_t)-1;
    ctx->ppe_init_params.enable_ppe = false;
    ctx->ppe_init_params.wait_time_ms = -1;
    ctx->ppe_init_params.feature_flags = V4L2_PPE_FEATURE_NONE;
    ctx->ppe_init_params.enable_profiler = 0;
    ctx->ppe_init_params.taq_max_qp_delta = 5;
    /* TAQ for B-frames is enabled by default */
    ctx->ppe_init_params.taq_b_frame_mode = 1;
}

/**
  * Setup output plane for DMABUF io-mode.
  *
  * @param ctx         : encoder context
  * @param num_buffers : request buffer count
  */
static int
setup_output_dmabuf(context_t *ctx, uint32_t num_buffers )
{
    int ret=0;
    NvBufSurf::NvCommonAllocateParams cParams;
    int fd;
    ret = ctx->enc->output_plane.reqbufs(V4L2_MEMORY_DMABUF,num_buffers);
    if(ret)
    {
        cerr << "reqbufs failed for output plane V4L2_MEMORY_DMABUF" << endl;
        return ret;
    }
    for (uint32_t i = 0; i < ctx->enc->output_plane.getNumBuffers(); i++)
    {
        cParams.width = ctx->width;
        cParams.height = ctx->height;
        cParams.layout = NVBUF_LAYOUT_PITCH;
        switch (ctx->cs)
        {
            case V4L2_COLORSPACE_REC709:
                cParams.colorFormat = ctx->enable_extended_colorformat ?
                    NVBUF_COLOR_FORMAT_YUV420_709_ER : NVBUF_COLOR_FORMAT_YUV420_709;
                break;
            case V4L2_COLORSPACE_SMPTE170M:
            default:
                cParams.colorFormat = ctx->enable_extended_colorformat ?
                    NVBUF_COLOR_FORMAT_YUV420_ER : NVBUF_COLOR_FORMAT_YUV420;
        }
        if (ctx->is_semiplanar)
        {
            cParams.colorFormat = NVBUF_COLOR_FORMAT_NV12;
        }
        if (ctx->encoder_pixfmt == V4L2_PIX_FMT_H264)
        {
            if (ctx->enableLossless)
            {
                if (ctx->is_semiplanar)
                    cParams.colorFormat = NVBUF_COLOR_FORMAT_NV24;
                else
                    cParams.colorFormat = NVBUF_COLOR_FORMAT_YUV444;
            }
        }
        else if (ctx->encoder_pixfmt == V4L2_PIX_FMT_H265)
        {
            if (ctx->chroma_format_idc == 3)
            {
                if (ctx->is_semiplanar)
                    cParams.colorFormat = NVBUF_COLOR_FORMAT_NV24;
                else
                    cParams.colorFormat = NVBUF_COLOR_FORMAT_YUV444;

                if (ctx->bit_depth == 10)
                    cParams.colorFormat = NVBUF_COLOR_FORMAT_NV24_10LE;
            }
            if (ctx->profile == V4L2_MPEG_VIDEO_H265_PROFILE_MAIN10 && (ctx->bit_depth == 10))
            {
                cParams.colorFormat = NVBUF_COLOR_FORMAT_NV12_10LE;
            }
        }
        cParams.memtag = NvBufSurfaceTag_VIDEO_ENC;
        cParams.memType = NVBUF_MEM_SURFACE_ARRAY;
        /* Create output plane fd for DMABUF io-mode */
        ret = NvBufSurf::NvAllocate(&cParams, 1, &fd);
        if(ret < 0)
        {
            cerr << "Failed to create NvBuffer" << endl;
            return ret;
        }

        ctx->output_plane_fd[i]=fd;
    }


    return ret;
}

static int
setup_capture_dmabuf(context_t *ctx, uint32_t num_buffers )
{
    NvBufSurfaceAllocateParams cParams = {{0}};
    NvBufSurface *surface = 0;
    int ret=0;

    ret = ctx->enc->capture_plane.reqbufs(V4L2_MEMORY_DMABUF,num_buffers);
    if(ret)
    {
        cerr << "reqbufs failed for capture plane V4L2_MEMORY_DMABUF" << endl;
        return ret;
    }

    for (uint32_t i = 0; i < ctx->enc->capture_plane.getNumBuffers(); i++)
    {
        ret = ctx->enc->capture_plane.queryBuffer(i);
        if (ret)
        {
            cerr << "Error in querying for " << i << "th buffer plane" << endl;
            return ret;
        }

        NvBuffer *buffer = ctx->enc->capture_plane.getNthBuffer(i);

        cParams.params.memType = NVBUF_MEM_HANDLE;
        cParams.params.size = buffer->planes[0].length;
        cParams.memtag = NvBufSurfaceTag_VIDEO_ENC;

        ret = NvBufSurfaceAllocate(&surface, 1, &cParams);
        if(ret < 0)
        {
            cerr << "Failed to create NvBuffer" << endl;
            return ret;
        }
        surface->numFilled = 1;

        ctx->capture_plane_fd[i] = surface->surfaceList[0].bufferDesc;
    }

    return ret;
}


/**
  * Performs CUDA Operations on egl image.
  *
  * @param image : EGL image
  */
static void
Handle_EGLImage(AVElement_t*p_video, EGLImageKHR image, int pitchc, uint8_t *p_frame)
{
    CUresult status;
    CUeglFrame eglFrame;
    CUgraphicsResource pResource = NULL;


    cudaFree(0);
    status = cuGraphicsEGLRegisterImage(&pResource, image,
                CU_GRAPHICS_MAP_RESOURCE_FLAGS_NONE);
    if (status != CUDA_SUCCESS)
    {
        printf("cuGraphicsEGLRegisterImage failed: %d, cuda process stop\n",
                        status);
        return;
    }

    status = cuGraphicsResourceGetMappedEglFrame(&eglFrame, pResource, 0, 0);
    if (status != CUDA_SUCCESS)
    {
        printf("cuGraphicsSubResourceGetMappedArray failed\n");
    }

    status = cuCtxSynchronize();
    if (status != CUDA_SUCCESS)
    {
        printf("cuCtxSynchronize failed\n");
    }

    if (eglFrame.frameType == CU_EGL_FRAME_TYPE_PITCH)
    {
    	int i_width = p_video->i_width*(p_video->b_compare+1);
    	cudaMemcpy2D((unsigned char*)eglFrame.frame.pPitch[0], eglFrame.pitch, p_frame, i_width, i_width, p_video->i_height, cudaMemcpyDeviceToDevice);
    	cudaMemcpy2D((unsigned char*)eglFrame.frame.pPitch[1], pitchc, p_frame+i_width*p_video->i_height, i_width/2, i_width/2, p_video->i_height/2, cudaMemcpyDeviceToDevice);
    	cudaMemcpy2D((unsigned char*)eglFrame.frame.pPitch[2], pitchc, p_frame+i_width*p_video->i_height*5/4, i_width/2, i_width/2, p_video->i_height/2, cudaMemcpyDeviceToDevice);
    }

    status = cuCtxSynchronize();
    if (status != CUDA_SUCCESS)
    {
        printf("cuCtxSynchronize failed after memcpy\n");
    }

    status = cuGraphicsUnregisterResource(pResource);
    if (status != CUDA_SUCCESS)
    {
        printf("cuGraphicsEGLUnRegisterResource failed: %d\n", status);
    }
}

static int proprocessing(AVElement_t*p_video, int buffer_fd, uint8_t *p_frame)
{
    int ret = 0;
    NvBufSurface *nvbuf_surf = NULL;

    ret = NvBufSurfaceFromFd(buffer_fd, (void**)(&nvbuf_surf));
    if (ret < 0)
    {
        cerr << "NvBufSurfaceFromFd failed!" << endl;
        return -1;
    }

    if (nvbuf_surf->surfaceList[0].mappedAddr.eglImage == NULL)
    {
        if (NvBufSurfaceMapEglImage(nvbuf_surf, 0) != 0)
        {
            cerr << "Unable to map EGL Image" << endl;
            return -1;
        }
    }

    EGLImageKHR eglimg = nvbuf_surf->surfaceList[0].mappedAddr.eglImage;
    if (eglimg == NULL)
    {
        cerr << "Error while mapping dmabuf fd to EGLImage" << endl;
        return -1;
    }

    /* Map EGLImage to CUDA buffer, and call CUDA kernel to
       draw a 32x32 pixels black box on left-top of each frame */
    Handle_EGLImage(p_video, eglimg, nvbuf_surf->surfaceList[0].planeParams.pitch[1], p_frame);

    /* Destroy EGLImage */
    if (NvBufSurfaceUnMapEglImage(nvbuf_surf, 0) != 0)
    {
        cerr << "Unable to unmap EGL Image" << endl;
        return -1;
    }


    return 0;
}

/**
  * Encode processing function for blocking mode.
  *
  * @param ctx : Encoder context
  * @param eos : end of stream
  */
static int encoder_proc_blocking(context_t &ctx, bool eos)
{
    int ret = 0;
    AVElement_t*p_video = (AVElement_t *)ctx.p_top;
    int64_t i_pts = 10000000;

    ctx.p_buffer = (uint8_t*)malloc(1024*1024);

    p_video->i_output += 1;
    /* Keep reading input till EOS is reached */
    while (!ctx.got_error && !ctx.enc->isInError() && !eos) {

    	recycle_frame_t *encframes = &p_video->m_encframes;

		uint8_t *p_frame = FrameBufferGet( encframes, &i_pts  );
		if(!p_frame && !p_video->b_die)
		{
			usleep(1000);
			continue;
		}

		//printf("input:%ld\n", i_pts);
		//static int64_t pts = 0;
		//printf("%ld d:%ld\n", i_pts, i_pts-pts);
		//pts = i_pts;

		struct v4l2_buffer v4l2_output_buf;
		struct v4l2_plane output_planes[MAX_PLANES];
		NvBuffer *outplane_buffer = NULL;

		memset(&v4l2_output_buf, 0, sizeof(v4l2_output_buf));
		memset(output_planes, 0, sizeof(output_planes));
		v4l2_output_buf.m.planes = output_planes;

		/* Dequeue from output plane, fill the frame and enqueue it back again.
		   NOTE: This could be moved out to a different thread as an optimization. */
		if(ctx.input_frames_queued_count < ctx.enc->output_plane.getNumBuffers())
		{
			outplane_buffer = ctx.enc->output_plane.getNthBuffer(ctx.input_frames_queued_count);
			v4l2_output_buf.index = ctx.input_frames_queued_count;

			if(ctx.output_memory_type == V4L2_MEMORY_DMABUF)
			{
				v4l2_output_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
				v4l2_output_buf.memory = V4L2_MEMORY_DMABUF;
				/* Map output plane buffer for memory type DMABUF. */
				ret = ctx.enc->output_plane.mapOutputBuffers(v4l2_output_buf, ctx.output_plane_fd[ctx.input_frames_queued_count]);

				if (ret < 0)
				{
					cerr << "Error while mapping buffer at output plane" << endl;
					abort(&ctx);
					//goto cleanup;
				}
			}
		}
		else
		{
			ret = ctx.enc->output_plane.dqBuffer(v4l2_output_buf, &outplane_buffer, NULL, 10);
			if (ret < 0)
			{
				cerr << "ERROR while DQing buffer at output plane" << endl;
				abort(&ctx);
				return 0;
			}
		}

		if(p_frame)
		{
            proprocessing(p_video, outplane_buffer->planes[0].fd, p_frame);

            FrameBufferGetOff(encframes);

			outplane_buffer->planes[0].bytesused = p_video->i_width*p_video->i_height;
		}

		if (ctx.copy_timestamp)
		{
			/* Set user provided timestamp when copy timestamp is enabled */
			v4l2_output_buf.flags |= V4L2_BUF_FLAG_TIMESTAMP_COPY;
			v4l2_output_buf.timestamp.tv_sec = i_pts / (MICROSECOND_UNIT);
			v4l2_output_buf.timestamp.tv_usec = i_pts % (MICROSECOND_UNIT);
		}

        if(ctx.output_memory_type == V4L2_MEMORY_DMABUF)
        {
            for (uint32_t j = 0 ; j < outplane_buffer->n_planes ; j++)
            {
            	if(p_video->b_die) outplane_buffer->planes[j].bytesused = 0;
            	v4l2_output_buf.m.planes[j].bytesused = outplane_buffer->planes[j].bytesused;
            }
        }

        /* encoder qbuffer for output plane */
        ret = ctx.enc->output_plane.qBuffer(v4l2_output_buf, NULL);
        if (ret < 0)
        {
            cerr << "Error while queueing buffer at output plane" << endl;
            abort(&ctx);
            goto cleanup;
        }

		if(p_video->b_fps_changed)
		{
			int fps_num = p_video->f_fps*1000;
			p_video->b_fps_changed = 0;
			ret = ctx.enc->setFrameRate(fps_num, 1000);
			if (ret < 0)
			{
				cerr << "Could not set framerate" << endl;
				//goto err;
			}
		}

        ctx.input_frames_queued_count++;
        if (v4l2_output_buf.m.planes[0].bytesused == 0)
        {
            cerr << "File read complete." << endl;
            eos = true;
            ctx.got_eos = true;
            return 0;
        }
    }
cleanup:

	free(ctx.p_buffer);
    return -1;
}

int OpenEncoder(void *param)
{
	AVElement_t*p_video = (AVElement_t *)param;
	//program_t *p_program = p_video->p_program;
	context_t ctx;

    int ret = 0;
    int error = 0;

    memset(&ctx, 0, sizeof(context_t));

    ctx.p_top = p_video;

    bool eos = false;

    /* Set default values for encoder context members. */
    set_defaults(&ctx);

    pthread_setname_np(pthread_self(),"EncOutPlane");

    if (ctx.encoder_pixfmt == V4L2_PIX_FMT_H265)
    {
        TEST_ERROR(ctx.width < 144 || ctx.height < 144, "Height/Width should be"
            " > 144 for H.265", cleanup);
    }

    if (ctx.endf) {
        TEST_ERROR(ctx.startf > ctx.endf, "End frame should be greater than start frame", cleanup);
        ctx.num_frames_to_encode = ctx.endf - ctx.startf + 1;
    }


    /* Create NvVideoEncoder object for blocking or non-blocking I/O mode. */
    if (ctx.blocking_mode)
    {
        cout << "Creating Encoder in blocking mode \n";
        ctx.enc = NvVideoEncoder::createVideoEncoder("enc0");
    }

    TEST_ERROR(!ctx.enc, "Could not create encoder", cleanup);

    if (ctx.stats)
    {
        ctx.enc->enableProfiling();
    }

    if (log_level >= LOG_LEVEL_DEBUG)
            cout << "Encode pixel format :" << get_pixfmt_string(ctx.encoder_pixfmt) << endl;

    /* Set encoder capture plane format.
       NOTE: It is necessary that Capture Plane format be set before Output Plane
       format. It is necessary to set width and height on the capture plane as well */
    ret =
        ctx.enc->setCapturePlaneFormat(ctx.encoder_pixfmt, ctx.width,
                                      ctx.height, 2 * 1024 * 1024);
    TEST_ERROR(ret < 0, "Could not set capture plane format", cleanup);

    if (ctx.encoder_pixfmt == V4L2_PIX_FMT_H265)
    {
        switch (ctx.profile)
        {
            case V4L2_MPEG_VIDEO_H265_PROFILE_MAIN10:
            {
                ctx.raw_pixfmt = V4L2_PIX_FMT_P010M;
                ctx.is_semiplanar = true; /* To keep previous execution commands working */
                ctx.bit_depth = 10;
                break;
            }
            case V4L2_MPEG_VIDEO_H265_PROFILE_MAIN:
            {
                if (ctx.is_semiplanar)
                    ctx.raw_pixfmt = V4L2_PIX_FMT_NV12M;
                else
                    ctx.raw_pixfmt = V4L2_PIX_FMT_YUV420M;
                if (ctx.chroma_format_idc == 3)
                {
                    if (ctx.bit_depth == 10 && ctx.is_semiplanar)
                        ctx.raw_pixfmt = V4L2_PIX_FMT_NV24_10LE;
                    if (ctx.bit_depth == 8)
                    {
                        if (ctx.is_semiplanar)
                            ctx.raw_pixfmt = V4L2_PIX_FMT_NV24M;
                        else
                            ctx.raw_pixfmt = V4L2_PIX_FMT_YUV444M;
                    }
                }
            }
                break;
            default:
                ctx.raw_pixfmt = V4L2_PIX_FMT_YUV420M;
        }
    }
    if (ctx.encoder_pixfmt == V4L2_PIX_FMT_H264)
    {
        if (ctx.enableLossless &&
            ctx.profile == V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE)
        {
            if (ctx.is_semiplanar)
                ctx.raw_pixfmt = V4L2_PIX_FMT_NV24M;
            else
                ctx.raw_pixfmt = V4L2_PIX_FMT_YUV444M;
        }
        else if ((ctx.enableLossless &&
            ctx.profile != V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE) ||
            (!ctx.enableLossless && ctx.profile == V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE))
        {
            cerr << "Lossless encoding is supported only for high444 profile\n";
            error = 1;
            goto cleanup;
        }
        else
        {
            if (ctx.is_semiplanar)
                ctx.raw_pixfmt = V4L2_PIX_FMT_NV12M;
            else
                ctx.raw_pixfmt = V4L2_PIX_FMT_YUV420M;
        }
    }

    if (log_level >= LOG_LEVEL_DEBUG)
            cout << "Raw pixel format :" << get_pixfmt_string(ctx.raw_pixfmt) << endl;

    /* Set encoder output plane format */
    ret =
        ctx.enc->setOutputPlaneFormat(ctx.raw_pixfmt, ctx.width,
                                      ctx.height);
    TEST_ERROR(ret < 0, "Could not set output plane format", cleanup);

    if (ctx.num_frames_to_encode)
    {
        ret = ctx.enc->setFramesToEncode(ctx.num_frames_to_encode);
        TEST_ERROR(ret < 0, "Could not set frames to encode", cleanup);
    }

    ret = ctx.enc->setBitrate(ctx.bitrate);
    TEST_ERROR(ret < 0, "Could not set encoder bitrate", cleanup);

    if (ctx.encoder_pixfmt == V4L2_PIX_FMT_H264)
    {
        /* Set encoder profile for H264 format */
        ret = ctx.enc->setProfile(ctx.profile);
        TEST_ERROR(ret < 0, "Could not set encoder profile", cleanup);

        if (ctx.level == (uint32_t)-1)
        {
            ctx.level = (uint32_t)V4L2_MPEG_VIDEO_H264_LEVEL_5_1;
        }

        ret = ctx.enc->setLevel(ctx.level);
        TEST_ERROR(ret < 0, "Could not set encoder level", cleanup);
    }
    else if (ctx.encoder_pixfmt == V4L2_PIX_FMT_H265)
    {
        ret = ctx.enc->setProfile(ctx.profile);
        TEST_ERROR(ret < 0, "Could not set encoder profile", cleanup);

        if (ctx.level != (uint32_t)-1)
        {
            ret = ctx.enc->setLevel(ctx.level);
            TEST_ERROR(ret < 0, "Could not set encoder level", cleanup);
        }

        if (ctx.chroma_format_idc != (uint8_t)-1)
        {
            ret = ctx.enc->setChromaFactorIDC(ctx.chroma_format_idc);
            TEST_ERROR(ret < 0, "Could not set chroma_format_idc", cleanup);
        }
    }

    if (ctx.enable_initQP)
    {
        ret = ctx.enc->setInitQP(ctx.IinitQP, ctx.PinitQP, ctx.BinitQP);
        TEST_ERROR(ret < 0, "Could not set encoder init QP", cleanup);
    }

    if (ctx.enableLossless)
    {
        ret = ctx.enc->setLossless(ctx.enableLossless);
        TEST_ERROR(ret < 0, "Could not set lossless encoding", cleanup);
    }
    else if (!ctx.enable_ratecontrol)
    {
        /* Set constant QP configuration by disabling rate control */
        ret = ctx.enc->setConstantQp(ctx.enable_ratecontrol);
        TEST_ERROR(ret < 0, "Could not set encoder constant QP", cleanup);
    }
    else
    {
        /* Set rate control mode for encoder */
        ret = ctx.enc->setRateControlMode(ctx.ratecontrol);
        TEST_ERROR(ret < 0, "Could not set encoder rate control mode", cleanup);
        if (ctx.ratecontrol == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR) {
            uint32_t peak_bitrate;
            if (ctx.peak_bitrate < ctx.bitrate)
                peak_bitrate = 1.2f * ctx.bitrate;
            else
                peak_bitrate = ctx.peak_bitrate;
            /* Set peak bitrate value for variable bitrate mode for encoder */
            ret = ctx.enc->setPeakBitrate(peak_bitrate);
            TEST_ERROR(ret < 0, "Could not set encoder peak bitrate", cleanup);
        }
    }

    if (ctx.poc_type)
    {
        ret = ctx.enc->setPocType(ctx.poc_type);
        TEST_ERROR(ret < 0, "Could not set Picture Order Count value", cleanup);
    }

    /* Set IDR frame interval for encoder */
    ret = ctx.enc->setIDRInterval(ctx.idr_interval);
    TEST_ERROR(ret < 0, "Could not set encoder IDR interval", cleanup);

    /* Set I frame interval for encoder */
    ret = ctx.enc->setIFrameInterval(ctx.iframe_interval);
    TEST_ERROR(ret < 0, "Could not set encoder I-Frame interval", cleanup);

    /* Set framerate for encoder */
    ret = ctx.enc->setFrameRate(ctx.fps_n, ctx.fps_d);
    TEST_ERROR(ret < 0, "Could not set framerate", cleanup);

    if (ctx.temporal_tradeoff_level)
    {
        /* Set temporal tradeoff level value for encoder */
        ret = ctx.enc->setTemporalTradeoff(ctx.temporal_tradeoff_level);
        TEST_ERROR(ret < 0, "Could not set temporal tradeoff level", cleanup);
    }

    if (ctx.slice_length)
    {
        /* Set slice length value for encoder */
        ret = ctx.enc->setSliceLength(ctx.slice_length_type,
                ctx.slice_length);
        TEST_ERROR(ret < 0, "Could not set slice length params", cleanup);
    }

    if (ctx.enable_slice_level_encode)
    {
        /* Enable slice level encode for encoder */
        ret = ctx.enc->setSliceLevelEncode(true);
        TEST_ERROR(ret < 0, "Could not set slice level encode", cleanup);
    }

    if (ctx.hw_preset_type)
    {
        /* Set hardware preset value for encoder */
        ret = ctx.enc->setHWPresetType(ctx.hw_preset_type);
        TEST_ERROR(ret < 0, "Could not set encoder HW Preset Type", cleanup);
    }

    if (ctx.virtual_buffer_size)
    {
        /* Set virtual buffer size value for encoder */
        ret = ctx.enc->setVirtualBufferSize(ctx.virtual_buffer_size);
        TEST_ERROR(ret < 0, "Could not set virtual buffer size", cleanup);
    }

    if (ctx.slice_intrarefresh_interval)
    {
        /* Set slice intra refresh interval value for encoder */
        ret = ctx.enc->setSliceIntrarefresh(ctx.slice_intrarefresh_interval);
        TEST_ERROR(ret < 0, "Could not set slice intrarefresh interval", cleanup);
    }

    if (ctx.insert_sps_pps_at_idr)
    {
        /* Enable insert of SPSPPS at IDR frames */
        ret = ctx.enc->setInsertSpsPpsAtIdrEnabled(true);
        TEST_ERROR(ret < 0, "Could not set insertSPSPPSAtIDR", cleanup);
    }

    if (ctx.disable_cabac)
    {
        /* Disable CABAC entropy encoding */
        ret = ctx.enc->setCABAC(false);
        TEST_ERROR(ret < 0, "Could not set disable CABAC", cleanup);
    }

    if (ctx.sar_width)
    {
        /* Set SAR width */
        ret = ctx.enc->setSampleAspectRatioWidth(ctx.sar_width);
        TEST_ERROR(ret < 0, "Could not set Sample Aspect Ratio width", cleanup);
    }

    if (ctx.sar_height)
    {
        /* Set SAR width */
        ret = ctx.enc->setSampleAspectRatioHeight(ctx.sar_height);
        TEST_ERROR(ret < 0, "Could not set Sample Aspect Ratio height", cleanup);
    }

    if (ctx.insert_vui)
    {
        /* Enable insert of VUI parameters */
        ret = ctx.enc->setInsertVuiEnabled(true);
        TEST_ERROR(ret < 0, "Could not set insertVUI", cleanup);
    }

    if (ctx.enable_extended_colorformat)
    {
        /* Enable extnded colorformat for encoder */
        ret = ctx.enc->setExtendedColorFormat(true);
        TEST_ERROR(ret < 0, "Could not set extended color format", cleanup);
    }

    if (ctx.insert_aud)
    {
        /* Enable insert of AUD parameters */
        ret = ctx.enc->setInsertAudEnabled(true);
        TEST_ERROR(ret < 0, "Could not set insertAUD", cleanup);
    }

    if (ctx.alliframes)
    {
        /* Enable all I-frame encode */
        ret = ctx.enc->setAlliFramesEncode(true);
        TEST_ERROR(ret < 0, "Could not set Alliframes encoding", cleanup);
    }

    if (ctx.num_b_frames != (uint32_t) -1)
    {
        /* Set number of B-frames to to be used by encoder */
        ret = ctx.enc->setNumBFrames(ctx.num_b_frames);
        TEST_ERROR(ret < 0, "Could not set number of B Frames", cleanup);
    }

    if ((ctx.nMinQpI != (uint32_t)QP_RETAIN_VAL) ||
        (ctx.nMaxQpI != (uint32_t)QP_RETAIN_VAL) ||
        (ctx.nMinQpP != (uint32_t)QP_RETAIN_VAL) ||
        (ctx.nMaxQpP != (uint32_t)QP_RETAIN_VAL) ||
        (ctx.nMinQpB != (uint32_t)QP_RETAIN_VAL) ||
        (ctx.nMaxQpB != (uint32_t)QP_RETAIN_VAL))
    {
        /* Set Min & Max qp range values for I/P/B-frames to be used by encoder */
        ret = ctx.enc->setQpRange(ctx.nMinQpI, ctx.nMaxQpI, ctx.nMinQpP,
                ctx.nMaxQpP, ctx.nMinQpB, ctx.nMaxQpB);
        TEST_ERROR(ret < 0, "Could not set quantization parameters", cleanup);
    }

    if (ctx.max_perf)
    {
        /* Enable maximum performance mode by disabling internal DFS logic.
           NOTE: This enables encoder to run at max clocks */
        ret = ctx.enc->setMaxPerfMode(ctx.max_perf);
        TEST_ERROR(ret < 0, "Error while setting encoder to max perf", cleanup);
    }

    if (ctx.dump_mv)
    {
        /* Enable dumping of motion vectors report from encoder */
        ret = ctx.enc->enableMotionVectorReporting();
        TEST_ERROR(ret < 0, "Could not enable motion vector reporting", cleanup);
    }

    if (ctx.bnoIframe) {
        ctx.iframe_interval = ((1<<31) + 1); /* TODO: how can we do this properly */
        ret = ctx.enc->setIFrameInterval(ctx.iframe_interval);
        TEST_ERROR(ret < 0, "Could not set encoder I-Frame interval", cleanup);
    }

    if (ctx.enableROI) {
        v4l2_enc_enable_roi_param VEnc_enable_ext_roi_ctrl;

        VEnc_enable_ext_roi_ctrl.bEnableROI = ctx.enableROI;
        /* Enable region of intrest configuration for encoder */
        ret = ctx.enc->enableROI(VEnc_enable_ext_roi_ctrl);
        TEST_ERROR(ret < 0, "Could not enable ROI", cleanup);
    }

    if (ctx.bReconCrc) {
        v4l2_enc_enable_reconcrc_param VEnc_enable_recon_crc_ctrl;

        VEnc_enable_recon_crc_ctrl.bEnableReconCRC = ctx.bReconCrc;
        /* Enable reconstructed CRC configuration for encoder */
        ret = ctx.enc->enableReconCRC(VEnc_enable_recon_crc_ctrl);
        TEST_ERROR(ret < 0, "Could not enable Recon CRC", cleanup);
    }

    if (ctx.externalRPS) {
        v4l2_enc_enable_ext_rps_ctr VEnc_enable_ext_rps_ctrl;

        VEnc_enable_ext_rps_ctrl.bEnableExternalRPS = ctx.externalRPS;
        if (ctx.encoder_pixfmt == V4L2_PIX_FMT_H264) {
            VEnc_enable_ext_rps_ctrl.bGapsInFrameNumAllowed = ctx.bGapsInFrameNumAllowed;
            VEnc_enable_ext_rps_ctrl.nH264FrameNumBits = ctx.nH264FrameNumBits;
        }
        if (ctx.encoder_pixfmt == V4L2_PIX_FMT_H265) {
            VEnc_enable_ext_rps_ctrl.nH265PocLsbBits = ctx.nH265PocLsbBits;
        }
        /* Enable external reference picture set configuration for encoder */
        ret = ctx.enc->enableExternalRPS(VEnc_enable_ext_rps_ctrl);
        TEST_ERROR(ret < 0, "Could not enable external RPS", cleanup);
    }

    if (ctx.num_reference_frames)
    {
        /* Set number of reference frame configuration value for encoder */
        ret = ctx.enc->setNumReferenceFrames(ctx.num_reference_frames);
        TEST_ERROR(ret < 0, "Could not set num reference frames", cleanup);
    }

    if (ctx.externalRCHints) {
        v4l2_enc_enable_ext_rate_ctr VEnc_enable_ext_rate_ctrl;

        VEnc_enable_ext_rate_ctrl.bEnableExternalPictureRC = ctx.externalRCHints;
        VEnc_enable_ext_rate_ctrl.nsessionMaxQP = ctx.sMaxQp;

        /* Enable external rate control configuration for encoder */
        ret = ctx.enc->enableExternalRC(VEnc_enable_ext_rate_ctrl);
        TEST_ERROR(ret < 0, "Could not enable external RC", cleanup);
    }

    if (ctx.encoder_pixfmt == V4L2_PIX_FMT_AV1)
    {
        if (ctx.enable_av1tile)
        {
            v4l2_enc_av1_tile_config VEnc_av1_tile_config;

            VEnc_av1_tile_config.bEnableTile = ctx.enable_av1tile;
            VEnc_av1_tile_config.nLog2RowTiles = ctx.log2_num_av1rows;
            VEnc_av1_tile_config.nLog2ColTiles = ctx.log2_num_av1cols;

            /* Enable tile configuration for encoder */
            ret = ctx.enc->enableAV1Tile(VEnc_av1_tile_config);
            TEST_ERROR(ret < 0, "Could not enable Tile Configuration", cleanup);
        }
        if (ctx.enable_av1ssimrdo != (uint8_t) -1)
        {
            ret = ctx.enc->setAV1SsimRdo(ctx.enable_av1ssimrdo);
            TEST_ERROR(ret < 0, "Could not set Ssim RDO", cleanup);
        }
        if (ctx.disable_av1cdfupdate != (uint8_t) -1)
        {
            ret = ctx.enc->setAV1DisableCDFUpdate(ctx.disable_av1cdfupdate);
            TEST_ERROR(ret < 0, "Could not set disable CDF update", cleanup);
        }
    }
    /* Query, Export and Map the output plane buffers so that we can read
       raw data into the buffers */
    switch(ctx.output_memory_type)
    {
        case V4L2_MEMORY_MMAP:
            ret = ctx.enc->output_plane.setupPlane(V4L2_MEMORY_MMAP, 10, true, false);
            TEST_ERROR(ret < 0, "Could not setup output plane", cleanup);
            break;

        case V4L2_MEMORY_USERPTR:
            ret = ctx.enc->output_plane.setupPlane(V4L2_MEMORY_USERPTR, 10, false, true);
            TEST_ERROR(ret < 0, "Could not setup output plane", cleanup);
            break;

        case V4L2_MEMORY_DMABUF:
            ret = setup_output_dmabuf(&ctx,10);
            TEST_ERROR(ret < 0, "Could not setup plane", cleanup);
            break;
        default :
            TEST_ERROR(true, "Not a valid plane", cleanup);
    }

    /* Query, Export and Map the capture plane buffers so that we can write
       encoded bitstream data into the buffers */
    switch(ctx.capture_memory_type)
    {
        case V4L2_MEMORY_MMAP:
            ret = ctx.enc->capture_plane.setupPlane(V4L2_MEMORY_MMAP, ctx.num_output_buffers, true, false);
            TEST_ERROR(ret < 0, "Could not setup capture plane", cleanup);
            break;
        case V4L2_MEMORY_DMABUF:
            ret = setup_capture_dmabuf(&ctx,ctx.num_output_buffers);
            TEST_ERROR(ret < 0, "Could not setup plane", cleanup);
            break;
        default :
            TEST_ERROR(true, "Not a valid plane", cleanup);
    }

    /* Subscibe for End Of Stream event */
    ret = ctx.enc->subscribeEvent(V4L2_EVENT_EOS,0,0);
    TEST_ERROR(ret < 0, "Could not subscribe EOS event", cleanup);

    if (ctx.b_use_enc_cmd)
    {
        /* Send v4l2 command for encoder start */
        ret = ctx.enc->setEncoderCommand(V4L2_ENC_CMD_START, 0);
        TEST_ERROR(ret < 0, "Error in start of encoder commands ", cleanup);
    }
    else
    {
        /* set encoder output plane STREAMON */
        ret = ctx.enc->output_plane.setStreamStatus(true);
        TEST_ERROR(ret < 0, "Error in output plane streamon", cleanup);

        /* set encoder capture plane STREAMON */
        ret = ctx.enc->capture_plane.setStreamStatus(true);
        TEST_ERROR(ret < 0, "Error in capture plane streamon", cleanup);
    }


    if (ctx.blocking_mode)
    {
        /* Set encoder capture plane dq thread callback for blocking io mode */
        ctx.enc->capture_plane.
            setDQThreadCallback(encoder_capture_plane_dq_callback);

        /* startDQThread starts a thread internally which calls the
           encoder_capture_plane_dq_callback whenever a buffer is dequeued
           on the plane */
        ctx.enc->capture_plane.startDQThread(&ctx);
    }

    /* Enqueue all the empty capture plane buffers. */
    for (uint32_t i = 0; i < ctx.enc->capture_plane.getNumBuffers(); i++)
    {
        struct v4l2_buffer v4l2_buf;
        struct v4l2_plane planes[MAX_PLANES];

        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        memset(planes, 0, MAX_PLANES * sizeof(struct v4l2_plane));

        v4l2_buf.index = i;
        v4l2_buf.m.planes = planes;
        if(ctx.capture_memory_type == V4L2_MEMORY_DMABUF)
        {
            v4l2_buf.m.planes[0].m.fd = ctx.capture_plane_fd[i];

             /* Map capture plane buffer for memory type DMABUF. */
            ret = ctx.enc->capture_plane.mapOutputBuffers(v4l2_buf, ctx.capture_plane_fd[i]);

            if (ret < 0)
            {
                cerr << "Error while mapping buffer at capture plane" << endl;
                abort(&ctx);
                goto cleanup;
            }
        }

        ret = ctx.enc->capture_plane.qBuffer(v4l2_buf, NULL);
        if (ret < 0)
        {
            cerr << "Error while queueing buffer at capture plane" << endl;
            abort(&ctx);
            goto cleanup;
        }
    }

    if (ctx.copy_timestamp) {
      /* Set user provided timestamp when copy timestamp is enabled */
      ctx.timestamp = (ctx.start_ts * MICROSECOND_UNIT);
      ctx.timestampincr = (MICROSECOND_UNIT * 16) / ((uint32_t) (ctx.fps_n * 16));
    }

    if(ctx.ppe_init_params.enable_ppe)
    {
        ret = ctx.enc->setPPEInitParams(ctx.ppe_init_params);
        if (ret < 0){
            cerr << "Error calling setPPEInitParams" << endl;
        }
    }

    if (ctx.blocking_mode)
    {
        /* Wait till capture plane DQ Thread finishes
           i.e. all the capture plane buffers are dequeued. */
        if (encoder_proc_blocking(ctx, eos) != 0)
            goto cleanup;
        ctx.enc->capture_plane.waitForDQThread(-1);
    }

    if (ctx.stats)
    {
        ctx.enc->printProfilingStats(cout);
    }

cleanup:
    if (ctx.enc && ctx.enc->isInError())
    {
        cerr << "Encoder is in error" << endl;
        error = 1;
    }
    if (ctx.got_error)
    {
        error = 1;
    }


    if(ctx.output_memory_type == V4L2_MEMORY_DMABUF && ctx.enc)
    {
        for (uint32_t i = 0; i < ctx.enc->output_plane.getNumBuffers(); i++)
        {
            /* Unmap output plane buffer for memory type DMABUF. */
            ret = ctx.enc->output_plane.unmapOutputBuffers(i, ctx.output_plane_fd[i]);
            if (ret < 0)
            {
                cerr << "Error while unmapping buffer at output plane" << endl;
                goto cleanup;
            }

            ret = NvBufSurf::NvDestroy(ctx.output_plane_fd[i]);
            ctx.output_plane_fd[i] = -1;
            if(ret < 0)
            {
                cerr << "Failed to Destroy NvBuffer\n" << endl;
                return ret;
            }
        }

    }

    if(ctx.capture_memory_type == V4L2_MEMORY_DMABUF && ctx.enc)
    {
        for (uint32_t i = 0; i < ctx.enc->capture_plane.getNumBuffers(); i++)
        {
            /* Unmap capture plane buffer for memory type DMABUF. */
            ret = ctx.enc->capture_plane.unmapOutputBuffers(i, ctx.capture_plane_fd[i]);
            if (ret < 0)
            {
                cerr << "Error while unmapping buffer at capture plane" << endl;
                return ret;
            }

            NvBufSurface *nvbuf_surf = 0;
            ret = NvBufSurfaceFromFd(ctx.capture_plane_fd[i], (void **)(&nvbuf_surf));
            if (ret < 0)
            {
                cerr << "Error while NvBufSurfaceFromFd" << endl;
                return ret;
            }

            ret = NvBufSurfaceDestroy(nvbuf_surf);
            if(ret < 0)
            {
                cerr << "Failed to Destroy NvBuffer\n" << endl;
                return ret;
            }
        }
    }

    free(ctx.p_buffer);
    /* Release encoder configuration specific resources. */
    delete ctx.enc;

    return -error;
}

