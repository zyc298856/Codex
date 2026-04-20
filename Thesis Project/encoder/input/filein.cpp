//*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtMxSdiRecorderDemo.cpp *#*#*#*#*#*#*#*# (C) 2015 DekTec
//
// 

//.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
#ifdef _WIN32
    // Windows specific includes
    #include <windows.h>
    #include <conio.h>
#else
    // Linux specific includes
    #include <unistd.h>
    #include <limits.h>
    #include <termios.h>
    #include <sys/select.h>
    #include <pthread.h>
    #include <sys/select.h>
    #include<stdlib.h>
#endif


#include <cuda.h>
#include <cuda_runtime.h>
#include <cuda_profiler_api.h>


extern "C"
{
#include "../main.h"
}

#ifdef _AV_DEBUG
typedef struct av_file_
{
	int b_die;
	pthread_t thid;
}av_file_t;

static void * file_capture_data(void * param)
{
    device_t *p_device = (device_t *)param;
    av_file_t *p_av = (av_file_t *)p_device->priv_data;
    AVElement_t*p_video = p_device->p_video;
    program_t *p_program = p_video->p_program;

	int64_t i_frame = 0;

	//FILE *fp = fopen("/home/nvidia/Videos/5180.yuv", "rb");
	//FILE *fp = fopen("/home/nvidia/Videos/data3.yuv", "rb");
	FILE *fp = fopen(p_program->psz_iurl, "rb");

	double i_send_offset = 0;
	int64_t i_last_send_time = 0;

	int bytes_to_read = p_video->i_height*p_video->i_width;
	unsigned char *addr = (unsigned char *)malloc(p_video->i_height*p_video->i_width*3/2);
	int64_t pts = 1000000;
	while ((fp) &&  !p_av->b_die )
	{
		int b_eos = 0;
		uint8_t *p_outframe = FrameBufferPut( &p_video->m_preframes );
		if(p_outframe)
		{
			int bytes = fread(addr, 1, bytes_to_read, fp);
			if(bytes < bytes_to_read)
			{
				static int times = 0;
				//printf(" ---------%d---------CLOSED\n", times++);

				fseek(fp, 0, 0);
				continue;
				//b_eos = 1;
				//break;
			}

			if(b_eos) break;

		    cudaMemcpy(p_outframe, addr, bytes_to_read, cudaMemcpyHostToDevice);
		    FrameBufferPutdown(&p_video->m_preframes,pts,0);

			pts += p_video->i_frame_period;
			p_video->i_video_pts = pts;
		}
		else
		{
			msleep( (int)1000);
			continue;
		}


		i_frame++;

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

	if(fp)
	fclose(fp);

	free(addr);

	printf("close file_capture_data\n");
	return NULL;
}

static void CloseFileCapture(void *param)
{
    device_t *p_device = (device_t *)param;
    av_file_t *p_av = (av_file_t *)p_device->priv_data;
    //AVElement_t*p_video = p_device->p_video;

	p_av->b_die = 1;
	if(p_av->thid)
	{
		pthread_join(p_av->thid, NULL);
	}


	free(p_av);
}


int StartFileCapture(void *param)
{
	av_file_t *p_av = (av_file_t *)malloc(sizeof(av_file_t));
	device_t *p_device = (device_t *)param;

	p_device->CloseCapture = CloseFileCapture;
	p_device->priv_data = p_av;
	p_av->b_die = 0;

	if(pthread_create(&p_av->thid, NULL, file_capture_data, p_device)!=0)
	fprintf(stderr, "thread create fail: %m\n");

	return 1;
}
#else
int StartFileCapture(void *param)
{
	return 0;
}
#endif

