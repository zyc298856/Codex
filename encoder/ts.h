
#ifndef _TS_H
#define _TS_H

#include <stdio.h>
#include "block.h"

#define _OBJECT_DETECT
#define _AV_DEBUG
#define CAMERA_INPUT
#define _RTSP_OUTPUT
//#define _FILE_DEBUG

#define QUEUE_LENGTH  4

typedef enum ESTYPE_{
	ES_V_MPG2=1,
	ES_V_H264,
	ES_V_H265,
	ES_V_MPG4,
	ES_V_RAW,
	ES_V_MAX,
	ES_A_MPG2=1,
	ES_A_RAW,
	ES_A_MAX,
	ES_S_DVB=1,
	ES_S_TEL,
}ESTYPE;

//typedef struct AVElement_ AVElement_t;

#endif //_TS_H
