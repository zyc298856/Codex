/*****************************************************************************
 * vlc_httpd.h: builtin HTTP/RTSP server.
 *****************************************************************************
 * Copyright (C) 2004 the VideoLAN team
 * $Id: vlc_httpd.h 12408 2005-08-26 18:15:21Z massiot $
 *
 * Authors: Laurent Aimar <fenrir@via.ecp.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/

#ifndef _VLC_HTTPD_H
#define _VLC_HTTPD_H 1

#include "inttypes.h"  // 为啥不是<>

#   include <unistd.h>
#   include <fcntl.h>

#if defined( UNDER_CE )     // 如果编译目标是 Windows CE（UNDER_CE），则使用 winsock.h
#   include <winsock.h>
#elif defined( WIN32 )      // 如果是普通 Windows 平台（WIN32），则使用 winsock2.h 和 ws2tcpip.h
#   include <winsock2.h>
#   include <ws2tcpip.h>
#else
#   include <netdb.h>       /* hostent ... 对于其他平台（如 Linux、macOS），使用 POSIX 标准的网络头文件（如 netdb.h、sys/socket.h 等）*/
#   include <sys/socket.h>
/* FIXME: should not be needed 暂时保留以确保兼容性*/
#   include <netinet/in.h>
#   include <arpa/inet.h>                    /* inet_ntoa(), inet_aton() */
#endif

#define         MIN(A, B)       ((A) < (B) ? (A) : (B)) // 返回两个值中的较小值
#define         MAX(A, B)       ((A) > (B) ? (A) : (B)) // 返回两个值中的较大值

typedef int8_t vlc_bool_t;  // 基于int8_t定义布尔类型
typedef struct httpd_client_t   httpd_client_t;

typedef struct httpd_message_t
{
    httpd_client_t *cl; /* NULL if not throught a connection e vlc internal 当前消息是否通过连接发送*/

    int     i_type;    // 消息类型
    int     i_proto;   // 协议类型
    int     i_version; // 协议版本

    /* for an answer */
    int     i_status;  // 状态响应码
    char    *psz_status;// 状态码对应的文本描述


    /* for a query */
    char    *psz_url;  // 请求的URL
    /* FIXME find a clean way to handle GET(psz_args)
       and POST(body) through the same code */
    uint8_t *psz_args; // 查询参数（GET 请求）或请求体（POST 请求）


    /* for rtp over rtsp */
    int     i_channel;  // RTP 通道编号


    /* options */
    int     i_name;  // 选项名称的数量
    char    **name;  // 选项名称数组

    int     i_value; // 选项值的数量
    char    **value; // 选项值数组


    /* body */
    int64_t i_body_offset;  // 消息体的偏移量
    int     i_body;         // 消息体的长度

    uint8_t *p_body;        // 消息体数据

}httpd_message_t;

//typedef struct httpd_t          httpd_t;
typedef struct httpd_host_t     httpd_host_t;
typedef struct httpd_url_t      httpd_url_t;
//typedef struct httpd_client_t   httpd_client_t;
typedef struct httpd_callback_sys_t httpd_callback_sys_t;
//typedef struct httpd_message_t  httpd_message_t;

// 用于注册 HTTP 回调函数，处理客户端请求
typedef int    (*httpd_callback_t)( httpd_callback_sys_t *, httpd_client_t *, httpd_message_t *answer, httpd_message_t *query );
typedef struct httpd_file_t     httpd_file_t;
typedef struct httpd_file_sys_t httpd_file_sys_t;

// 用于处理文件相关的请求，例如读取文件内容
typedef int (*httpd_file_callback_t)( httpd_file_sys_t *, httpd_file_t *, uint8_t *psz_request, uint8_t **pp_data, int *pi_data );
typedef struct httpd_handler_t  httpd_handler_t;
typedef struct httpd_handler_sys_t httpd_handler_sys_t;

// 用于处理复杂的 HTTP 请求，例如动态生成内容或处理特定协议
typedef int (*httpd_handler_callback_t)( httpd_handler_sys_t *, httpd_handler_t *, uint8_t *psz_url, uint8_t *psz_request, int i_type, uint8_t *p_in, int i_in, char *psz_remote_addr, char *psz_remote_host, uint8_t **pp_data, int *pi_data );
typedef struct httpd_redirect_t httpd_redirect_t;
typedef struct httpd_stream_t httpd_stream_t;

typedef struct vlc_acl_t vlc_acl_t;


#define VLC_EEXIT         -255                             /* Program exited */
#define VLC_EGENERIC      -666                              /* Generic error */

 /*****************************************************************************
 * Booleans
*****************************************************************************/
#define VLC_FALSE 0  // 操作成功
#define VLC_TRUE  1  // 内存不足
#define VLC_SUCCESS         -0                                   /* No error */
#define VLC_ENOMEM          -1                          /* Not enough memory */
#define COPYRIGHT_MESSAGE "GMT Encoder Web Administrator - (c) 2016-- GMT"
#define VERSION_MESSAGE "GMT Encoder Web Administrator"

