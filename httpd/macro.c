/*****************************************************************************
* macro.c : Custom <vlc> macro handling
*****************************************************************************
* Copyright (C) 2001-2005 the VideoLAN team
* $Id: http.c 12225 2005-08-18 10:01:30Z massiot $
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

#include "http.h"
#include "macros.h"

int E_(MacroParse)( macro_t *m, char *psz_src )
{
	char *dup = strdup( (char *)psz_src );
	char *src = dup;
	char *p;
	int     i_skip;

#define EXTRACT( name, l ) \
	src += l;    \
	p = strchr( src, '"' );             \
	if( p )                             \
	{                                   \
	*p++ = '\0';                    \
	}                                   \
	m->name = strdup( src );            \
	if( !p )                            \
	{                                   \
	break;                          \
	}                                   \
	src = p;

	/* init m */
	m->id = NULL;
	m->param1 = NULL;
	m->param2 = NULL;

	/* parse */
	src += 4;

	while( *src )
	{
		while( *src == ' ')
		{
			src++;
		}
		if( !strncasecmp( src, "id=\"", 4 ) )
		{
			EXTRACT( id, 4 );
		}
		else if( !strncasecmp( src, "param1=\"", 8 ) )
		{
			EXTRACT( param1, 8 );
		}
		else if( !strncasecmp( src, "param2=\"", 8 ) )
		{
			EXTRACT( param2, 8 );
		}
		else
		{
			break;
		}
	}
	if( strstr( src, "/>" ) )
	{
		src = strstr( src, "/>" ) + 2;
	}
	else
	{
		src += strlen( src );
	}

	if( m->id == NULL )
	{
		m->id = strdup( "" );
	}
	if( m->param1 == NULL )
	{
		m->param1 = strdup( "" );
	}
	if( m->param2 == NULL )
	{
		m->param2 = strdup( "" );
	}
	i_skip = src - dup;

	free( dup );
	return i_skip;
#undef EXTRACT
}

void E_(MacroClean)( macro_t *m )
{
	free( m->id );
	free( m->param1 );
	free( m->param2 );
}

int E_(StrToMacroType)( char *name )
{
	int i;

	if( !name || *name == '\0')
	{
		return MVLC_UNKNOWN;
	}
	for( i = 0; StrToMacroTypeTab[i].psz_name != NULL; i++ )
	{
		if( !strcasecmp( name, StrToMacroTypeTab[i].psz_name ) )
		{
			return StrToMacroTypeTab[i].i_type;
		}
	}
	return MVLC_UNKNOWN;
}

void sout_Parse(httpd_file_sys_t* p_args, char *psz_uri)
{
	//static char *Input_Type_text[6] = {"CAM"};
	//static char *Output_Type_text[8] = {"URL"};
	static char *video_filter_text[16] = {"无","MRTD"};
	char value[256];
	int  i, ivalue,bchange=0;

#define p_sys p_args->p_sys
	http_param_sys_t *param = &p_sys->params[p_sys->id];
	E_(ExtractURIValue)( psz_uri, "VideoFilter", value, 256 );
	E_(DecodeEncodedURI)( value );
	if( *value)
	{
		int i;
		ivalue = 0;
		for(i = 0;i < 2; i++)
		{
			if(!strcasecmp(value, video_filter_text[i]))
			{
				ivalue = i;
				break;
			}
		}

		if(param->i_filter_type != ivalue)
		{
			param->i_filter_type = ivalue;
			bchange = 1;
		}
	}

	E_(ExtractURIValue)( psz_uri, "HMax", value, 256 );
	E_(DecodeEncodedURI)( value );
	if( *value)
	{
		double fvalue = atoi(value);
		if(param->i_max != fvalue)
		{
			param->i_max = fvalue;
			bchange = 1;
		}

	}

	E_(ExtractURIValue)( psz_uri, "HMin", value, 256 );
	E_(DecodeEncodedURI)( value );
	if( *value)
	{
		double fvalue = atoi(value);
		if(param->i_min != fvalue)
		{
			param->i_min = fvalue;
			bchange = 1;
		}

	}

	E_(ExtractURIValue)( psz_uri, "FPS", value, 256 );
	E_(DecodeEncodedURI)( value );
	if( *value)
	{
		double fvalue = atof(value);
		if(param->f_fps != fvalue)
		{
			param->f_fps = fvalue;
			bchange = 1;
		}
	}

	ivalue = 0;
	E_(ExtractURIValue)( psz_uri, "objectDetect", value, 256 );
	E_(DecodeEncodedURI)( value );
	if( *value)
	{
		ivalue = 1;
	}
	if(param->b_object_detect != ivalue)
	{
		param->b_object_detect = ivalue;
		bchange = 1;
	}

	ivalue = 0;
	E_(ExtractURIValue)( psz_uri, "objectShow", value, 256 );
	E_(DecodeEncodedURI)( value );
	if( *value)
	{
		ivalue = 1;
	}
	if(param->b_object_show != ivalue)
	{
		param->b_object_show = ivalue;
		bchange = 1;
	}


	ivalue = 0;
	E_(ExtractURIValue)( psz_uri, "imageUnet", value, 256 );
	E_(DecodeEncodedURI)( value );
	if( *value)
	{
		ivalue = 1;
	}
	if(param->b_image_unet != ivalue)
	{
		param->b_image_unet = ivalue;
		bchange = 1;
	}

	param->param_change = bchange;
#undef p_sys
}

