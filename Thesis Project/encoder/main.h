
#ifndef _MAIN_H
#define _MAIN_H

#include <stdio.h>
#include "block.h"
#include "util.h"
#include "ts.h"

#define INPUT_MAX 8
#define MAX_PM   128
#define MAX_UDP  16
#define MAX_PRA  128
//#define YOLOV5
#define YOLOV10

enum
{
    CMD_PARAM = 1,
    CMD_STATUS= 2,
    CMD_LOGS  = 3,
    CMD_EXIT  = 4    
};

enum
{
	NULL_IN = 0,
    AV_IN   = 1,
	STRM_IN = 2
};

enum
{
	NULL_OUT = 0,
	URL_OUT  = 1,
};


typedef struct program_
{
	/* Video Properties */
	double			f_fps;
	double			f_prob;
	uint16_t        i_width;
	uint16_t        i_height;
	uint16_t        i_video_bitrate;

	uint8_t         i_video_codec;
	uint8_t         i_keyint_max;       /* Force an IDR keyframe at this interval */
	uint8_t         i_rc_method;    /* X264_RC_* */
    uint8_t         i_depth;

    uint8_t  	    b_image_unet;
    uint8_t  	    i_filter_type;
	uint8_t			b_object_detect;
	uint8_t			b_object_show;
	uint8_t			b_compare;

	uint8_t  	    i_input_type;
    uint8_t         i_device;
	uint8_t  	    i_output_type;

	uint8_t  	    i_max;
	uint8_t  	    i_min;
	char            psz_iurl[256];
 	char            psz_url[256];
 	char            psz_dir[255];

} program_t;

typedef struct net_
{
	int      i_id;
	int      i_socket;
	int 	*fd;
	volatile int b_die;
}net_t;

typedef struct AVElement_
{
	volatile int  b_die;
	int id;
	int init;
	int i_output;
	int i_width;
	int i_height;
	int i_video_codec;
	int i_filter_type;
	int b_image_unet;
	int b_object_detect;
	int b_object_show;
	int b_fps_changed;
	int i_max;
	int i_min;
	int b_compare;
	int b_save_file;
	int64_t i_video_pts;
	float   i_input_fps;
	float   i_output_fps;
	double 	f_fps;
	double	f_prob;
	double 	i_frame_period;
	recycle_block_buffer_t video_object;
	recycle_block_buffer_t video_buffer;
	recycle_block_buffer_t video_fifo;
	recycle_frame_t m_preframes;
	recycle_frame_t m_unetframes;
	recycle_frame_t m_yoloframes;
	recycle_frame_t m_encframes;
	pthread_t  preprothid;
	pthread_t  unethid;
	pthread_t  yolothid;
	pthread_t  encode_thid;
	pthread_t  decode_thid;
	pthread_t  demux_thid;
	pthread_mutex_t video_mutex;
	program_t *p_program;
	net_t *p_net;
}AVElement_t;

typedef struct device_
{
	AVElement_t*p_video;
	void *priv_data;
	void (*CloseCapture)(void *param);
}device_t;

typedef struct encoder_
{
  int         i_id;
  net_t		  m_net;
  device_t    m_device;
  AVElement_t m_video;
  program_t   m_program;
}encoder_t;


int  StartFileCapture(void *param);
int  StartUSBCapture(void *param);

void *video_decoder(void * param);
int  copybuffer(AVElement_t*p_video, block_t *p_block);
int  OpenEncoder(void *param);
int  OpenDecoder(void *param);

encoder_t * init_param(int id);
void close_param(encoder_t *avs_param);
int  own_system(const char * cmd);

int  set_param(encoder_t *p_encoder, char *psz);
void output_param(encoder_t *p_encoder, char *psz_cmd);
void output_roi(AVElement_t*p_video, float *prob);

int  *open_tcp(int id);
void *tcp_thread(void *param);

void SendCommand(int i_socket, char *p_buffer);
void set_status(encoder_t *p_encoder);
void set_exit();

void *video_preprocessing(void * param);
void *video_unet(void * param);
void *video_yolo(void * param);

void*open_yolo(int i_width, int i_height);
int  yolo_infer(AVElement_t*p_video, void *param, unsigned char*src, int,  int);
void close_yolo(void *param);

int ST_RtspServerStart(void *pParam);
int ST_RtspServerStop(void);
#endif //_MAIN_H