#if defined( WIN32 ) && !defined( UNDER_CE )
typedef unsigned (WINAPI *PTHREAD_START) (void *);
#endif

typedef struct
{
#if defined( WIN32 ) && !defined( UNDER_CE )
	/* WinNT/2K/XP implementation */
	HANDLE              mutex;
	/* Win95/98/ME implementation */
	CRITICAL_SECTION    csection;
#else
	pthread_mutex_t mutex;
#endif

} vlc_mutex_t;

/* NEVER touch that, it's here only because src/misc/objects.c
 * need sizeof(httpd_t) */
typedef struct httpd_t
{
		int b_die;
		int b_dead;
}httpd_t;

enum
{
    HTTPD_MSG_NONE,

    /* answer */
    HTTPD_MSG_ANSWER,

    /* channel communication */
    HTTPD_MSG_CHANNEL,

    /* http request */
    HTTPD_MSG_GET,
    HTTPD_MSG_HEAD,
    HTTPD_MSG_POST,

    /* rtsp request */
    HTTPD_MSG_OPTIONS,
    HTTPD_MSG_DESCRIBE,
    HTTPD_MSG_SETUP,
    HTTPD_MSG_PLAY,
    HTTPD_MSG_PAUSE,
    HTTPD_MSG_TEARDOWN,

    /* just to track the count of MSG */
    HTTPD_MSG_MAX
};

enum
{
    HTTPD_PROTO_NONE,
    HTTPD_PROTO_HTTP,
    HTTPD_PROTO_RTSP,
};

#if 0
typedef struct httpd_message_t
{
    httpd_client_t *cl; /* NULL if not throught a connection e vlc internal */

    int     i_type;
    int     i_proto;
    int     i_version;

    /* for an answer */
    int     i_status;
    char    *psz_status;

    /* for a query */
    char    *psz_url;
    /* FIXME find a clean way to handle GET(psz_args)
       and POST(body) through the same code */
    uint8_t *psz_args;

    /* for rtp over rtsp */
    int     i_channel;

    /* options */
    int     i_name;
    char    **name;
    int     i_value;
    char    **value;

    /* body */
    int64_t i_body_offset;
    int     i_body;
    uint8_t *p_body;

}httpd_message_t;
#endif

/* each host run in his own thread 
主机管理 httpd_host_t
每个主机运行在独立线程中
管理监听套接字和端口
维护 URL 注册表和客户端连接*/
struct httpd_host_t
{
    //VLC_COMMON_MEMBERS
		int b_dead;
#if defined( WIN32 ) && !defined( UNDER_CE )		
	  HANDLE      thread_id;
#else
		pthread_t thread_id;
#endif
	  int         b_die;

    httpd_t     *httpd;

    /* ref count */
    int         i_ref;

    /* address/port and socket for listening at connections */
    char        *psz_hostname;
    int         i_port;
	//char        ip[64];
    int         *fd;

    vlc_mutex_t lock;

    /* all registered url (becarefull that 2 httpd_url_t could point at the same url)
     * This will slow down the url research but make my live easier
     * All url will have their cb trigger, but only the first one can answer
     * */
    int         i_url;
    httpd_url_t **url;

    int            i_client;
    httpd_client_t *client[16];    
};

struct httpd_url_t
{
    httpd_host_t *host;

    vlc_mutex_t lock;

    char      *psz_url;
    char      *psz_user;
    char      *psz_password;
    vlc_acl_t *p_acl; 

    struct
    {
        httpd_callback_t     cb;
        httpd_callback_sys_t *p_sys;
    } catch[HTTPD_MSG_MAX];
};

/* status */
enum
{
    HTTPD_CLIENT_RECEIVING,
    HTTPD_CLIENT_RECEIVE_DONE,

    HTTPD_CLIENT_SENDING,
    HTTPD_CLIENT_SEND_DONE,

    HTTPD_CLIENT_WAITING,

    HTTPD_CLIENT_DEAD,

    HTTPD_CLIENT_TLS_HS_IN,
    HTTPD_CLIENT_TLS_HS_OUT
};
/* mode */
enum
{
    HTTPD_CLIENT_FILE,      /* default */
    HTTPD_CLIENT_STREAM,    /* regulary get data from cb */
    HTTPD_CLIENT_BIDIR,     /* check for reading and get data from cb */
};

struct httpd_client_t
{
    httpd_url_t *url;

    int     i_ref;

    struct  sockaddr_storage sock;
    int     i_sock_size;
    int     fd;

    int     i_mode;
    int     i_state;
    int     b_read_waiting; /* stop as soon as possible sending */

    uint64_t i_activity_date;
    uint64_t i_activity_timeout;

