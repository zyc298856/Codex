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

#include <malloc.h>
#include <sstream>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <math.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include <cuda_profiler_api.h>

extern "C"
{
#include "../main.h"
}

#include "nvbuf_utils.h"
#include "NvUtils.h"
#include "NvBufSurface.h"
#include "cudaEGL.h"
#include "NvCudaProc.h"

using namespace std;

#ifdef ENABLE_RKNN_YOLO_BRIDGE
#include "../yolo/yolov10_rknn.h"
#endif

void findMaxmin(cudaStream_t stream, unsigned char *buffer, long size, unsigned char *pDstmax, unsigned char *pDstmin);
void GrayImageconvert(cudaStream_t stream, uint8_t *pDst, uint8_t *dpImage, int pitch, int nWidth, int nHeight, int omin, int nmin, float ratio);

static int use_rknn_yolo_backend()
{
#ifdef ENABLE_RKNN_YOLO_BRIDGE
	const char *backend = getenv("YOLOV10_BACKEND");
	return backend && !strcmp(backend, "rknn");
#else
	return 0;
#endif
}

void *video_preprocessing(void * param)
{
	AVElement_t *p_video = (AVElement_t *)param;

	uint8_t *pDst;
	uint8_t* pMax;
	uint8_t* pMin;

	cudaMallocManaged(&pDst, p_video->i_width* p_video->i_height);
	cudaMallocManaged(&pMax, 320);
	cudaMallocManaged(&pMin, 320);

	uint8_t hMax[320];
	uint8_t hMin[320];
    cudaStream_t streams;
    cudaStreamCreate(&streams);

    int b_pcd = 0;
    uint8_t *p_frame;
    uint8_t *p_outframe;

    recycle_frame_t *preframes = &p_video->m_preframes;
    recycle_frame_t *unetframes = &p_video->m_unetframes;

#ifdef _FILE_DEBUG
    int64_t total = 0, esp = 0;
    int64_t last = mdate();
    int i_frame = 0;
#endif

    int64_t pts = 0;
    p_video->i_output += 1;
    while(!p_video->b_die)
    {
    	if(!b_pcd)
    	{
			p_frame = FrameBufferGet( preframes, &pts );
			if(p_frame)
			{
#ifdef _FILE_DEBUG
				int64_t  cur = mdate();
				if(!esp&&!total) last = cur;
#endif
				if(p_video->i_filter_type == 1)
				{
					findMaxmin(streams, p_frame, p_video->i_width*p_video->i_height, pMax, pMin);

					int imax = 0;
					int imin = 255;
					cudaMemcpy(hMax, pMax, 320, cudaMemcpyDeviceToHost);
					cudaMemcpy(hMin, pMin, 320, cudaMemcpyDeviceToHost);
					for(int i = 0; i < 320; i++)
					{
						if(hMax[i] > imax)
						{
							imax = hMax[i];
						}
						if(hMin[i] < imin)
						{
							imin = hMin[i];
						}
					}

					GrayImageconvert(streams, pDst, p_frame, p_video->i_width, p_video->i_width, p_video->i_height, imin, p_video->i_min, (float)(p_video->i_max-p_video->i_min)/(imax-imin));


					if(!p_video->b_compare)
					p_frame = pDst;
				}

				b_pcd = 1;

#ifdef _FILE_DEBUG
				esp += mdate() - cur;
				i_frame++;
				if(i_frame > 60 )
				{
					total += esp;
					printf("prepro fps:%6.2f %.2f\n", i_frame*1000000.0/esp, total*1.0/(mdate() - last));
					esp = 0;
					i_frame = 0;
				}
#endif
			}
    	}

    	if(b_pcd)
    	{
    		p_outframe = FrameBufferPut( unetframes );
    		if(p_outframe)
    		{
    			b_pcd = 0;
    			if(p_video->b_compare == 1)
    			{
    				cudaMemcpy2D(p_outframe, p_video->i_width*2, p_video->i_filter_type == 1?pDst:p_frame, p_video->i_width, p_video->i_width, p_video->i_height, cudaMemcpyDeviceToDevice);
    				cudaMemcpy2D(p_outframe+p_video->i_width, p_video->i_width*2, p_frame, p_video->i_width, p_video->i_width, p_video->i_height, cudaMemcpyDeviceToDevice);
    			}
    			else
    			{
    				cudaMemcpy(p_outframe, p_frame, p_video->i_width* p_video->i_height, cudaMemcpyDeviceToDevice);
    			}

    			FrameBufferGetOff(preframes);
    			FrameBufferPutdown( unetframes, pts, 0);
    		}
    		else
    		{
    			usleep(10);
    		}
    	}
    }

    cudaFree(pDst);
    cudaFree(pMax);
    cudaFree(pMin);

    cudaStreamDestroy(streams);

	return NULL;
}



