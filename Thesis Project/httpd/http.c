/*****************************************************************************
* http.c : HTTP/HTTPS Remote control interface
*****************************************************************************
* Copyright (C) 2001-2005 the VideoLAN team
* $Id: http.c 12449 2005-09-02 17:11:23Z massiot $
*
* Authors: Gildas Bazin <gbazin@netcourrier.com>
*          Laurent Aimar <fenrir@via.ecp.fr>
*          Christophe Massiot <massiot@via.ecp.fr>
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

#include <stdio.h>
#include <string.h>
#include "http.h"
#include "network.h"

/*****************************************************************************
* Activate: initialize and create stuff  HTTP 服务初始化
*****************************************************************************/
void *http_open(char *psz_dir, int i_port )
{
	intf_sys_t    *p_sys;
	char          *psz_cert = NULL, *psz_key = NULL, *psz_ca = NULL,
		*psz_crl = NULL;

	p_sys = malloc( sizeof( intf_sys_t ) );
	if( !p_sys )
	{
		return NULL;
	}

	memset(p_sys, 0 , sizeof( intf_sys_t ));

	p_sys->i_number = 1;
	strcpy(p_sys->psz_dir, psz_dir);  //将用户提供的目录路径 psz_dir 拷贝到 p_sys->psz_dir 中

	p_sys->psz_html_type = malloc( 32 );  //分配 HTML 类型字段
	if( p_sys->psz_html_type == NULL ) //如果分配失败，释放之前分配的 p_sys 内存，并返回 NULL
	{
		free( p_sys );
		return NULL ;
	}

	strcpy( p_sys->psz_html_type, "text/html; charset=UTF-8" );
	p_sys->iconv_from_utf8 = p_sys->iconv_to_utf8 = (vlc_iconv_t)-1;  //初始化字符集转换字段

	p_sys->p_httpd_host = httpd_TLSHostNew("", i_port, //调用 httpd_TLSHostNew 函数创建一个新的 HTTP 主机
		psz_cert, psz_key, psz_ca, psz_crl );

	if( p_sys->p_httpd_host == NULL )
	{
		free( p_sys->psz_html_type );
		free( p_sys );
		return NULL;
	} //如果 httpd_TLSHostNew 返回 NULL，表示主机创建失败。此时释放已分配的内存，并返回 NULL

	p_sys->i_files  = 0;

	E_(ParseDirectory)(p_sys);	 //调用 ParseDirectory 函数解析指定的目录（psz_dir），并将结果存储在 p_sys 中

	if( p_sys->i_files <= 0 )
	{
		httpd_HostDelete( p_sys->p_httpd_host );
		free( p_sys->psz_html_type ); 
		free( p_sys );
		return NULL;
	}  //检查解析后的文件数量（p_sys->i_files）。如果没有文件（<= 0），表示目录无效，此时删除主机、释放内存，并返回 NULL

	return p_sys;
}

/*****************************************************************************
* Close: destroy interface
*****************************************************************************/
void http_close (void *param)
{
	int i;
	intf_sys_t *p_sys = (intf_sys_t*)param;

	for( i = 0; i < p_sys->i_files; i++ )
	{
		if( p_sys->pp_files[i]->b_handler )
			httpd_HandlerDelete( ((httpd_handler_sys_t *)p_sys->pp_files[i])->p_handler );
		else
			httpd_FileDelete( p_sys->pp_files[i]->p_file );
		if( p_sys->pp_files[i]->p_redir )
			httpd_RedirectDelete( p_sys->pp_files[i]->p_redir );
		if( p_sys->pp_files[i]->p_redir2 )
			httpd_RedirectDelete( p_sys->pp_files[i]->p_redir2 );

		free( p_sys->pp_files[i]->file );
		free( p_sys->pp_files[i]->name );
		free( p_sys->pp_files[i] );
	}
	httpd_HostDelete( p_sys->p_httpd_host );
	free( p_sys->psz_html_type );		
	free( p_sys );
}

#if 0
int main(int argr, char**argv )
{
	intf_sys_t    *http_sys[4];
	int id = 0;

	//http_sys = http_open( id );

	http_close (http_sys);
	
	return VLC_SUCCESS;
}
#endif

/****************************************************************************
* HttpCallback:
****************************************************************************
* a file with b_html is parsed and all "macro" replaced
****************************************************************************/
static void Callback404( httpd_file_sys_t *p_args, char **pp_data,
						int *pi_data )
{
	char *p = *pp_data = malloc( 10240 );
	if( !p )
	{
		return;
	}
	p += sprintf( p, "<html>\n" );
	p += sprintf( p, "<head>\n" );
	p += sprintf( p, "<title>Error loading %s</title>\n", p_args->file );
	p += sprintf( p, "</head>\n" );
	p += sprintf( p, "<body>\n" );
	p += sprintf( p, "<h1><center>Error loading %s for %s</center></h1>\n", p_args->file, p_args->name );
	p += sprintf( p, "<hr />\n" );
	p += sprintf( p, "<a href=\"http://www.avsgm.com/\">GMT</a>\n" );
	p += sprintf( p, "</body>\n" );
	p += sprintf( p, "</html>\n" );

	*pi_data = strlen( *pp_data );
}

