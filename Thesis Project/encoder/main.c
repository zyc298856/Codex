
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>

#include "ts.h"
#include "block.h"
#include "main.h"
#include "cJSON.h"

static char *video_ecodec[16] = {"copy", "mpeg2", "h264", "h265", "mpeg4"};

static char *input_types[8] = {"null", "av", "file"};

static volatile b_halt = 0;
static volatile sig_atomic_t b_ctrl_c = 0;
static void sigint_handler(int a)
{
    b_ctrl_c = 1;
}

void set_exit()
{
	b_halt = 1;
	b_ctrl_c = 1;
}

void set_status(encoder_t *p_encoder)
{
	program_t *p_program = &p_encoder->m_program;

	if((p_program->i_video_codec > 0 ))
	{
		AVElement_t*p_video = &p_encoder->m_video;
		cJSON *root = cJSON_CreateObject();
		if(root)
		{
			cJSON * js_body;
			char psz_warning[128];
			const char *const body = "status";
			cJSON_AddItemToObject(root, body, js_body=cJSON_CreateObject());
			cJSON_AddNumberToObject(js_body,"id", 0);

			int64_t i_video_delay = p_video->i_video_pts - p_video->video_buffer.i_frame_pts;
			sprintf(psz_warning, "fps:%5.2f delay:%4d ms",
					p_video->i_output_fps,(int)(i_video_delay/1000));
			cJSON_AddStringToObject(js_body,"info", psz_warning);

			char *s = cJSON_PrintUnformatted(root);
			if(s)
			{
				net_t *p_net = &p_encoder->m_net;
				//printf("create js string is %s\n",s);
				if(p_net->i_socket > 0)
				SendCommand(p_net->i_socket, s);

				free(s);
			}

			cJSON_Delete(root);
		}

		p_video->b_save_file = 1;
	}
}

 static void init_encoder(encoder_t *p_encoder)
{
	device_t *p_device = &p_encoder->m_device;
	AVElement_t*p_video = &p_encoder->m_video;
	program_t *p_program = &p_encoder->m_program;

	p_device->p_video = p_video;
	p_video->p_program = p_program;
	p_video->p_net = &p_encoder->m_net;

	p_video->p_net->i_id = p_encoder->i_id;
	p_video->id = p_encoder->i_id;

	BlockBufferInit( &p_video->video_object, 8, 2*1024*1024, 0);
	BlockBufferInit( &p_video->video_buffer, 32, 32*1024*1024, 0);
	//BlockBufferInit( &p_video->video_fifo, 32, 32*1024*1024, 0);

	pthread_mutex_init(&p_video->video_mutex, NULL);

}

static void reset_encoder(encoder_t *p_encoder)
{
	AVElement_t*p_video = &p_encoder->m_video;
	program_t *p_program = &p_encoder->m_program;

	p_video->encode_thid = 0;
	p_video->decode_thid = 0;
	p_video->demux_thid = 0;
	p_video->b_die = 0;
	p_video->init = 0;
	p_video->i_output = 0;

	p_video->i_width = p_program->i_width;
	p_video->i_height= p_program->i_height;
	p_video->b_object_detect = p_program->b_object_detect;
	p_video->b_object_show = p_program->b_object_show;
	p_video->b_image_unet = p_program->b_image_unet;
	p_video->i_filter_type = p_program->i_filter_type;
	p_video->i_max = p_program->i_max;
	p_video->i_min = p_program->i_min;

	p_video->video_buffer.i_frame_pts = 0;
	p_video->video_buffer.i_frame_dts = 0;
	p_video->video_buffer.i_read_block = 0;
	p_video->video_buffer.i_write_block = 0;

	p_video->video_object.i_read_block = 0;
	p_video->video_object.i_write_block = 0;

	p_video->i_frame_period = 40000;

}

static void clean_encoder(encoder_t *p_encoder)
{
	AVElement_t*p_video = &p_encoder->m_video;

	BlockBufferClean(&p_video->video_object);
	BlockBufferClean(&p_video->video_buffer);
	//BlockBufferClean(&p_video->video_fifo);
	pthread_mutex_destroy(&p_video->video_mutex );
}

static void *video_encoder(void * param)
{
	AVElement_t *p_video = (AVElement_t *)param;
	program_t *p_program = p_video->p_program;

	printf("id:%d open %s video encoder\n",
			p_video->id+1, video_ecodec[p_program->i_video_codec&15]);

	if(p_program->i_video_codec == ES_V_H264 ||
		p_program->i_video_codec == ES_V_H265)
		p_video->init = OpenEncoder(p_video);

	return NULL;
}


void *video_decoder(void * param)
{
	AVElement_t *p_video = (AVElement_t *)param;
	program_t *p_program = p_video->p_program;

	block_t *p_data;
	int i_video_codec = p_program->i_video_codec;

	if(i_video_codec > 0)
	{
		int ret = OpenDecoder(p_video);
		if(!ret)
		{
			printf("close %d video decoder loop\n", p_video->i_video_codec);
		}
	}

	return NULL;
}

static void * av_demux(void * param)
{
	encoder_t *p_encoder = (encoder_t *)param;
	AVElement_t*p_video = &p_encoder->m_video;
	program_t *p_program = &p_encoder->m_program;

	printf("id:%d demux url:%s\n",
	p_encoder->i_id+1, p_program->psz_iurl);

	av_loop(p_video, p_program->psz_iurl);


	printf("id:%d close av demux loop\n", p_encoder->i_id+1);

	return NULL;
}

