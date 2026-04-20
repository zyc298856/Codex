#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

extern "C"
{
#include "../main.h"
}

#include <nvbuf_utils.h>

#include <cuda.h>
#include <cuda_runtime.h>
#include <cuda_profiler_api.h>
#include "cudaEGL.h"

#ifdef CAMERA_INPUT

#include <libusb-1.0/libusb.h>
#include "usbcam.h"
typedef struct ximea_
{
	//int i_id;
	int b_die;
	int i_width;
	int i_height;
	//int nDataSize;
	//float framerate;
	pthread_t mixea_id;
}usbcap_t;


static void CloseUSBCapture(void *param);

//#define DEBUG_V4L2
#ifdef DEBUG_V4L2
FILE *fp;
#endif



static void *video_capture_proc(void *data)
{
    device_t *p_device = (device_t *)data;
	usbcap_t *p_usbcap = (usbcap_t *)p_device->priv_data;
    AVElement_t*p_video = p_device->p_video;

    std::vector<usbcam::usbdev*> devlist = usbcam::scanUSBDevices();
    std::cout<<"devices nums :"<<devlist.size()<<std::endl;

	if(devlist.size() < 1)
	{
		printf("ERROR:open_video_encoder failed!\n");
		return (void *)0;
	}

    devlist[0]->init(p_usbcap->i_height, p_usbcap->i_width, 1);
    devlist[0]->start();

#ifdef DEBUG_V4L2
       fp = fopen("cap.rgb", "wb");
#endif

    unsigned char *addr = (unsigned char *)malloc(p_usbcap->i_height*p_usbcap->i_width);
    //memset(addr, 128, p_usbcap->i_height*p_usbcap->i_width*3/2);
    int64_t pts = 1000000;
	double i_send_offset = 0;
	int64_t i_last_send_time = 0;
	while(!p_usbcap->b_die)
	{
		int stat = devlist[0]->getimage(addr,3000);
		if (stat)
		{
			uint8_t *p_outframe = FrameBufferPut( &p_video->m_preframes );
			if(p_video->i_output >= 4 && p_outframe)
			{
			    cudaMemcpy(p_outframe, addr, p_usbcap->i_height*p_usbcap->i_width, cudaMemcpyHostToDevice);
			    FrameBufferPutdown(&p_video->m_preframes,pts, 0);
			}

			pts += p_video->i_frame_period;
			p_video->i_video_pts = pts;

			i_send_offset += p_video->i_frame_period;

			int64_t i_current = mdate();
			if(!i_last_send_time)
			i_last_send_time = i_current;
			if(i_current > i_last_send_time)
			i_send_offset -= (i_current - i_last_send_time);
			i_last_send_time = i_current;

			if(i_send_offset > 1)
			msleep( (int)i_send_offset);
		}
		else
		{
			msleep( (int)1000);
		}
	}

	free(addr);

    devlist[0]->stop();

	return (void *)0;
}

int StartUSBCapture(void *param)
{
	device_t *p_device = (device_t *)param;
	AVElement_t*p_video = p_device->p_video;

	usbcap_t *p_usbcap = (usbcap_t *)malloc(sizeof(usbcap_t));
	memset(p_usbcap, 0, sizeof(usbcap_t));

	p_usbcap->i_width = p_video->i_width;
	p_usbcap->i_height = p_video->i_height;
	//p_usbcap->framerate = 1000000.0/p_video->i_frame_period;
	p_device->CloseCapture = CloseUSBCapture;
	p_device->priv_data = p_usbcap;

	pthread_create(&p_usbcap->mixea_id,NULL,video_capture_proc,(void *)p_device);
	return 1;
}


static void CloseUSBCapture(void *param)
{
    device_t *p_device = (device_t *)param;
    usbcap_t *p_usbcap= (usbcap_t *)p_device->priv_data;
    //AVElement_t*p_video = p_device->p_video;

	p_usbcap->b_die = 1;
	if(p_usbcap->mixea_id)
	{
		pthread_join(p_usbcap->mixea_id, NULL);
	}

	free(p_usbcap);

}
#else
int StartUSBCapture(void *param)
{
	return 0;
}
#endif
