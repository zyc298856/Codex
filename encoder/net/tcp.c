#include <stdio.h>
#include <string.h>
#include "main.h"
#include "network.h"
#include "cJSON.h"


#define MAX_LINE_LENGTH 2048

int ReadCommand( int i_socket, char *p_buffer, int *pi_size )
{
    int i_read = 0, i_size = 0;

    while( i_socket > 0 && i_size < MAX_LINE_LENGTH &&
           (i_read = net_ReadNonBlock( i_socket,
                  (uint8_t *)p_buffer + i_size, 1, 10000 ) ) > 0 )
    {
        if( p_buffer[ i_size ] == '\r' || p_buffer[ i_size ] == '\n' )
            break;

        i_size++;
    }

    *pi_size = i_size;
    /* Connection closed */
    if( i_read == -1 )
    {
        p_buffer[ i_size ] = 0;
        return -1;
    }

    if( i_size == MAX_LINE_LENGTH ||
        p_buffer[ i_size ] == '\r' || p_buffer[ i_size ] == '\n' )
    {
        p_buffer[ i_size ] = 0;
        return 1;
    }

    return 0;
}


int WriteCommand(int i_socket, char *p_buffer, int i_size )
{
    int i_send = 0;
    int i_total = 0;

    while( i_socket > 0 && i_total < i_size &&
           (i_send = net_WriteNonBlock( i_socket,
                  (uint8_t *)p_buffer + i_total, i_size - i_total, 10000 ) ) > 0 )
    {
    	i_total += i_send;
    	//printf("times=%d %d %s", i_times, i_total, p_buffer);
    }

    /* Connection closed */
    if( i_send == -1 )
    {
    	net_Close( i_socket );
        return -1;
    }

    //printf("times=%d %s\n", i_times, p_buffer);
    return i_total;
}


void SendCommand(int i_socket, char *p_buffer)
{
	int len = strlen(p_buffer);
	p_buffer[len] = '\n';
	p_buffer[len+1] = 0;
	len += 1;
	WriteCommand(i_socket, p_buffer, len );
}

enum macroType
{
	RC_UNKNOWN = 0,
	RC_PARAMETER,
	RC_STATUS,
	RC_EXIT,
	RC_LOG,
	RC_INFO,
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

	{ "log",            RC_LOG },
	{ "info",           RC_INFO },
	/* end */
	{ NULL,             RC_UNKNOWN }
};

static int NetStrToMacroType( char *name)
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
				cJSON_Delete(root);
				return StrToMacroTypeTab[i].i_type;
			}
		}

		cJSON_Delete(root);
	}
    return RC_UNKNOWN;
}

int *open_tcp(int id)
{
	int   i_port = 9736 + id;
    int *fd = net_ListenTCP( NULL, i_port );
    if( fd == NULL )
    {
        return NULL;
    }

    return fd;
}

void * tcp_thread(void *param)
{
	encoder_t*p_encoder = (encoder_t*)param;
	net_t *p_net = &p_encoder->m_net;

	int   i_size = 0;
	int   i_socket = 0;
	char  *p_buffer = malloc( MAX_LINE_LENGTH + 1 );

	memset(p_buffer, 0,  MAX_LINE_LENGTH + 1 );

	while(!p_net->b_die)
	{
		char *psz_cmd;
		int b_complete = 0;

		if(i_socket <= 0 )
		{
			i_socket =
				net_Accept( p_net->fd, 100000 );

			if(i_socket != -1 )
			{
				char psz_addr[64]="";
				struct sockaddr_in addr_in;
				int nSize = sizeof(addr_in);

				//getsockname(i_socket, (struct sockaddr *)&addr_in, &nSize);//获取connfd表示的连接上的本地地址
			    getpeername(i_socket,(struct sockaddr *)&addr_in, &nSize);
				strncpy(psz_addr, inet_ntoa(addr_in.sin_addr), 16);
				psz_addr[15] = 0;

				if(p_net->i_socket > 0)
				net_Close( p_net->i_socket );
				p_net->i_socket = i_socket;
			}
		}

		if(i_socket > 0)
			b_complete = ReadCommand( i_socket, p_buffer, &i_size );

		/* Is there something to do? */
		if( b_complete <= 0 || i_size <= 0)
		{
			if( b_complete < 0)
			{
				p_net->i_socket = -1;
				net_Close( i_socket );
				i_socket = -1;
			}

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

		//printf("macro id=`%s' from encoder\n", psz_cmd );
		switch( NetStrToMacroType(psz_cmd ) )
		{
		case RC_PARAMETER:
			{
				if (strstr(psz_cmd, "GET"))
				{
					output_param(p_encoder, p_buffer);
					WriteCommand(p_net->i_socket, p_buffer, strlen(p_buffer) );
				}
				else if (strstr(psz_cmd, "POST"))
				{
					set_param(p_encoder, psz_cmd);
				}
			}
			break;
		case RC_STATUS:
			{
				set_status(p_encoder);
			}
			break;
		case RC_EXIT:
			{
				set_exit();
			}
			break;
		default:
			printf("invalid macro id=`%s'\n", psz_cmd );
			break;
		}

		if(i_size > 0)
		memset(p_buffer, 0,  i_size + 1 );
		i_size = 0;
	}

	if(i_socket > 0)
	net_Close( i_socket );

	net_ListenClose(p_net->fd);

	free(p_buffer);

	return NULL;
}