void E_(MacroDo)( httpd_file_sys_t *p_args,
				 macro_t *m,
				 char *p_request, int i_request,
				 char **pp_data,  int *pi_data,
				 char **pp_dst )
{
	intf_sys_t     *p_sys = p_args->p_sys;
	char control[128]="";

#define ALLOC( l ) \
	{               \
	int __i__ = *pp_dst - *pp_data; \
	*pi_data += (l);                  \
	*pp_data = realloc( *pp_data, *pi_data );   \
	*pp_dst = (*pp_data) + __i__;   \
	}
#define PRINT( str ) \
	ALLOC( strlen( str ) + 1 ); \
	*pp_dst += sprintf( *pp_dst, str );

#define PRINTS( str, s ) \
	ALLOC( strlen( str ) + strlen( s ) + 1 ); \
	{ \
	char * psz_cur = *pp_dst; \
	*pp_dst += sprintf( *pp_dst, str, s ); \
	while( psz_cur && *psz_cur ) \
	{  \
	/* Prevent script injection */ \
	if( *psz_cur == '<' ) *psz_cur = '*'; \
	if( *psz_cur == '>' ) *psz_cur = '*'; \
	psz_cur++ ; \
	} \
	}

	switch( E_(StrToMacroType)( m->id ) )
	{
	case MVLC_CONTROL:
		if( i_request <= 0 )
		{
			break;
		}
		
		E_(ExtractURIValue)( p_request, "open", control, 128 );
		if(*control)
		{
			if( i_request <= 0 ||
				*m->param1  == '\0' ||
				strstr( p_request, m->param1 ) == NULL )
			{
				break;
			}
			sout_Parse(p_args, p_request);
			break;
		}
		else
		{
			E_(ExtractURIValue)( p_request, "refresh", control, 128 );
			if(*control)
			{
				p_sys->params[0].status_request = 1;
				//EncoderId_Parse(p_args, p_request);
				break;
			}
			E_(ExtractURIValue)( p_request, "shutdown", control, 128 );
			if(*control)
			{
				p_sys->params[0].system_request = 1;
				//DVBSet_Parse(p_args, p_request);
				break;
			}
		}
		break;

	case MVLC_SET:
		{
			if( i_request <= 0 ||
				*m->param1  == '\0' ||
				strstr( p_request, m->param1 ) == NULL )
			{
				break;
			}

			break;
		}
	case MVLC_GET:
		{
			char value[512]="";

			if( *m->param1  == '\0' )
			{
				break;
			}
			PRINTS( "%s", value );
			break;
		}
	case MVLC_VALUE:
		{
			char *s, *v;

			if( m->param1 )
			{
				E_(EvaluateRPN)( p_sys,  p_args->vars, &p_args->stack, m->param1 );
				s = E_(SSPop)( &p_args->stack );
				v = E_(mvar_GetValue)( p_args->vars, s );
			}
			else
			{
				v = s = E_(SSPop)( &p_args->stack );
			}

			PRINTS( "%s", v );
			free( s );
			break;
		}
	case MVLC_RPN:
		E_(EvaluateRPN)( p_sys, p_args->vars, &p_args->stack, m->param1 );
		break;
		/* Useful to learn stack management */
	case MVLC_STACK:
		{
			break;
		}

	case MVLC_UNKNOWN:
	default:
		break;
	}
#undef PRINTS
#undef PRINT
#undef ALLOC
}

