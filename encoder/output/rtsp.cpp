/* Copyright (c) 2018-2019 Sigmastar Technology Corp.
 All rights reserved.

  Unless otherwise stipulated in writing, any and all information contained
 herein regardless in any format shall remain the sole proprietary of
 Sigmastar Technology Corp. and be kept in strict confidence
 (��Sigmastar Confidential Information��) by the recipient.
 Any unauthorized act including without limitation unauthorized disclosure,
 copying, use, reproduction, sale, distribution, modification, disassembling,
 reverse engineering and compiling of the contents of Sigmastar Confidential
 Information is unlawful and strictly prohibited. Sigmastar hereby reserves the
 rights to any and all damages, losses, costs and expenses resulting therefrom.
*/
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <string>
#include <math.h>

extern "C"
{
#include "../main.h"
}

#include "BasicUsageEnvironment.hh"
#include "liveMedia.hh"
#include "Live555RTSPServer.hh"

using namespace std;

#define RTSP_LISTEN_PORT        554
static Live555RTSPServer *g_pRTSPServer = NULL;

#define PATH_PREFIX                "/mnt"

int s32LoadIQBin = 1;
#define NONHDR_PATH                "/customer/nohdr.bin"
#define HDR_PATH                "/customer/hdr.bin"

#define ST_MAX_PORT_NUM (5)
#define ST_MAX_SCL_NUM (3)

#define ST_MAX_SENSOR_NUM (3)

#define ST_MAX_STREAM_NUM 4

#define ST_LDC_MAX_VIEWNUM (4)

#define ST_MAX_VPE_INPORT_NUM (2)
#define ST_MAX_VPECHN_NUM (3)
#define ST_VPE_INVAILD (0xFF)

#define ASCII_COLOR_GREEN                        "\033[1;32m"
#define ASCII_COLOR_END                          "\033[0m"
#define ASCII_COLOR_RED                          "\033[1;31m"

#define DBG_INFO(fmt, args...) printf(ASCII_COLOR_GREEN"%s[%d]: " fmt ASCII_COLOR_END, __FUNCTION__,__LINE__, ##args);
#define DBG_ERR(fmt, args...) printf(ASCII_COLOR_RED"%s[%d]: " fmt ASCII_COLOR_END, __FUNCTION__,__LINE__, ##args);
#define PI       acos(-1)


typedef struct ST_VencAttr_s
{
    char szStreamName[128];
    int bUsed;
    int i_id;
}ST_VencAttr_t;

typedef struct Client_s
{
    int i_read_block;
    int i_id;
}Client_t;

static ST_VencAttr_t gstVencattr;

void *ST_OpenStream(char const *szStreamName, void *arg)
{
    AVElement_t*p_video = (AVElement_t*)arg;

    if(strncmp(szStreamName, gstVencattr.szStreamName,
                strlen(szStreamName)))
    {
        //ST_ERR("not found this stream, \"%s\"", szStreamName);
        return NULL;
    }

    Client_t *p_cli = (Client_t*)malloc(sizeof(Client_t));

    p_cli->i_read_block = RecycleBufferBlockReset( &p_video->video_buffer );

    return p_cli;
}


int ST_VideoReadStream(void *handle, unsigned char *ucpBuf, int BufLen, struct timeval *p_Timestamp, void *arg)
{
	AVElement_t*p_video = (AVElement_t*)arg;

    if(handle == NULL)
    {
        return -1;
    }

    Client_t *p_cli = (Client_t*)handle;

	block_t *p_block = RecycleBufferGetBlock( &p_video->video_buffer, p_cli->i_read_block );

	if(p_block)
	{
		int len = p_block->i_buffer;
		memcpy(ucpBuf, p_block->p_buffer, MIN(len, BufLen));

		RecycleBufferGetBlockOff( &p_video->video_buffer, &p_cli->i_read_block);

		return len;
	}

    return 0;
}

int ST_CloseStream(void *handle, void *arg)
{
	//AVElement_t*p_video = (AVElement_t*)arg;

    if(handle == NULL)
    {
        return -1;
    }

    ST_VencAttr_t *pstStreamInfo = (ST_VencAttr_t *)handle;

    //printf("close \"%s\" success\n", pstStreamInfo->szStreamName);

    free(handle);

    return 0;
}

int ST_RtspServerStart(void *pParam)
{
	AVElement_t*p_video = (AVElement_t*)pParam;
	program_t *p_program = p_video->p_program;

    unsigned int rtspServerPortNum = RTSP_LISTEN_PORT;
    int iRet = 0;
    char *urlPrefix = NULL;

    ServerMediaSession *mediaSession = NULL;
    ServerMediaSubsession *subSession = NULL;
    Live555RTSPServer *pRTSPServer = NULL;

    pRTSPServer = new Live555RTSPServer();

    if(pRTSPServer == NULL)
    {
        //ST_ERR("malloc error\n");
        return -1;
    }

    char psw[64]="admin";
    char psz_filename[128]="admin";

    //iRet = pRTSPServer->addUserRecord(psz_filename, psw);

    iRet = pRTSPServer->SetRTSPServerPort(rtspServerPortNum);

    while(iRet < 0)
    {
        rtspServerPortNum++;

        if(rtspServerPortNum > 65535)
        {
            //ST_INFO("Failed to create RTSP server: %s\n", pRTSPServer->getResultMsg());
            delete pRTSPServer;
            pRTSPServer = NULL;
            return -2;
        }

        iRet = pRTSPServer->SetRTSPServerPort(rtspServerPortNum);
    }

    urlPrefix = pRTSPServer->rtspURLPrefix();
	ST_VencAttr_t *pstVencAttr = &gstVencattr;

	pstVencAttr->i_id = 0;
	sprintf(pstVencAttr->szStreamName, "video%d", 0);
	sprintf(p_program->psz_url, "%s%s", urlPrefix, pstVencAttr->szStreamName);

    printf("=================URL===================\n");
    printf("%s%s\n", urlPrefix, pstVencAttr->szStreamName);
    printf("=================URL===================\n");

    pRTSPServer->createServerMediaSession(mediaSession,
    		pstVencAttr->szStreamName,
                                          NULL, NULL);

	if(p_program->i_video_codec == ES_V_H264)
    {
        subSession = WW_H264VideoFileServerMediaSubsession::createNew(
                         *(pRTSPServer->GetUsageEnvironmentObj()),
						 pstVencAttr->szStreamName,
                         ST_OpenStream,
                         ST_VideoReadStream,
                         ST_CloseStream, p_program->f_fps,p_video);
    }
	else if(p_program->i_video_codec == ES_V_H265)
    {
        subSession = WW_H265VideoFileServerMediaSubsession::createNew(
                         *(pRTSPServer->GetUsageEnvironmentObj()),
						 pstVencAttr->szStreamName,
                         ST_OpenStream,
                         ST_VideoReadStream,
                         ST_CloseStream, p_program->f_fps,p_video);
    }


    pRTSPServer->addSubsession(mediaSession, subSession);
    pRTSPServer->addServerMediaSession(mediaSession);

    pRTSPServer->Start();

    g_pRTSPServer = pRTSPServer;

    return 0;
}

int ST_RtspServerStop(void)
{
    if(g_pRTSPServer)
    {
        g_pRTSPServer->Join();
        delete g_pRTSPServer;
        g_pRTSPServer = NULL;
    }

    return 0;
}