    /* buffer for reading header */
    int     i_buffer_size;
    int     i_buffer;
    uint8_t *p_buffer;

    /* */
    httpd_message_t query;  /* client -> httpd */
    httpd_message_t answer; /* httpd -> client */    
};

httpd_host_t *httpd_TLSHostNew(  const char *psz_hostname,
                                int i_port,
                                const char *psz_cert, const char *psz_key,
                                const char *psz_ca, const char *psz_crl );
void httpd_HostDelete( httpd_host_t *host );

/* register a new url */
httpd_url_t *  httpd_UrlNew ( httpd_host_t *, const char *psz_url, const char *psz_user, const char *psz_password, const vlc_acl_t *p_acl  );
httpd_url_t *  httpd_UrlNewUnique ( httpd_host_t *, const char *psz_url, const char *psz_user, const char *psz_password, const vlc_acl_t *p_acl  );
/* register callback on a url */
int            httpd_UrlCatch ( httpd_url_t *, int i_msg, httpd_callback_t, httpd_callback_sys_t *  );
/* delete an url */
void           httpd_UrlDelete ( httpd_url_t *  );

/* Default client mode is FILE, use these to change it */
void           httpd_ClientModeStream ( httpd_client_t *cl  );
void           httpd_ClientModeBidir ( httpd_client_t *cl  );
char*          httpd_ClientIP ( httpd_client_t *cl, char *psz_ip  );
char*          httpd_ServerIP ( httpd_client_t *cl, char *psz_ip  );

/* High level */

httpd_file_t * httpd_FileNew ( httpd_host_t *, const char *psz_url, const char *psz_mime, const char *psz_user, const char *psz_password, const vlc_acl_t *p_acl, httpd_file_callback_t pf_fill, httpd_file_sys_t *  );
void           httpd_FileDelete ( httpd_file_t *  );


httpd_handler_t * httpd_HandlerNew ( httpd_host_t *, const char *psz_url, const char *psz_user, const char *psz_password, const vlc_acl_t *p_acl, httpd_handler_callback_t pf_fill, httpd_handler_sys_t *  );
void           httpd_HandlerDelete ( httpd_handler_t *  );


httpd_redirect_t * httpd_RedirectNew ( httpd_host_t *, const char *psz_url_dst, const char *psz_url_src  );
void               httpd_RedirectDelete ( httpd_redirect_t *  );


httpd_stream_t * httpd_StreamNew    ( httpd_host_t *, const char *psz_url, const char *psz_mime, const char *psz_user, const char *psz_password, const vlc_acl_t *p_acl  );
void             httpd_StreamDelete ( httpd_stream_t *  );
int              httpd_StreamHeader ( httpd_stream_t *, uint8_t *p_data, int i_data  );
int              httpd_StreamSend   ( httpd_stream_t *, uint8_t *p_data, int i_data  );


/* Msg functions facilities */
void         httpd_MsgInit ( httpd_message_t *   );
void         httpd_MsgAdd ( httpd_message_t *, char *psz_name, char *psz_value, ...  );
/* return "" if not found. The string is not allocated */
char *       httpd_MsgGet ( httpd_message_t *, char *psz_name  );
void         httpd_MsgClean ( httpd_message_t *  );

int vlc_mutex_unlock(vlc_mutex_t *p_mutex );
int vlc_mutex_lock(vlc_mutex_t * p_mutex );

#define TAB_APPEND( count, tab, p )             \
	if( (count) > 0 )                           \
{                                           \
	(tab) = realloc( tab, sizeof( void ** ) * ( (count) + 1 ) ); \
}                                           \
	else                                        \
{                                           \
	(tab) = malloc( sizeof( void ** ) );    \
}                                           \
	(tab)[count] = (p);        \
(count)++



#define TAB_FIND( count, tab, p, index )        \
{                                           \
	int _i_;                                \
	(index) = -1;                           \
	for( _i_ = 0; _i_ < (count); _i_++ )    \
{                                       \
	if( (tab)[_i_] == (p) )  \
{                                   \
	(index) = _i_;                  \
	break;                          \
}                                   \
}                                       \
}

#define TAB_REMOVE( count, tab, p )             \
{                                           \
	int _i_index_;                          \
	TAB_FIND( count, tab, p, _i_index_ );   \
	if( _i_index_ >= 0 )                    \
{                                       \
	if( (count) > 1 )                     \
{                                   \
	memmove( ((void**)(tab) + _i_index_),    \
	((void**)(tab) + _i_index_+1),  \
	( (count) - _i_index_ - 1 ) * sizeof( void* ) );\
}                                   \
	(count)--;                          \
	if( (count) == 0 )                  \
{                                   \
	free( tab );                    \
	(tab) = NULL;                   \
}                                   \
}                                       \
}

#endif /* _VLC_HTTPD_H */