char *E_(MacroSearch)( char *src, char *end, int i_mvlc, vlc_bool_t b_after )
{
	int     i_id;
	int     i_level = 0;

	while( src < end )
	{
		if( src + 4 < end  && !strncasecmp( (char *)src, "<vlc", 4 ) )
		{
			int i_skip;
			macro_t m;

			i_skip = E_(MacroParse)( &m, src );

			i_id = E_(StrToMacroType)( m.id );

			switch( i_id )
			{
			case MVLC_IF:
			case MVLC_FOREACH:
				i_level++;
				break;
			case MVLC_END:
				i_level--;
				break;
			default:
				break;
			}

			E_(MacroClean)( &m );

			if( ( i_mvlc == MVLC_END && i_level == -1 ) ||
				( i_mvlc != MVLC_END && i_level == 0 && i_mvlc == i_id ) )
			{
				return src + ( b_after ? i_skip : 0 );
			}
			else if( i_level < 0 )
			{
				return NULL;
			}

			src += i_skip;
		}
		else
		{
			src++;
		}
	}

	return NULL;
}

void E_(Execute)( httpd_file_sys_t *p_args,
				 char *p_request, int i_request,
				 char **pp_data, int *pi_data,
				 char **pp_dst,
				 char *_src, char *_end )
{
	char *src, *dup, *end;
	char *dst = *pp_dst;

	src = dup = malloc( _end - _src + 1 );
	end = src +( _end - _src );

	memcpy( src, _src, _end - _src );
	*end = '\0';

	/* we parse searching <vlc */
	while(src && src < end )
	{
		char *p;
		int i_copy;

		p = (char *)strstr( (char *)src, "<vlc" );
		if( p < end && p == src )
		{
			macro_t m;

			src += E_(MacroParse)( &m, src );

			switch( E_(StrToMacroType)( m.id ) )
			{
			case MVLC_INCLUDE:
				{
					FILE *f;
					int  i_buffer;
					char *p_buffer;
					char psz_file[MAX_DIR_SIZE];
					char *p;
					char sep;

#if defined( WIN32 )
					sep = '\\';
#else
					sep = '/';
#endif

					if( m.param1[0] != sep )
					{
						strcpy( psz_file, p_args->file );
						p = strrchr( psz_file, sep );
						if( p != NULL )
							strcpy( p + 1, m.param1 );
						else
							strcpy( psz_file, m.param1 );
					}
					else
					{
						strcpy( psz_file, m.param1 );
					}

					if( ( f = fopen( psz_file, "r" ) ) == NULL )
					{
						break;
					}

					/* first we load in a temporary buffer */
					E_(FileLoad)( f, &p_buffer, &i_buffer );

					/* we parse executing all  <vlc /> macros */
					E_(Execute)( p_args, p_request, i_request, pp_data, pi_data,
						&dst, &p_buffer[0], &p_buffer[i_buffer] );
					free( p_buffer );
					fclose(f);
					break;
				}
			case MVLC_IF:
				{
					vlc_bool_t i_test;
					char    *endif;

					E_(EvaluateRPN)( p_args->p_sys, p_args->vars, &p_args->stack, m.param1 );
					if( E_(SSPopN)( &p_args->stack, p_args->vars ) )
					{
						i_test = 1;
					}
					else
					{
						i_test = 0;
					}
					endif = E_(MacroSearch)( src, end, MVLC_END, VLC_TRUE );

					if( i_test == 0 )
					{
						char *start = E_(MacroSearch)( src, endif, MVLC_ELSE, VLC_TRUE );

						if( start )
						{
							char *stop  = E_(MacroSearch)( start, endif, MVLC_END, VLC_FALSE );
							if( stop )
							{
								E_(Execute)( p_args, p_request, i_request,
									pp_data, pi_data, &dst, start, stop );
							}
						}
					}
					else if( i_test == 1 )
					{
						char *stop;
						if( ( stop = E_(MacroSearch)( src, endif, MVLC_ELSE, VLC_FALSE ) ) == NULL )
						{
							stop = E_(MacroSearch)( src, endif, MVLC_END, VLC_FALSE );
						}
						if( stop )
						{
							E_(Execute)( p_args, p_request, i_request,
								pp_data, pi_data, &dst, src, stop );
						}
					}

					src = endif;
					break;
				}
			case MVLC_FOREACH:
				{
					char *endfor = E_(MacroSearch)( src, end, MVLC_END, VLC_TRUE );
					char *start = src;
					char *stop = E_(MacroSearch)( src, end, MVLC_END, VLC_FALSE );

					if( stop )
					{
						mvar_t *index = NULL;
						int    i_idx;
						mvar_t *v;
						if( !strcasecmp( m.param2, "integer" ) )
						{
							char *arg = E_(SSPop)( &p_args->stack );
							index = E_(mvar_IntegerSetNew)( m.param1, arg );
							free( arg );
						}
                        else if( !strcmp( m.param2, "ProgramLists" ) )
                        {
							//index = E_(mvar_ProgramSetNew)( p_args->p_sys, m.param1 );
                        }
                        else if( !strcmp( m.param2, "EncoderLists" ) )
                        {
							//index = E_(mvar_EncodersSetNew)( p_args->p_sys, m.param1 );
                        }
                        else if( !strcmp( m.param2, "NetworkInterfaceLists" ) )
                        {
							//index = E_(mvar_NetworkSetNew)( p_args->p_sys, m.param1 );
                        }
                        else if( !strcmp( m.param2, "AudioTrackLists" ) )
                        {
							//index = E_(mvar_AudioTrackSetNew)( p_args->p_sys, m.param1 );
                        }
                        else if( !strcmp( m.param2, "DeviceLists" ) )
                        {
							//index = E_(mvar_DeviceSetNew)( p_args->p_sys, m.param1 );
                        }
						else if( ( v = E_(mvar_GetVar)( p_args->vars, m.param2 ) ) )
						{
							index = E_(mvar_Duplicate)( v );
						}
						else
						{
							src = endfor;
							break;
						}

						if(index)
						{
						for( i_idx = 0; i_idx < index->i_field; i_idx++ )
						{
							mvar_t *f = E_(mvar_Duplicate)( index->field[i_idx] );

							free( f->name );
							f->name = strdup( m.param1 );


							E_(mvar_PushVar)( p_args->vars, f );
							E_(Execute)( p_args, p_request, i_request,
								pp_data, pi_data, &dst, start, stop );
							E_(mvar_RemoveVar)( p_args->vars, f );

							E_(mvar_Delete)( f );
						}
						E_(mvar_Delete)( index );
						}

						src = endfor;
					}
					break;
				}
			default:
				E_(MacroDo)( p_args, &m, p_request, i_request,
					pp_data, pi_data, &dst );
				break;
			}

			E_(MacroClean)( &m );
			continue;
		}

		i_copy =   ( (p == NULL || p > end ) ? end : p  ) - src;
		if( i_copy > 0 )
		{
			int i_index = dst - *pp_data;

			*pi_data += i_copy;
			*pp_data = realloc( *pp_data, *pi_data );
			dst = (*pp_data) + i_index;

			memcpy( dst, src, i_copy );
			dst += i_copy;
			src += i_copy;
		}
	}

	*pp_dst = dst;
	free( dup );
}

