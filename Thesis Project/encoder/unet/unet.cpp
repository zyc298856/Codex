#include "mimo_unet.h"

#include <opencv2/opencv.hpp>
#include <iostream>


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

//AddBITMAPHEAD
//by pipawancui
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>

void *video_unet(void * param)
{
	AVElement_t *p_video = (AVElement_t *)param;
	uint8_t* pDst;
	float* pPro;
	Logger logger;

    // 创建MimoUnet推理对象，并加载engine模型
    MimoUnet mimo("trt.engine", logger);

	cudaMallocManaged(&pPro, p_video->i_width* p_video->i_height*sizeof(float));
	cudaMallocManaged(&pDst, p_video->i_width* p_video->i_height);

    int b_pcd = 0;
    uint8_t *p_frame;
    uint8_t *p_outframe;
    recycle_frame_t *unetframes = &p_video->m_unetframes;
    recycle_frame_t *yoloframes = &p_video->m_yoloframes;

#ifdef _FILE_DEBUG
    int64_t total = 0, esp = 0;
    int64_t last = mdate();
    int i_frame = 0;
#endif

    int64_t pts = 0;
    int flag = 0;
    p_video->i_output += 1;
    while(!p_video->b_die)
    {
    	if(!b_pcd)
    	{
			p_frame = FrameBufferGet( unetframes, &pts  );
			if(p_frame)
			{
#ifdef _FILE_DEBUG
				int64_t  cur = mdate();
				if(!esp&&!total) last = cur;
#endif
				if(p_video->b_image_unet)
			    {
					mimo.infer(p_frame, p_video->i_width*(p_video->b_compare+1), pDst, pPro, p_video->i_width, p_video->i_height);
			    }

				b_pcd = 1;

#ifdef _FILE_DEBUG
				esp += mdate() - cur;
				i_frame++;
				if(i_frame > 30 )
				{
					total += esp;
					printf("net    fps:%6.2f %.2f\n", i_frame*1000000.0/esp, total*1.0/(mdate() - last));
					esp = 0;
					i_frame = 0;
				}
#endif
			}
    	}

    	if(b_pcd)
    	{
    		p_outframe = FrameBufferPut( yoloframes );
    		if(p_outframe)
    		{
    			b_pcd = 0;
    			if(p_video->b_compare == 1 && p_video->b_image_unet == 1)
    			{
    				cudaMemcpy2D(p_outframe, p_video->i_width*2, pDst, p_video->i_width, p_video->i_width, p_video->i_height, cudaMemcpyDeviceToDevice);
    				cudaMemcpy2D(p_outframe+p_video->i_width, p_video->i_width*2, p_frame+p_video->i_width, 2*p_video->i_width, p_video->i_width, p_video->i_height, cudaMemcpyDeviceToDevice);
    			}
    			else
				{
    				cudaMemcpy(p_outframe, p_video->b_image_unet? pDst : p_frame, p_video->i_width* p_video->i_height*(p_video->b_compare+1), cudaMemcpyDeviceToDevice);
				}

    			FrameBufferGetOff(unetframes);
    			FrameBufferPutdown( yoloframes,pts,flag);
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

    cudaFree(pPro);
    cudaFree(pDst);

	return NULL;
}
