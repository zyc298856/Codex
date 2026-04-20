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

#include "NvApplicationProfiler.h"
#include "NvUtils.h"
#include <errno.h>
#include <fstream>
#include <iostream>
#include <linux/videodev2.h>
#include <malloc.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <vulkan/vulkan.h>
#include "video_decode.h"
#include <bitset>
#include <set>


#include <cuda.h>
#include <cuda_runtime.h>
#include <cuda_profiler_api.h>

extern "C"
{
#include "../main.h"
}

#include "nvbuf_utils.h"
#include "NvUtils.h"
#include "cudaEGL.h"
#include "NvCudaProc.h"

#define TEST_ERROR(cond, str, label) if(cond) { \
                                        cerr << str << endl; \
                                        error = 1; \
                                        goto label; }

#define MICROSECOND_UNIT 1000000
#define CHUNK_SIZE 4000000
#define MIN(a,b) (((a) < (b)) ? (a) : (b))

#define IS_NAL_UNIT_START(buffer_ptr) (!buffer_ptr[0] && !buffer_ptr[1] && \
        !buffer_ptr[2] && (buffer_ptr[3] == 1))

#define IS_NAL_UNIT_START1(buffer_ptr) (!buffer_ptr[0] && !buffer_ptr[1] && \
        (buffer_ptr[2] == 1))

#define H264_NAL_UNIT_CODED_SLICE  1
#define H264_NAL_UNIT_CODED_SLICE_IDR  5

#define HEVC_NUT_TRAIL_N  0
#define HEVC_NUT_RASL_R  9
#define HEVC_NUT_BLA_W_LP  16
#define HEVC_NUT_CRA_NUT  21

#define IVF_FILE_HDR_SIZE   32
#define IVF_FRAME_HDR_SIZE  12

#define IS_H264_NAL_CODED_SLICE(buffer_ptr) ((buffer_ptr[0] & 0x1F) == H264_NAL_UNIT_CODED_SLICE)
#define IS_H264_NAL_CODED_SLICE_IDR(buffer_ptr) ((buffer_ptr[0] & 0x1F) == H264_NAL_UNIT_CODED_SLICE_IDR)

#define IS_MJPEG_START(buffer_ptr) (buffer_ptr[0] == 0xFF && buffer_ptr[1] == 0xD8)
#define IS_MJPEG_END(buffer_ptr) (buffer_ptr[0] == 0xFF && buffer_ptr[1] == 0xD9)

#define GET_H265_NAL_UNIT_TYPE(buffer_ptr) ((buffer_ptr[0] & 0x7E) >> 1)

using namespace std;

/**
  * Exit on error.
  *
  * @param ctx : Decoder context
  */
static void
abort(context_t *ctx)
{
    ctx->got_error = true;
    ctx->dec->abort();
}

/**
  * Query and Set Capture plane.
  *
  * @param ctx : Decoder context
  */