void *video_yolo(void * param)
{
	AVElement_t *p_video = (AVElement_t *)param;
    void*p_yolo = NULL;
    int b_use_rknn = use_rknn_yolo_backend();

#ifdef _OBJECT_DETECT
    if(b_use_rknn)
    {
#ifdef ENABLE_RKNN_YOLO_BRIDGE
    	p_yolo = open_yolo_rknn(p_video->i_width, p_video->i_height);
#endif
    }
    else
    {
    	p_yolo = open_yolo(p_video->i_width, p_video->i_height);
    }
#endif

    cudaStream_t streams;
    cudaStreamCreate(&streams);
    int b_pcd = 0;

    uint8_t *p_frame;
    uint8_t *p_outframe;
    recycle_frame_t *yoloframes = &p_video->m_yoloframes;
    recycle_frame_t *encframes = &p_video->m_encframes;
#ifdef _FILE_DEBUG
    int64_t total = 0, esp = 0;
    int64_t last = mdate();
    int i_frame = 0;
#endif
    int64_t pts = 0;
    p_video->i_output += 1;
    while(!p_video->b_die)
    {
    	if(!b_pcd)
    	{
			p_frame = FrameBufferGet( yoloframes, &pts  );
			if(p_frame)
			{
#ifdef _FILE_DEBUG
				int64_t  cur = mdate();
				if(!esp&&!total) last = cur;
#endif

#ifdef _OBJECT_DETECT
				if(p_yolo)
				{
					if(p_video->b_object_detect)// && i_frame%2 == 0)
					{
						if(b_use_rknn)
						{
#ifdef ENABLE_RKNN_YOLO_BRIDGE
							yolo_infer_rknn(p_video, p_yolo, p_frame, p_video->i_width*(p_video->b_compare+1), p_video->b_object_show);
#endif
						}
						else
						{
							yolo_infer(p_video, p_yolo, p_frame, p_video->i_width*(p_video->b_compare+1), p_video->b_object_show);
						}
					}
				}
#endif
				b_pcd = 1;

#ifdef _FILE_DEBUG
				esp += mdate() - cur;
				i_frame++;
				if(i_frame > 30 )
				{
					total += esp;
					printf("yolov10 fps:%6.2f %.2f\n", i_frame*1000000.0/esp, total*1.0/(mdate() - last));
					esp = 0;
					i_frame = 0;
				}
#endif
			}
    	}

    	if(b_pcd)
    	{
    		p_outframe = FrameBufferPut(encframes);
    		if(p_outframe)
    		{
    			b_pcd = 0;
    			cudaMemcpy(p_outframe, p_frame, p_video->i_width* p_video->i_height*(p_video->b_compare+1)*3/2, cudaMemcpyDeviceToDevice);
    			cudaMemset(p_frame+p_video->i_width* p_video->i_height*(p_video->b_compare+1),  128, p_video->i_width* p_video->i_height*(p_video->b_compare+1)/2);
    			FrameBufferPutdown(encframes,pts, 0);
    			FrameBufferGetOff(yoloframes);
    		}
    		else
    		{
    			usleep(10);
    		}
    	}
		else
		{
			usleep(10);
		}
    }

#ifdef _OBJECT_DETECT
	if(p_yolo)
	{
		if(b_use_rknn)
		{
#ifdef ENABLE_RKNN_YOLO_BRIDGE
			close_yolo_rknn(p_yolo);
#endif
		}
		else
		{
			close_yolo(p_yolo);
		}
	}
#endif

    cudaStreamDestroy(streams);

	return NULL;
}
