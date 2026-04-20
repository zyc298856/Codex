/*****************************************************************************
 * tcp.c: net links encoder
 *****************************************************************************
 * Copyright (C) 2016 avs+ project
 * $Id: tcp.c 8973 2016-4-21 13:09:42Z gbazin $
 **
 **  Authors: Guoping Li <li.gp@avsgm.com>
*****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#include <stdlib.h>
#include <math.h>

#include <signal.h>
#define _GNU_SOURCE

#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <termios.h>

#include <ifaddrs.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include "network.h"
#include "http.h"
#include "base64.h"
#include "cJSON.h"

#ifndef _MSC_VER
#include <sys/time.h>
#else
#include <sys/types.h>
#include <sys/timeb.h>
#endif
#include <time.h>


//#define _CMD_TEST

enum macroType
{
	RC_UNKNOWN = 0,
	RC_PARAMETER,
	RC_PROGRAM,
	RC_DEVICE,
	RC_COMPLEXITY,
	RC_STATUS,
	RC_EXIT,
	RC_CLEAR,
	RC_RATE,
	RC_INFO,
	RC_LOG,
	MVLC_VALUE
};

/* Static mapping of macros type to macro strings */
static struct
{
    char *psz_name;
    int  i_type;
}
StrToMacroTypeTab [] =
{
	{ "parameter",     RC_PARAMETER },
	{ "status",         RC_STATUS},
	{ "exit",	    	RC_EXIT },
	{ "clear",          RC_CLEAR },
	{ "log",            RC_LOG },
	/* end */
	{ NULL,             RC_UNKNOWN }
};


#define LOAD_NODED(js_lists, js_nodes, i_type, key) \
    js_nodes = cJSON_GetObjectItem(js_lists, key);\
    if(js_nodes)\
	{\
   		i_type = js_nodes->valuedouble;\
	}

#define LOAD_NODES(js_lists, js_nodes, i_type, key) \
    js_nodes = cJSON_GetObjectItem(js_lists, key);\
    if(js_nodes)\
	{\
    	strcpy(i_type, js_nodes->valuestring);\
	}

#define LOAD_NODE(js_lists, js_nodes, i_type, key) \
    js_nodes = cJSON_GetObjectItem(js_lists, key);\
    if(js_nodes)\
	{\
   		i_type = js_nodes->valueint;\
	}


static int NetStrToMacroType(intf_sys_t *p_sys,  char *name )
{
    if( !name || *name == '\0')
    {
        return RC_UNKNOWN;
    }

	cJSON *root = cJSON_Parse(name);
	if(root)
	{
		int i;
		for( i = 0; StrToMacroTypeTab[i].psz_name != NULL; i++ )
		{
			cJSON *js_list = cJSON_GetObjectItem(root, StrToMacroTypeTab[i].psz_name);
			if( js_list )
			{
				int id=0;
				cJSON *js_item;
				if(RC_STATUS == StrToMacroTypeTab[i].i_type)
				{
					//LOAD_NODE(js_list, js_item, id, "id")
					LOAD_NODES(js_list, js_item, p_sys->params[id&(MAX_PM-1)].stream_state, "info")
				}

				cJSON_Delete(root);
				return StrToMacroTypeTab[i].i_type;
			}
		}

		cJSON_Delete(root);
	}

    return RC_UNKNOWN;
}