static void
query_and_set_capture(context_t * ctx)
{
    NvVideoDecoder *dec = ctx->dec;
    struct v4l2_format format;
    struct v4l2_crop crop;
    int32_t min_dec_capture_buffers;
    int ret = 0;
    int error = 0;
    uint32_t window_width;
    uint32_t window_height;
    uint32_t sar_width;
    uint32_t sar_height;
    NvBufSurfaceColorFormat pix_format;
    NvBufSurf::NvCommonAllocateParams params;
    NvBufSurf::NvCommonAllocateParams capParams;

    /* Get capture plane format from the decoder.
       This may change after resolution change event.
       Refer ioctl VIDIOC_G_FMT */
    ret = dec->capture_plane.getFormat(format);
    TEST_ERROR(ret < 0,
               "Error: Could not get format from decoder capture plane", error);

    /* Get the display resolution from the decoder.
       Refer ioctl VIDIOC_G_CROP */
    ret = dec->capture_plane.getCrop(crop);
    TEST_ERROR(ret < 0,
               "Error: Could not get crop from decoder capture plane", error);

    cout << "Video Resolution: " << crop.c.width << "x" << crop.c.height
        << endl;
    ctx->display_height = crop.c.height;
    ctx->display_width = crop.c.width;

    /* Get the Sample Aspect Ratio (SAR) width and height */
    ret = dec->getSAR(sar_width, sar_height);
    cout << "Video SAR width: " << sar_width << " SAR height: " << sar_height << endl;
    if(ctx->dst_dma_fd != -1)
    {
        ret = NvBufSurf::NvDestroy(ctx->dst_dma_fd);
        ctx->dst_dma_fd = -1;
        TEST_ERROR(ret < 0, "Error: Error in BufferDestroy", error);
    }
    /* Create output buffer for transform. */
    params.memType = NVBUF_MEM_SURFACE_ARRAY;
    params.width = crop.c.width;
    params.height = crop.c.height;
    if (ctx->vkRendering)
    {
        /* Foreign FD in rmapi_tegra is imported as block linear kind by default and
         * there is no way right now in our driver to know the kind at the time of
         * import
         * */
        params.layout = NVBUF_LAYOUT_BLOCK_LINEAR;
    }
    else
        params.layout = NVBUF_LAYOUT_PITCH;
    if (ctx->out_pixfmt == 1)
      params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
    else if (ctx->out_pixfmt == 2)
      params.colorFormat = NVBUF_COLOR_FORMAT_YUV420;
    else if (ctx->out_pixfmt == 3)
      params.colorFormat = NVBUF_COLOR_FORMAT_NV16;
    else if (ctx->out_pixfmt == 4)
      params.colorFormat = NVBUF_COLOR_FORMAT_NV24;

    params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;
    if (ctx->vkRendering)
        params.colorFormat = NVBUF_COLOR_FORMAT_RGBA;

    ret = NvBufSurf::NvAllocate(&params, 1, &ctx->dst_dma_fd);
    TEST_ERROR(ret == -1, "create dmabuf failed", error);

    if (!ctx->disable_rendering)
    {
        /* Destroy the old instance of renderer as resolution might have changed. */
        if (ctx->vkRendering)
        {
            delete ctx->vkRenderer;
        } else {
            delete ctx->eglRenderer;
        }

        if (ctx->fullscreen)
        {
            /* Required for fullscreen. */
            window_width = window_height = 0;
        }
        else if (ctx->window_width && ctx->window_height)
        {
            /* As specified by user on commandline. */
            window_width = ctx->window_width;
            window_height = ctx->window_height;
        }
        else
        {
            /* Resolution got from the decoder. */
            window_width = crop.c.width;
            window_height = crop.c.height;
        }

        if (!ctx->vkRendering)
        {
            /* If height or width are set to zero, EglRenderer creates a fullscreen
               window for rendering. */
            ctx->eglRenderer =
                NvEglRenderer::createEglRenderer("renderer0", window_width,
                                           window_height, ctx->window_x,
                                           ctx->window_y);
            TEST_ERROR(!ctx->eglRenderer,
                   "Error in setting up of egl renderer. "
                   "Check if X is running or run with --disable-rendering",
                   error);
            if (ctx->stats)
            {
                /* Enable profiling for renderer if stats are requested. */
                ctx->eglRenderer->enableProfiling();
            }
            ctx->eglRenderer->setFPS(ctx->fps);
        } else {
            ctx->vkRenderer = NvVulkanRenderer::createVulkanRenderer("renderer0", window_width,
                                           window_height, ctx->window_x,
                                           ctx->window_y);
            TEST_ERROR(!ctx->vkRenderer,
                   "Error in setting up of vulkan renderer. "
                   "Check if X is running or run with --disable-rendering",
                   error);
            ctx->vkRenderer->setSize(window_width, window_height);
            ctx->vkRenderer->initVulkan();
        }
    }

    /* deinitPlane unmaps the buffers and calls REQBUFS with count 0 */
    dec->capture_plane.deinitPlane();
    if(ctx->capture_plane_mem_type == V4L2_MEMORY_DMABUF)
    {
        for(int index = 0 ; index < ctx->numCapBuffers ; index++)
        {
            if(ctx->dmabuff_fd[index] != 0)
            {
                ret = NvBufSurf::NvDestroy(ctx->dmabuff_fd[index]);
                TEST_ERROR(ret < 0, "Error: Error in BufferDestroy", error);
            }
        }
    }

    /* Not necessary to call VIDIOC_S_FMT on decoder capture plane.
       But decoder setCapturePlaneFormat function updates the class variables */
    ret = dec->setCapturePlaneFormat(format.fmt.pix_mp.pixelformat,
                                     format.fmt.pix_mp.width,
                                     format.fmt.pix_mp.height);
    TEST_ERROR(ret < 0, "Error in setting decoder capture plane format", error);

    ctx->video_height = format.fmt.pix_mp.height;
    ctx->video_width = format.fmt.pix_mp.width;
    /* Get the minimum buffers which have to be requested on the capture plane. */
    ret = dec->getMinimumCapturePlaneBuffers(min_dec_capture_buffers);
    TEST_ERROR(ret < 0,
               "Error while getting value of minimum capture plane buffers",
               error);

    /* Request (min + extra) buffers, export and map buffers. */
    if(ctx->capture_plane_mem_type == V4L2_MEMORY_MMAP)
    {
        /* Request, Query and export decoder capture plane buffers.
           Refer ioctl VIDIOC_REQBUFS, VIDIOC_QUERYBUF and VIDIOC_EXPBUF */
        ret =
            dec->capture_plane.setupPlane(V4L2_MEMORY_MMAP,
                                          min_dec_capture_buffers + ctx->extra_cap_plane_buffer, false,
                                          false);
        TEST_ERROR(ret < 0, "Error in decoder capture plane setup", error);
    }
    else if(ctx->capture_plane_mem_type == V4L2_MEMORY_DMABUF)
    {
        /* Set colorformats for relevant colorspaces. */
        switch(format.fmt.pix_mp.colorspace)
        {
            case V4L2_COLORSPACE_SMPTE170M:
                if (format.fmt.pix_mp.quantization == V4L2_QUANTIZATION_DEFAULT)
                {
                    cout << "Decoder colorspace ITU-R BT.601 with standard range luma (16-235)" << endl;
                    pix_format = NVBUF_COLOR_FORMAT_NV12;
                }
                else
                {
                    cout << "Decoder colorspace ITU-R BT.601 with extended range luma (0-255)" << endl;
                    pix_format = NVBUF_COLOR_FORMAT_NV12_ER;
                }
                break;
            case V4L2_COLORSPACE_REC709:
                if (format.fmt.pix_mp.quantization == V4L2_QUANTIZATION_DEFAULT)
                {
                    cout << "Decoder colorspace ITU-R BT.709 with standard range luma (16-235)" << endl;
                    pix_format =  NVBUF_COLOR_FORMAT_NV12_709;
                }
                else
                {
                    cout << "Decoder colorspace ITU-R BT.709 with extended range luma (0-255)" << endl;
                    pix_format = NVBUF_COLOR_FORMAT_NV12_709_ER;
                }
                break;
            case V4L2_COLORSPACE_BT2020:
                {
                    cout << "Decoder colorspace ITU-R BT.2020" << endl;
                    pix_format = NVBUF_COLOR_FORMAT_NV12_2020;
                }
                break;
            default:
                cout << "supported colorspace details not available, use default" << endl;
                if (format.fmt.pix_mp.quantization == V4L2_QUANTIZATION_DEFAULT)
                {
                    cout << "Decoder colorspace ITU-R BT.601 with standard range luma (16-235)" << endl;
                    pix_format = NVBUF_COLOR_FORMAT_NV12;
                }
                else
                {
                    cout << "Decoder colorspace ITU-R BT.601 with extended range luma (0-255)" << endl;
                    pix_format = NVBUF_COLOR_FORMAT_NV12_ER;
                }
                break;
        }

        ctx->numCapBuffers = min_dec_capture_buffers + ctx->extra_cap_plane_buffer;

        capParams.memType = NVBUF_MEM_SURFACE_ARRAY;
        capParams.width = crop.c.width;
        capParams.height = crop.c.height;
        capParams.layout = NVBUF_LAYOUT_BLOCK_LINEAR;
        capParams.memtag = NvBufSurfaceTag_VIDEO_DEC;

        if (format.fmt.pix_mp.pixelformat  == V4L2_PIX_FMT_NV24M)
          pix_format = NVBUF_COLOR_FORMAT_NV24;
        else if (format.fmt.pix_mp.pixelformat  == V4L2_PIX_FMT_NV24_10LE)
          pix_format = NVBUF_COLOR_FORMAT_NV24_10LE;
        if (ctx->decoder_pixfmt == V4L2_PIX_FMT_MJPEG)
        {
            capParams.layout = NVBUF_LAYOUT_PITCH;
            if (format.fmt.pix_mp.pixelformat == V4L2_PIX_FMT_YUV422M)
            {
                pix_format = NVBUF_COLOR_FORMAT_YUV422;
            }
            else
            {
                pix_format = NVBUF_COLOR_FORMAT_YUV420;
            }
        }

        capParams.colorFormat = pix_format;

        ret = NvBufSurf::NvAllocate(&capParams, ctx->numCapBuffers, ctx->dmabuff_fd);

        TEST_ERROR(ret < 0, "Failed to create buffers", error);
        /* Request buffers on decoder capture plane.
           Refer ioctl VIDIOC_REQBUFS */
        ret = dec->capture_plane.reqbufs(V4L2_MEMORY_DMABUF,ctx->numCapBuffers);
        TEST_ERROR(ret, "Error in request buffers on capture plane", error);
    }

    /* Decoder capture plane STREAMON.
       Refer ioctl VIDIOC_STREAMON */
    ret = dec->capture_plane.setStreamStatus(true);
    TEST_ERROR(ret < 0, "Error in decoder capture plane streamon", error);

    /* Enqueue all the empty decoder capture plane buffers. */
    for (uint32_t i = 0; i < dec->capture_plane.getNumBuffers(); i++)
    {
        struct v4l2_buffer v4l2_buf;
        struct v4l2_plane planes[MAX_PLANES];

        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        memset(planes, 0, sizeof(planes));

        v4l2_buf.index = i;
        v4l2_buf.m.planes = planes;
        v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        v4l2_buf.memory = ctx->capture_plane_mem_type;
        if(ctx->capture_plane_mem_type == V4L2_MEMORY_DMABUF)
            v4l2_buf.m.planes[0].m.fd = ctx->dmabuff_fd[i];
        ret = dec->capture_plane.qBuffer(v4l2_buf, NULL);
        TEST_ERROR(ret < 0, "Error Qing buffer at output plane", error);
    }
    cout << "Query and set capture successful" << endl;
    return;

error:
    if (error)
    {
        abort(ctx);
        cerr << "Error in " << __func__ << endl;
    }
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
    	//cudaMemcpy2D((unsigned char*)eglFrame.frame.pPitch[1], pitchc, p_frame+i_width*p_video->i_height, i_width/2, i_width/2, p_video->i_height/2, cudaMemcpyDeviceToDevice);
    	//cudaMemcpy2D((unsigned char*)eglFrame.frame.pPitch[2], pitchc, p_frame+i_width*p_video->i_height*5/4, i_width/2, i_width/2, p_video->i_height/2, cudaMemcpyDeviceToDevice);
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

static int preprocessing(AVElement_t*p_video, int buffer_fd, uint8_t *p_frame)
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
  * Decoder capture thread loop function.
  *
  * @param args : void arguments
  */
static void *
dec_capture_loop_fcn(void *arg)
{
    context_t *ctx = (context_t *) arg;
    AVElement_t*p_video = (AVElement_t *)ctx->p_top;
    NvVideoDecoder *dec = ctx->dec;
    struct v4l2_event ev;
    int ret;

    cout << "Starting decoder capture loop thread" << endl;
    /* Need to wait for the first Resolution change event, so that
       the decoder knows the stream resolution and can allocate appropriate
       buffers when we call REQBUFS. */
    do
    {
        /* Refer ioctl VIDIOC_DQEVENT */
        ret = dec->dqEvent(ev, 50000);
        if (ret < 0)
        {
            if (errno == EAGAIN)
            {
                cerr <<
                    "Timed out waiting for first V4L2_EVENT_RESOLUTION_CHANGE"
                    << endl;
            }
            else
            {
                cerr << "Error in dequeueing decoder event" << endl;
            }
            abort(ctx);
            break;
        }
    }
    while ((ev.type != V4L2_EVENT_RESOLUTION_CHANGE) && !ctx->got_error);

    /* Received the resolution change event, now can do query_and_set_capture. */
    if (!ctx->got_error)
        query_and_set_capture(ctx);

    /* Exit on error or EOS which is signalled in main() */
    while (!(ctx->got_error || dec->isInError()))
    {
        NvBuffer *dec_buffer;

        /* Check for Resolution change again.
           Refer ioctl VIDIOC_DQEVENT */
        ret = dec->dqEvent(ev, false);
        if (ret == 0)
        {
            switch (ev.type)
            {
                case V4L2_EVENT_RESOLUTION_CHANGE:
                    query_and_set_capture(ctx);
                    continue;
            }
        }

        /* Decoder capture loop */
        while (1)
        {
        	uint8_t *p_outframe = FrameBufferPut( &p_video->m_preframes );
    		if(!p_outframe && !p_video->b_die)
    		{
    			usleep(1000);
    			continue;
    		}

            struct v4l2_buffer v4l2_buf;
            struct v4l2_plane planes[MAX_PLANES];

            memset(&v4l2_buf, 0, sizeof(v4l2_buf));
            memset(planes, 0, sizeof(planes));
            v4l2_buf.m.planes = planes;

            /* Dequeue a filled buffer. */
            if (dec->capture_plane.dqBuffer(v4l2_buf, &dec_buffer, NULL, 0))
            {
                if (errno == EAGAIN)
                {
                    if (v4l2_buf.flags & V4L2_BUF_FLAG_LAST)
                    {
                        cout << "Got EoS at capture plane" << endl;
                        goto handle_eos;
                    }
                    usleep(1000);
                }
                else
                {
                    abort(ctx);
                    cerr << "Error while calling dequeue at capture plane" <<
                        endl;
                }
                break;
            }

            if (ctx->out_file || (!ctx->disable_rendering && !ctx->stats))
            {

                if(ctx->capture_plane_mem_type == V4L2_MEMORY_DMABUF)
                    dec_buffer->planes[0].fd = ctx->dmabuff_fd[v4l2_buf.index];

        		if(p_outframe)
        		{
                    preprocessing(p_video, dec_buffer->planes[0].fd, p_outframe);
                    FrameBufferPutdown(&p_video->m_preframes,0,0);
        		}

                /* If not writing to file, Queue the buffer back once it has been used. */
                if(ctx->capture_plane_mem_type == V4L2_MEMORY_DMABUF)
                    v4l2_buf.m.planes[0].m.fd = ctx->dmabuff_fd[v4l2_buf.index];
                if (dec->capture_plane.qBuffer(v4l2_buf, NULL) < 0)
                {
                    abort(ctx);
                    cerr <<
                        "Error while queueing buffer at decoder capture plane"
                        << endl;
                    break;
                }
            }
            else
            {
                /* If not writing to file, Queue the buffer back once it has been used. */
                if(ctx->capture_plane_mem_type == V4L2_MEMORY_DMABUF)
                    v4l2_buf.m.planes[0].m.fd = ctx->dmabuff_fd[v4l2_buf.index];
                if (dec->capture_plane.qBuffer(v4l2_buf, NULL) < 0)
                {
                    abort(ctx);
                    cerr <<
                        "Error while queueing buffer at decoder capture plane"
                        << endl;
                    break;
                }
            }
        }
    }
handle_eos:
    cout << "Exiting decoder capture loop thread" << endl;
    return NULL;
}

/**
  * Set the default values for decoder context members.
  *
  * @param ctx : Decoder context
  */
static void
set_defaults(context_t * ctx)
{
    memset(ctx, 0, sizeof(context_t));
    ctx->fullscreen = false;
    ctx->window_height = 0;
    ctx->window_width = 0;
    ctx->window_x = 0;
    ctx->window_y = 0;
    ctx->out_pixfmt = 1;
    ctx->fps = 30;
    ctx->output_plane_mem_type = V4L2_MEMORY_MMAP;
    ctx->capture_plane_mem_type = V4L2_MEMORY_DMABUF;
    ctx->vp9_file_header_flag = 0;
    ctx->vp8_file_header_flag = 0;
    ctx->stress_test = 1;
    ctx->copy_timestamp = true;
    ctx->flag_copyts = false;
    ctx->start_ts = 0;
    ctx->file_count = 1;
    ctx->dec_fps = 30;
    ctx->dst_dma_fd = -1;
    ctx->bLoop = false;
    ctx->bQueue = false;
    ctx->loop_count = 0;
    ctx->max_perf = 0;
    ctx->extra_cap_plane_buffer = 1;
    ctx->blocking_mode = 1;
    pthread_mutex_init(&ctx->queue_lock, NULL);
    pthread_cond_init(&ctx->queue_cond, NULL);
}

/**
  * Decode processing function for blocking mode.
  *
  * @param ctx               : Decoder context
  * @param eos               : end of stream
  * @param current_file      : current file
  * @param current_loop      : iterator count
  * @param nalu_parse_buffer : input parsed nal unit
  */
static bool decoder_proc_blocking(context_t &ctx, bool eos, uint32_t current_file,
                                int current_loop, char *nalu_parse_buffer)
{
	AVElement_t*p_video = (AVElement_t *)ctx.p_top;

    int allow_DQ = true;
    int ret = 0;
    struct v4l2_buffer temp_buf;

    /* Since all the output plane buffers have been queued, we first need to
       dequeue a buffer from output plane before we can read new data into it
       and queue it again. */
    while (!eos && !ctx.got_error && !ctx.dec->isInError())
    {
        int64_t i_pts = 0;
		block_t *p_block = BlockBufferGet( &p_video->video_fifo );
        if(!p_block && !p_video->b_die)
		{
			usleep(100);
			continue;
		}

        struct v4l2_buffer v4l2_buf;
        struct v4l2_plane planes[MAX_PLANES];
        NvBuffer *buffer;

        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        memset(planes, 0, sizeof(planes));

        v4l2_buf.m.planes = planes;

        /* dequeue a buffer for output plane. */
        if(allow_DQ)
        {
            ret = ctx.dec->output_plane.dqBuffer(v4l2_buf, &buffer, NULL, -1);
            if (ret < 0)
            {
                cerr << "Error DQing buffer at output plane" << endl;
                abort(&ctx);
                break;
            }
        }
        else
        {
            allow_DQ = true;
            memcpy(&v4l2_buf,&temp_buf,sizeof(v4l2_buffer));
            buffer = ctx.dec->output_plane.getNthBuffer(v4l2_buf.index);
        }

        buffer->planes[0].bytesused = 0;

		if(p_block && p_block->i_buffer > 0)
		{
			uint8_t *buffer_ptr = (uint8_t *) buffer->planes[0].data;
			i_pts = p_block->i_pts;

			buffer->planes[0].bytesused = p_block->i_buffer;
			memcpy(buffer_ptr, p_block->p_buffer, p_block->i_buffer);
			BlockBufferGetOff( &p_video->video_fifo );
		}

        v4l2_buf.m.planes[0].bytesused = buffer->planes[0].bytesused;

        if (ctx.input_nalu && ctx.copy_timestamp)
        {
          /* Update the timestamp. */
          v4l2_buf.flags |= V4L2_BUF_FLAG_TIMESTAMP_COPY;
          //if (ctx.flag_copyts)
          //    ctx.timestamp += ctx.timestampincr;
          v4l2_buf.timestamp.tv_sec = i_pts / (MICROSECOND_UNIT);
          v4l2_buf.timestamp.tv_usec = i_pts % (MICROSECOND_UNIT);
        }

        /* enqueue a buffer for output plane. */
        ret = ctx.dec->output_plane.qBuffer(v4l2_buf, NULL);
        if (ret < 0)
        {
            cerr << "Error Qing buffer at output plane" << endl;
            abort(&ctx);
            break;
        }
        if (v4l2_buf.m.planes[0].bytesused == 0)
        {
            eos = true;
            cout << "Input file read complete" << endl;
            break;
        }
    }
    return eos;
}

/**
  * Decode processing function.
  *
  * @param ctx  : Decoder context
  * @param argc : Argument Count
  * @param argv : Argument Vector
  */
int OpenDecoder(void *param)
{
	AVElement_t*p_video = (AVElement_t *)param;
	//program_t *p_program = p_video->p_program;

	context_t ctx;

    int ret = 0;
    int error = 0;
    uint32_t current_file = 0;
    uint32_t i;
    bool eos = false;
    int current_loop = 0;
    char *nalu_parse_buffer = NULL;
    NvApplicationProfiler &profiler = NvApplicationProfiler::getProfilerInstance();

    /* Set default values for decoder context members. */
    set_defaults(&ctx);
    ctx.p_top = p_video;
    if(p_video->i_video_codec == ES_V_H264)
    ctx.decoder_pixfmt = V4L2_PIX_FMT_H264;
    if(p_video->i_video_codec == ES_V_H265)
    ctx.decoder_pixfmt = V4L2_PIX_FMT_H265;

    /* Set thread name for decoder Output Plane thread. */
    pthread_setname_np(pthread_self(), "DecOutPlane");


    if (ctx.enable_sld && (ctx.decoder_pixfmt != V4L2_PIX_FMT_H265))
    {
        fprintf(stdout, "Slice level decoding is only applicable for H265 so disabling it\n");
        ctx.enable_sld = false;
    }

    if (ctx.enable_sld && !ctx.input_nalu)
    {
        fprintf(stdout, "Enabling input nalu mode required for slice level decode\n");
        ctx.input_nalu = true;
    }

    /* Create NvVideoDecoder object for blocking or non-blocking I/O mode. */
    if (ctx.blocking_mode)
    {
        cout << "Creating decoder in blocking mode \n";
        ctx.dec = NvVideoDecoder::createVideoDecoder("dec0");
    }

    TEST_ERROR(!ctx.dec, "Could not create decoder", cleanup);

    /* Enable profiling for decoder if stats are requested. */
    if (ctx.stats)
    {
        profiler.start(NvApplicationProfiler::DefaultSamplingInterval);
        ctx.dec->enableProfiling();
    }

    /* Subscribe to Resolution change event.
       Refer ioctl VIDIOC_SUBSCRIBE_EVENT */
    ret = ctx.dec->subscribeEvent(V4L2_EVENT_RESOLUTION_CHANGE, 0, 0);
    TEST_ERROR(ret < 0, "Could not subscribe to V4L2_EVENT_RESOLUTION_CHANGE",
               cleanup);

    /* Set format on the output plane.
       Refer ioctl VIDIOC_S_FMT */
    ret = ctx.dec->setOutputPlaneFormat(ctx.decoder_pixfmt, CHUNK_SIZE);
    TEST_ERROR(ret < 0, "Could not set output plane format", cleanup);

    /* Configure for frame input mode for decoder.
       Refer V4L2_CID_MPEG_VIDEO_DISABLE_COMPLETE_FRAME_INPUT */
    if (ctx.input_nalu)
    {
        /* Input to the decoder will be nal units. */
        nalu_parse_buffer = new char[CHUNK_SIZE];
        printf("Setting frame input mode to 1 \n");
        ret = ctx.dec->setFrameInputMode(1);
        TEST_ERROR(ret < 0,
                "Error in decoder setFrameInputMode", cleanup);
    }
    else
    {
        /* Input to the decoder will be a chunk of bytes.
           NOTE: Set V4L2_CID_MPEG_VIDEO_DISABLE_COMPLETE_FRAME_INPUT control to
                 false so that application can send chunks of encoded data instead
                 of forming complete frames. */
        printf("Setting frame input mode to 1 \n");
        ret = ctx.dec->setFrameInputMode(1);
        TEST_ERROR(ret < 0,
                "Error in decoder setFrameInputMode", cleanup);
    }

    if (ctx.enable_sld)
    {
        printf("Setting slice mode to 1 \n");
        ret = ctx.dec->setSliceMode(1);
        TEST_ERROR(ret < 0,
                "Error in decoder setSliceMode", cleanup);
    }

    /* Disable decoder DPB management.
       NOTE: V4L2_CID_MPEG_VIDEO_DISABLE_DPB should be set after output plane
             set format */
    if (ctx.disable_dpb)
    {
        ret = ctx.dec->disableDPB();
        TEST_ERROR(ret < 0, "Error in decoder disableDPB", cleanup);
    }

    /* Enable max performance mode by using decoder max clock settings.
       Refer V4L2_CID_MPEG_VIDEO_MAX_PERFORMANCE */
    if (ctx.max_perf)
    {
        ret = ctx.dec->setMaxPerfMode(ctx.max_perf);
        TEST_ERROR(ret < 0, "Error while setting decoder to max perf", cleanup);
    }

    /* Set the skip frames property of the decoder.
       Refer V4L2_CID_MPEG_VIDEO_SKIP_FRAMES */
    if (ctx.skip_frames)
    {
        ret = ctx.dec->setSkipFrames(ctx.skip_frames);
        TEST_ERROR(ret < 0, "Error while setting skip frames param", cleanup);
    }

    /* Query, Export and Map the output plane buffers so can read
       encoded data into the buffers. */
    if (ctx.output_plane_mem_type == V4L2_MEMORY_MMAP) {
        /* configure decoder output plane for MMAP io-mode.
           Refer ioctl VIDIOC_REQBUFS, VIDIOC_QUERYBUF and VIDIOC_EXPBUF */
        ret = ctx.dec->output_plane.setupPlane(V4L2_MEMORY_MMAP, 2, true, false);
    } else if (ctx.output_plane_mem_type == V4L2_MEMORY_USERPTR) {
        /* configure decoder output plane for USERPTR io-mode.
           Refer ioctl VIDIOC_REQBUFS */
        ret = ctx.dec->output_plane.setupPlane(V4L2_MEMORY_USERPTR, 10, false, true);
    }
    TEST_ERROR(ret < 0, "Error while setting up output plane", cleanup);

    /* Start stream processing on decoder output-plane.
       Refer ioctl VIDIOC_STREAMON */
    ret = ctx.dec->output_plane.setStreamStatus(true);
    TEST_ERROR(ret < 0, "Error in output plane stream on", cleanup);

    /* Enable copy timestamp with start timestamp in seconds for decode fps.
       NOTE: Used to demonstrate how timestamp can be associated with an
             individual H264/H265 frame to achieve video-synchronization. */
    if (ctx.copy_timestamp && ctx.input_nalu) {
      ctx.timestamp = (ctx.start_ts * MICROSECOND_UNIT);
      ctx.timestampincr = (MICROSECOND_UNIT * 16) / ((uint32_t) (ctx.dec_fps * 16));
    }

    /* Read encoded data and enqueue all the output plane buffers.
       Exit loop in case file read is complete. */
    i = 0;
    current_loop = 1;
    while (!eos && !ctx.got_error && !ctx.dec->isInError() &&
           i < ctx.dec->output_plane.getNumBuffers())
    {
        int64_t i_pts = 0;
		block_t *p_block = BlockBufferGet( &p_video->video_fifo );
        if(!p_block && !p_video->b_die)
		{
			usleep(100);
			continue;
		}

        struct v4l2_buffer v4l2_buf;
        struct v4l2_plane planes[MAX_PLANES];
        NvBuffer *buffer;

        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        memset(planes, 0, sizeof(planes));

        buffer = ctx.dec->output_plane.getNthBuffer(i);

        buffer->planes[0].bytesused = 0;

		if(p_block && p_block->i_buffer > 0)
		{
			uint8_t *buffer_ptr = (uint8_t *) buffer->planes[0].data;
			i_pts = p_block->i_pts;

			buffer->planes[0].bytesused = p_block->i_buffer;
			memcpy(buffer_ptr, p_block->p_buffer, p_block->i_buffer);
			BlockBufferGetOff( &p_video->video_fifo );
		}

        v4l2_buf.index = i;
        v4l2_buf.m.planes = planes;
        v4l2_buf.m.planes[0].bytesused = buffer->planes[0].bytesused;

        if (ctx.input_nalu && ctx.copy_timestamp)
        {
          /* Update the timestamp. */
          v4l2_buf.flags |= V4L2_BUF_FLAG_TIMESTAMP_COPY;
          //if (ctx.flag_copyts)
          //    ctx.timestamp += ctx.timestampincr;
          v4l2_buf.timestamp.tv_sec = i_pts / (MICROSECOND_UNIT);
          v4l2_buf.timestamp.tv_usec = i_pts % (MICROSECOND_UNIT);
        }

        if (ctx.copy_timestamp && ctx.input_nalu && ctx.stats)
        {
          cout << "[" << v4l2_buf.index << "]" "dec output plane qB timestamp [" <<
              v4l2_buf.timestamp.tv_sec << "s" << v4l2_buf.timestamp.tv_usec << "us]" << endl;
        }

        if (v4l2_buf.m.planes[0].bytesused == 0)
        {
            if (ctx.bQueue)
            {
                current_file++;
                if(current_file != ctx.file_count)
                {
                    continue;
                }
            }
            if(ctx.bLoop)
            {
                current_file = current_file % ctx.file_count;
                if(ctx.loop_count == 0 || current_loop < ctx.loop_count )
                {
                    current_loop++;
                    continue;
                }
            }
        }
        /* It is necessary to queue an empty buffer to signal EOS to the decoder
           i.e. set v4l2_buf.m.planes[0].bytesused = 0 and queue the buffer. */
        ret = ctx.dec->output_plane.qBuffer(v4l2_buf, NULL);
        if (ret < 0)
        {
            cerr << "Error Qing buffer at output plane" << endl;
            abort(&ctx);
            break;
        }
        if (v4l2_buf.m.planes[0].bytesused == 0)
        {
            eos = true;
            cout << "Input file read complete" << endl;
            break;
        }
        i++;
    }


    /* Create threads for decoder output */
    if (ctx.blocking_mode)
    {
        pthread_create(&ctx.dec_capture_loop, NULL, dec_capture_loop_fcn, &ctx);
        /* Set thread name for decoder Capture Plane thread. */
        pthread_setname_np(ctx.dec_capture_loop, "DecCapPlane");
    }


    if (ctx.blocking_mode)
        eos = decoder_proc_blocking(ctx, eos, current_file, current_loop, nalu_parse_buffer);
    /* After sending EOS, all the buffers from output plane should be dequeued.
       and after that capture plane loop should be signalled to stop. */
    if (ctx.blocking_mode)
    {
        while (ctx.dec->output_plane.getNumQueuedBuffers() > 0 &&
               !ctx.got_error && !ctx.dec->isInError())
        {
            struct v4l2_buffer v4l2_buf;
            struct v4l2_plane planes[MAX_PLANES];

            memset(&v4l2_buf, 0, sizeof(v4l2_buf));
            memset(planes, 0, sizeof(planes));

            v4l2_buf.m.planes = planes;
            ret = ctx.dec->output_plane.dqBuffer(v4l2_buf, NULL, NULL, -1);
            if (ret < 0)
            {
                cerr << "Error DQing buffer at output plane" << endl;
                abort(&ctx);
                break;
            }
            if (v4l2_buf.m.planes[0].bytesused == 0)
            {
                cout << "Got EoS at output plane"<< endl;
                break;
            }

        }
    }

    /* Signal EOS to the decoder capture loop. */
    ctx.got_eos = true;

cleanup:
    if (ctx.blocking_mode && ctx.dec_capture_loop)
    {
        pthread_join(ctx.dec_capture_loop, NULL);
    }

    if (ctx.stats && !ctx.vkRendering)
    {
        profiler.stop();
        ctx.dec->printProfilingStats(cout);
        if (ctx.eglRenderer)
        {
            ctx.eglRenderer->printProfilingStats(cout);
        }
        profiler.printProfilerData(cout);
    }

    if(ctx.capture_plane_mem_type == V4L2_MEMORY_DMABUF)
    {
        for(int index = 0 ; index < ctx.numCapBuffers ; index++)
        {
            if(ctx.dmabuff_fd[index] != 0)
            {
                ret = NvBufSurf::NvDestroy(ctx.dmabuff_fd[index]);
                if(ret < 0)
                {
                    cerr << "Failed to Destroy NvBuffer" << endl;
                }
            }
        }
    }
    if (ctx.dec && ctx.dec->isInError())
    {
        cerr << "Decoder is in error" << endl;
        error = 1;
    }

    if (ctx.got_error)
    {
        error = 1;
    }

    /* The decoder destructor does all the cleanup i.e set streamoff on output and
       capture planes, unmap buffers, tell decoder to deallocate buffer (reqbufs
       ioctl with count = 0), and finally call v4l2_close on the fd. */
    delete ctx.dec;
    /* Similarly, Renderer destructor does all the cleanup. */
    if (ctx.vkRendering)
    {
        delete ctx.vkRenderer;
    } else {
        delete ctx.eglRenderer;
    }

    if(ctx.dst_dma_fd != -1)
    {
        ret = NvBufSurf::NvDestroy(ctx.dst_dma_fd);
        ctx.dst_dma_fd = -1;
        if(ret < 0)
        {
            cerr << "Error in BufferDestroy" << endl;
            error = 1;
        }
    }
    delete[] nalu_parse_buffer;

    return -error;
}