static void open_encoder(encoder_t *p_encoder)
{
	AVElement_t*p_video = &p_encoder->m_video;
	program_t *p_program = &p_encoder->m_program;

	int i_id = p_encoder->i_id;
	int i_input_type = p_program->i_input_type;

	printf("id:%d open encoder:%s\n", i_id+1, input_types[i_input_type&3]);

	reset_encoder(p_encoder);

	if(i_input_type == AV_IN)
	{
		static int (*device_open[8])( void *) = {StartUSBCapture, StartFileCapture};
		device_t *p_device = &p_encoder->m_device;

		p_video->i_width = p_program->i_width;
		p_video->i_height= p_program->i_height;

		if(device_open[p_program->i_device] && device_open[p_program->i_device](p_device))
		{

			printf("id:%d open AV vc:%d %dx%d fps:%.2f\n", i_id+1,
			p_program->i_video_codec, p_video->i_width, p_video->i_height,
			p_program->f_fps);
		}
	}
	else if(i_input_type == STRM_IN )
	{
		pthread_create(&p_video->demux_thid, NULL, av_demux, (void *)p_encoder);
	}

	p_video->f_fps = p_program->f_fps;
	p_video->i_frame_period = 1000000.0/p_program->f_fps;
	p_video->b_compare = p_program->b_compare;
	p_video->f_prob = p_program->f_prob;
	p_video->b_fps_changed = 0;
	p_video->b_save_file = 0;

	FrameBufferInit( &p_video->m_preframes, 4, p_video->i_width*p_video->i_height);
	FrameBufferInit( &p_video->m_unetframes, 4, p_video->i_width*p_video->i_height*(p_video->b_compare+1));
	FrameBufferInit( &p_video->m_yoloframes, 4, p_video->i_width*p_video->i_height*(p_video->b_compare+1)*3/2);
	FrameBufferInit( &p_video->m_encframes, 4, p_video->i_width*p_video->i_height*(p_video->b_compare+1)*3/2);

	pthread_create(&p_video->preprothid,NULL,video_preprocessing,(void *)p_video);
	pthread_create(&p_video->yolothid,NULL,video_yolo,(void *)p_video);
	pthread_create(&p_video->unethid,NULL,video_unet,(void *)p_video);
	pthread_create(&p_video->encode_thid,NULL,video_encoder,(void *)p_video);

	if(p_program->i_output_type == URL_OUT)
	{
#ifdef _RTSP_OUTPUT
		ST_RtspServerStart(p_video);
#endif
	}


}

static void close_encoder(encoder_t *p_encoder)
{
	AVElement_t*p_video = &p_encoder->m_video;
	device_t *p_device = &p_encoder->m_device;

	printf("id:%d close previous encoder\n", p_encoder->i_id+1);

	p_video->b_die = 1;
	p_video->i_output = 0;
	if( p_device->priv_data)
	{
		p_device->CloseCapture(p_device);
		p_device->priv_data = NULL;
	}

	if(p_video->demux_thid > 0)
	{
		pthread_join(p_video->demux_thid, NULL);
		p_video->demux_thid = 0;
	}

	if(p_video->encode_thid > 0)
	{
		pthread_join(p_video->encode_thid, NULL);
		p_video->encode_thid = 0;
	}

	if(p_video->preprothid > 0)
	{
		pthread_join(p_video->preprothid, NULL);
		p_video->preprothid = 0;
	}

	if(p_video->unethid > 0)
	{
		pthread_join(p_video->unethid, NULL);
		p_video->unethid = 0;
	}

	if(p_video->yolothid > 0)
	{
		pthread_join(p_video->yolothid, NULL);
		p_video->yolothid = 0;
	}

	FrameBufferClean(&p_video->m_preframes);
	FrameBufferClean(&p_video->m_unetframes);
	FrameBufferClean(&p_video->m_yoloframes);
	FrameBufferClean(&p_video->m_encframes);

	program_t *p_program = &p_encoder->m_program;
	if(p_program->i_output_type == URL_OUT)
	{
#ifdef _RTSP_OUTPUT
		ST_RtspServerStop();
#endif
	}

}

//#define _TEST_

int main(int argc, char** argv)
{
	pthread_t thid = 0;
	uint32_t id = argc>1? atoi(argv[1]) : 0;
#ifdef _TEST_
	while(1) {
#endif
	int *fd = open_tcp( id);
	if(fd == NULL) return 0;

	encoder_t*p_encoder = init_param(id);
	net_t *p_net = &p_encoder->m_net;

	init_encoder(p_encoder);

	int times = 0;
	open_encoder(p_encoder);

	p_net->fd = fd;
	pthread_create(&thid, NULL, tcp_thread, (void *)p_encoder);

    if (signal(SIGINT, sigint_handler) == SIG_ERR)
       printf("Unable to register CTRL+C + q handler: %s\n", strerror(errno));

	while(!b_ctrl_c)
	{
		usleep(10000);
#ifdef _TEST_
		if(times++ > 4000) break;
#endif
	}

	close_encoder(p_encoder);
	clean_encoder(p_encoder);

	p_net->b_die = 1;

	if(thid > 0)
	{
		pthread_join(thid, NULL);
	}

	close_param(p_encoder);

    printf("encoder exit\n");
#ifdef _TEST_
	}
#endif

	if(b_halt)
	{
		own_system("shutdown -h now");
		own_system("halt");
	}
    return 0;
}