static void ParseExecute( httpd_file_sys_t *p_args, char *p_buffer,
						 int i_buffer, char *p_request,
						 char **pp_data, int *pi_data )
{
	int i, i_request = p_request != NULL ? strlen( p_request ) : 0;
	char *dst;
	char value[256];
	static char *Input_Type_text[6] = {"无", "CAM", "File"};
	static char *Output_Type_text[8] = {"URL"};
	static char *video_filter_text[16] = {"无","MRTD"};

	http_param_sys_t *param = &p_args->p_sys->params[p_args->p_sys->id];

	if(!param->status_change && i_request <= 38)//=38
	{
		int i_time = 0;
		param->status_request = 1;
		param->program_request= 1;
		param->param_request  = 1;
		param->status_change  = 1;
		while(param->status_change == 1 && i_time <= 20)
		{
			i_time++;
			usleep(100000);
		}
	}
	else
		param->status_request = 1;

	p_args->vars = E_(mvar_New)( "variables", "" );
	E_(mvar_AppendNewVar)( p_args->vars, "url_param", i_request > 0 ? "1" : "0" );
	E_(mvar_AppendNewVar)( p_args->vars, "url_value", p_request );
	E_(mvar_AppendNewVar)( p_args->vars, "copyright", COPYRIGHT_MESSAGE );

	sprintf( value, "%d", p_args->p_sys->id+1);
	E_(mvar_AppendNewVar)( p_args->vars, "EncoderID", value);

	E_(mvar_AppendNewVar)( p_args->vars, "stream_state", param->stream_state);

	sprintf( value, "%s" , Input_Type_text[param->i_input_type]);
	E_(mvar_AppendNewVar)( p_args->vars, "InputType", value );

	sprintf( value, "%s" , Output_Type_text[0]);
	E_(mvar_AppendNewVar)( p_args->vars, "OutputType", value );

	E_(mvar_AppendNewVar)( p_args->vars, "OutputAddr", param->psz_url );
	
	E_(mvar_AppendNewVar)( p_args->vars, "VideoFilter", video_filter_text[param->i_filter_type]);

	sprintf(value, "%d", param->i_max);
	E_(mvar_AppendNewVar)( p_args->vars, "HMax", value );

	sprintf(value, "%d", param->i_min);
	E_(mvar_AppendNewVar)( p_args->vars, "HMin", value );

	sprintf(value, "%.2f", param->f_fps);
	E_(mvar_AppendNewVar)( p_args->vars, "FPS", value );

	sprintf(value, "%d", param->b_object_detect);
	E_(mvar_AppendNewVar)( p_args->vars, "objectDetect", value );

	sprintf(value, "%d", param->b_object_show);
	E_(mvar_AppendNewVar)( p_args->vars, "objectShow", value );

	sprintf(value, "%d", param->b_image_unet);
	E_(mvar_AppendNewVar)( p_args->vars, "imageUnet", value );

	E_(SSInit)( &p_args->stack );

	/* allocate output */
	*pi_data = i_buffer + 1000;
	dst = *pp_data = malloc( *pi_data );

	/* we parse executing all  <vlc /> macros */
	E_(Execute)( p_args, p_request, i_request, pp_data, pi_data, &dst,
		&p_buffer[0], &p_buffer[i_buffer] );

	*dst     = '\0';
	*pi_data = dst - *pp_data;

	E_(SSClean)( &p_args->stack );
	E_(mvar_Delete)( p_args->vars );
}

int  E_(HttpCallback)( httpd_file_sys_t *p_args,
					  httpd_file_t *p_file,
					  uint8_t *_p_request,
					  uint8_t **_pp_data, int *pi_data )
{
	char *p_request = (char *)_p_request;
	char **pp_data = (char **)_pp_data;
	FILE *f;

	if( ( f = fopen( p_args->file, "r" ) ) == NULL )
	{
		Callback404( p_args, pp_data, pi_data );
		return VLC_SUCCESS;
	}

	if( !p_args->b_html )
	{
		E_(FileLoad)( f, pp_data, pi_data );
	}
	else
	{
		int  i_buffer;
		char *p_buffer;

		/* first we load in a temporary buffer */
		E_(FileLoad)( f, &p_buffer, &i_buffer );

		ParseExecute( p_args, p_buffer, i_buffer, p_request, pp_data, pi_data );

		free( p_buffer );
	}

	fclose( f );

	return VLC_SUCCESS;
}