int analysis_parameters(intf_sys_t *p_sys, char *psz_cmd)
{
	int i, id = 0;
	cJSON *root = cJSON_Parse(psz_cmd);
	if(root)
	{
		cJSON *js_node;
		cJSON *js_item;
		{
			cJSON*js_list = cJSON_GetObjectItem(root, "parameter");
			if(js_list)
			{
				http_param_sys_t*p_program;

				LOAD_NODE(js_list, js_node, p_sys->i_number, "pm_num")
				p_sys->i_number = MIN(MAX_PM, MAX(1, p_sys->i_number));

				LOAD_NODE(js_list, js_node, id, "id")
				p_program = &p_sys->params[id];

				LOAD_NODES(js_list, js_node, p_program->psz_url, "url")
				LOAD_NODE(js_list, js_node, p_program->b_object_detect, "objectDetect")
				LOAD_NODE(js_list, js_node, p_program->b_object_show, "objectShow")
				LOAD_NODE(js_list, js_node, p_program->i_filter_type, "filterType")
				LOAD_NODE(js_list, js_node, p_program->i_input_type, "inputType")
				LOAD_NODE(js_list, js_node, p_program->b_image_unet, "unet")
				LOAD_NODED(js_list, js_node, p_program->i_max, "hmax")
				LOAD_NODED(js_list, js_node, p_program->i_min, "hmin")
				LOAD_NODED(js_list, js_node, p_program->f_fps, "fps")

			}
		}

		cJSON_Delete(root);
	}

	return id;
}


void output_program(intf_sys_t *p_sys, int id, char *psz_cmd)
{
	http_param_sys_t *p_program = &p_sys->params[id];
	cJSON *root = cJSON_CreateObject();
	if(root)
	{
		cJSON * js_body ;
		const char *const body = "parameter";

		cJSON_AddItemToObject(root, body, js_body=cJSON_CreateObject());
		cJSON_AddStringToObject(js_body,"action","POST");
		cJSON_AddNumberToObject(js_body,"id",id);

		cJSON_AddNumberToObject(js_body,"filterType", p_program->i_filter_type);
		cJSON_AddNumberToObject(js_body,"unet", p_program->b_image_unet);
		cJSON_AddNumberToObject(js_body,"objectShow", p_program->b_object_show);
		cJSON_AddNumberToObject(js_body,"objectDetect", p_program->b_object_detect);
		cJSON_AddNumberToObject(js_body,"hmax", p_program->i_max);
		cJSON_AddNumberToObject(js_body,"hmin", p_program->i_min);
		cJSON_AddNumberToObject(js_body,"fps", p_program->f_fps);

		char *s = cJSON_PrintUnformatted(root);
		if(s)
		{
			int len = strlen(s);
			memcpy(psz_cmd, s, len);
			psz_cmd[len] = '\n';
			psz_cmd[len+1] = 0;
			free(s);
		}

		cJSON_Delete(root);
	}
}

int ReadCommand( int i_socket, char *p_buffer, int *pi_size )
{
    int i_read = 0;

    while( i_socket > 0 && *pi_size < MAX_LINE_LENGTH &&
           (i_read = net_ReadNonBlock( i_socket,
                  (uint8_t *)p_buffer + *pi_size, 1, 10000 ) ) > 0 )
    {
        if( p_buffer[ *pi_size ] == '\r' || p_buffer[ *pi_size ] == '\n' )
            break;

        (*pi_size)++;
    }

    /* Connection closed */
    if( i_read == -1 )
    {
        p_buffer[ *pi_size ] = 0;
        return -1;
    }

    if( *pi_size == MAX_LINE_LENGTH ||
        p_buffer[ *pi_size ] == '\r' || p_buffer[ *pi_size ] == '\n' )
    {
        p_buffer[ *pi_size ] = 0;
        return 1;
    }

    return 0;
}

int WriteCommand( int i_socket, char *p_buffer, int i_size )
{
    int i_send = 0;
    int i_total = 0;

    while( i_socket > 0 && i_total < i_size &&
           (i_send = net_WriteNonBlock( i_socket,
                  (uint8_t *)p_buffer + i_total, i_size - i_total, 10000 ) ) > 0 )
    {
    	i_total += i_send;
    }

    /* Connection closed */
    if( i_send == -1 )
    {
    	net_Close( i_socket );
        return -1;
    }

    return i_total;
}

int main(int argc, char** argv)
{
	intf_sys_t    *http_sys;

	char  *p_buffer = malloc( MAX_LINE_LENGTH + 1 );
    int   i_size = 0;
	char *psz_host = "127.0.0.1";
	int   i_port = 9736;
	int   i_port_for_web = 8080;
	int   fd = 0;
	int   i;
	int64_t i_time = 0;

	char *tail;
	readlink ("/proc/self/exe", p_buffer, 256);
	tail=strrchr(p_buffer,'//');
	if(tail) *(tail+1) = 0;
	if(argc > 1)
	i_port_for_web = atoi(argv[1]);

	http_sys = http_open( p_buffer, i_port_for_web );

	memset(p_buffer, 0,  MAX_LINE_LENGTH + 1 );

	while(1)
	{
		char *psz_cmd;
		int b_complete = 0;

		if(fd > 0)
			b_complete = ReadCommand( fd, p_buffer, &i_size );		

		/* Is there something to do? */
		if( b_complete <= 0) 
		{
			if( b_complete < 0)
			{
				net_Close( fd );
				fd = -1;
			}

			for(i = 0; i < http_sys->i_number; i++)
			{
				if(http_sys->params[i].status_change == 2 &&
					i_time - http_sys->params[i].last_time > 10000)
				{
					http_sys->params[i].last_time = i_time;
					http_sys->params[i].status_change = 0;
				}
				else if(http_sys->params[i].status_change == 1 &&
						i_time - http_sys->params[i].last_time > 20000)
				{
					net_Close( fd );
					fd = -1;
					http_sys->params[i].last_time = i_time;
					http_sys->params[i].status_change = 0;
				}

				if(fd <= 0 &&
				  (http_sys->params[i].param_change ||
				   http_sys->params[i].param_request ||
				   http_sys->params[i].program_request ||
				   http_sys->params[i].status_request))
				{
					fd =  net_ConnectTCP(  psz_host, i_port );
					if(fd <= 0)
					{
						i_time += 100;
						usleep(100000);
						continue;
					}
				}

				if(fd > 0)
				{
					if(http_sys->params[i].param_change)
					{
						output_program(http_sys, i, p_buffer);
						//printf("%s\n", p_buffer);
						WriteCommand( fd, p_buffer, strlen(p_buffer) );
						http_sys->params[i].param_change = 0;
						http_sys->params[i].status_change = 0;
						break;
					}
					else if(http_sys->params[i].param_request)
					{
						sprintf(p_buffer, "{\"parameter\":{\"action\":\"GET\",\"id\":%d}}\n", i);
						WriteCommand( fd, p_buffer, strlen(p_buffer));
						http_sys->params[i].param_request = 0;
						break;
					}
					else if(http_sys->params[i].status_request)
					{
						sprintf(p_buffer, "{\"status\":{\"action\":\"GET\",\"id\":%d}}\n", i);
						WriteCommand( fd, p_buffer, strlen(p_buffer));
						http_sys->params[i].status_request = 0;

						break;
					}
					else if(http_sys->params[i].system_request)
					{
						sprintf(p_buffer, "{\"exit\":{\"action\":\"SET\",\"id\":%d}}\n", i);
						WriteCommand( fd, p_buffer, strlen(p_buffer));
						http_sys->params[i].system_request = 0;

						break;
					}
				}
			}

			i_time += 10;
			i_size = 0; 
			usleep(10000);

			continue;
		}

		/* Skip heading spaces */
		psz_cmd = p_buffer;
		while( *psz_cmd == ' ' || *psz_cmd == '\r' || *psz_cmd == '\n')
		{
			psz_cmd++;
		}

		switch( NetStrToMacroType(http_sys, psz_cmd ) )
		{
		case RC_PARAMETER:
			{
				int iid = analysis_parameters(http_sys, psz_cmd);
				http_sys->params[iid&(MAX_PM-1)].status_change = 2;
				http_sys->params[iid].last_time = i_time;
			}
			break;
		case RC_STATUS:
			break;
		case RC_PROGRAM:
			break;
		case RC_DEVICE:
			break;
		default:
			printf("invalid macro id=`%s'\n", psz_cmd );
			break;
		}

		if(i_size > 0)
		memset(p_buffer, 0,  i_size + 1 );
		i_size = 0;
	}


	if(fd > 0)
	net_Close( fd );

	http_close (http_sys);

	free(p_buffer);
}


