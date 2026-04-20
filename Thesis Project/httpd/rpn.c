/*****************************************************************************
 * rpn.c : RPN evaluator for the HTTP Interface
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
#include "network.h"

static char *vlc_UrlEncode( const char *psz_url )
{
	char *psz_enc, *out;
	const unsigned char *in;
	
	psz_enc = (char *)malloc( 3 * strlen( psz_url ) + 1 );
	if( psz_enc == NULL )
		return NULL;
	
	out = psz_enc;
	for( in = (const unsigned char *)psz_url; *in; in++ )
	{
		unsigned char c = *in;
		
		if( isurlsafe( c ) )
			*out++ = (char)c;
		else
		{
			*out++ = '%';
			*out++ = ( ( c >> 4 ) >= 0xA ) ? 'A' + ( c >> 4 ) - 0xA
				: '0' + ( c >> 4 );
			*out++ = ( ( c & 0xf ) >= 0xA ) ? 'A' + ( c & 0xf ) - 0xA
				: '0' + ( c & 0xf );
		}
	}
	*out++ = '\0';
	
	return (char *)realloc( psz_enc, out - psz_enc );
}


void E_(SSInit)( rpn_stack_t *st )
{
    st->i_stack = 0;
}

void E_(SSClean)( rpn_stack_t *st )
{
    while( st->i_stack > 0 )
    {
        free( st->stack[--st->i_stack] );
    }
}

void E_(SSPush)( rpn_stack_t *st, const char *s )
{
    if( st->i_stack < STACK_MAX )
    {
        st->stack[st->i_stack++] = strdup( s );
    }
}

char *E_(SSPop)( rpn_stack_t *st )
{
    if( st->i_stack <= 0 )
    {
        return strdup( "" );
    }
    else
    {
        return st->stack[--st->i_stack];
    }
}

int E_(SSPopN)( rpn_stack_t *st, mvar_t  *vars )
{
    char *name;
    char *value;

    char *end;
    int  i;

    name = E_(SSPop)( st );
    i = strtol( name, &end, 0 );
    if( end == name )
    {
        value = E_(mvar_GetValue)( vars, name );
        i = atoi( value );
    }
    free( name );

    return( i );
}

void E_(SSPushN)( rpn_stack_t *st, int i )
{
    char v[512];

    sprintf( v, "%d", i );
    E_(SSPush)( st, v );
}

void E_(EvaluateRPN)( intf_sys_t *p_sys, mvar_t  *vars,
                      rpn_stack_t *st, char *exp )
{
    while( exp != NULL && *exp != '\0' )
    {
        char *p, *s;

        /* skip space */
        while( *exp == ' ' )
        {
            exp++;
        }

        if( *exp == '\'' )
        {
            /* extract string */
            p = E_(FirstWord)( exp, exp );
            E_(SSPush)( st, exp );
            exp = p;
            continue;
        }

        /* extract token */
        p = E_(FirstWord)( exp, exp );
        s = exp;
        if( p == NULL )
        {
            exp += strlen( exp );
        }
        else
        {
            exp = p;
        }

        if( *s == '\0' )
        {
            break;
        }

        /* 1. Integer function */
        if( !strcmp( s, "!" ) )
        {
            E_(SSPushN)( st, ~E_(SSPopN)( st, vars ) );
        }
        else if( !strcmp( s, "^" ) )
        {
            E_(SSPushN)( st, E_(SSPopN)( st, vars ) ^ E_(SSPopN)( st, vars ) );
        }
        else if( !strcmp( s, "&" ) )
        {
            E_(SSPushN)( st, E_(SSPopN)( st, vars ) & E_(SSPopN)( st, vars ) );
        }
        else if( !strcmp( s, "|" ) )
        {
            E_(SSPushN)( st, E_(SSPopN)( st, vars ) | E_(SSPopN)( st, vars ) );
        }
        else if( !strcmp( s, "+" ) )
        {
            E_(SSPushN)( st, E_(SSPopN)( st, vars ) + E_(SSPopN)( st, vars ) );
        }
        else if( !strcmp( s, "-" ) )
        {
            int j = E_(SSPopN)( st, vars );
            int i = E_(SSPopN)( st, vars );
            E_(SSPushN)( st, i - j );
        }
        else if( !strcmp( s, "*" ) )
        {
            E_(SSPushN)( st, E_(SSPopN)( st, vars ) * E_(SSPopN)( st, vars ) );
        }
        else if( !strcmp( s, "/" ) )
        {
            int i, j;

            j = E_(SSPopN)( st, vars );
            i = E_(SSPopN)( st, vars );

            E_(SSPushN)( st, j != 0 ? i / j : 0 );
        }
        else if( !strcmp( s, "%" ) )
        {
            int i, j;

            j = E_(SSPopN)( st, vars );
            i = E_(SSPopN)( st, vars );

            E_(SSPushN)( st, j != 0 ? i % j : 0 );
        }
        /* 2. integer tests */
        else if( !strcmp( s, "=" ) )
        {
            E_(SSPushN)( st, E_(SSPopN)( st, vars ) == E_(SSPopN)( st, vars ) ? -1 : 0 );
        }
        else if( !strcmp( s, "!=" ) )
        {
            E_(SSPushN)( st, E_(SSPopN)( st, vars ) != E_(SSPopN)( st, vars ) ? -1 : 0 );
        }
        else if( !strcmp( s, "<" ) )
        {
            int j = E_(SSPopN)( st, vars );
            int i = E_(SSPopN)( st, vars );

            E_(SSPushN)( st, i < j ? -1 : 0 );
        }
        else if( !strcmp( s, ">" ) )
        {
            int j = E_(SSPopN)( st, vars );
            int i = E_(SSPopN)( st, vars );

            E_(SSPushN)( st, i > j ? -1 : 0 );
        }
        else if( !strcmp( s, "<=" ) )
        {
            int j = E_(SSPopN)( st, vars );
            int i = E_(SSPopN)( st, vars );

            E_(SSPushN)( st, i <= j ? -1 : 0 );
        }
        else if( !strcmp( s, ">=" ) )
        {
            int j = E_(SSPopN)( st, vars );
            int i = E_(SSPopN)( st, vars );

            E_(SSPushN)( st, i >= j ? -1 : 0 );
        }
        /* 3. string functions */
        else if( !strcmp( s, "strcat" ) )
        {
            char *s2 = E_(SSPop)( st );
            char *s1 = E_(SSPop)( st );
            char *str = malloc( strlen( s1 ) + strlen( s2 ) + 1 );

            strcpy( str, s1 );
            strcat( str, s2 );

            E_(SSPush)( st, str );
            free( s1 );
            free( s2 );
            free( str );
        }
        else if( !strcmp( s, "strcmp" ) )
        {
            char *s2 = E_(SSPop)( st );
            char *s1 = E_(SSPop)( st );

            E_(SSPushN)( st, strcmp( s1, s2 ) );
            free( s1 );
            free( s2 );
        }
        else if( !strcmp( s, "strncmp" ) )
        {
            int n = E_(SSPopN)( st, vars );
            char *s2 = E_(SSPop)( st );
            char *s1 = E_(SSPop)( st );

            E_(SSPushN)( st, strncmp( s1, s2 , n ) );
            free( s1 );
            free( s2 );
        }
        else if( !strcmp( s, "strequ" ) )
        {
					char *s2 = E_(SSPop)( st );
					char *s1 = E_(SSPop)( st );
					
					E_(SSPushN)( st, !strcmp( s1, s2 ) );
					free( s1 );
					free( s2 );
        }				
        else if( !strcmp( s, "strsub" ) )
        {
            int n = E_(SSPopN)( st, vars );
            int m = E_(SSPopN)( st, vars );
            int i_len;
            char *s = E_(SSPop)( st );
            char *str;

            if( n >= m )
            {
                i_len = n - m + 1;
            }
            else
            {
                i_len = 0;
            }

            str = malloc( i_len + 1 );

            memcpy( str, s + m - 1, i_len );
            str[ i_len ] = '\0';

            E_(SSPush)( st, str );
            free( s );
            free( str );
        }
        else if( !strcmp( s, "strlen" ) )
        {
            char *str = E_(SSPop)( st );

            E_(SSPushN)( st, strlen( str ) );
            free( str );
        }
        else if( !strcmp( s, "str_replace" ) )
        {
            char *psz_to = E_(SSPop)( st );
            char *psz_from = E_(SSPop)( st );
            char *psz_in = E_(SSPop)( st );
            char *psz_in_current = psz_in;
            char *psz_out = malloc( strlen(psz_in) * strlen(psz_to) + 1 );
            char *psz_out_current = psz_out;

            while( (p = strstr( psz_in_current, psz_from )) != NULL )
            {
                memcpy( psz_out_current, psz_in_current, p - psz_in_current );
                psz_out_current += p - psz_in_current;
                strcpy( psz_out_current, psz_to );
                psz_out_current += strlen(psz_to);
                psz_in_current = p + strlen(psz_from);
            }
            strcpy( psz_out_current, psz_in_current );
            psz_out_current += strlen(psz_in_current);
            *psz_out_current = '\0';

            E_(SSPush)( st, psz_out );
            free( psz_to );
            free( psz_from );
            free( psz_in );
            free( psz_out );
        }
        else if( !strcmp( s, "url_extract" ) )
        {
            char *url = E_(mvar_GetValue)( vars, "url_value" );
            char *name = E_(SSPop)( st );
            char value[512];

            E_(ExtractURIValue)( url, name, value, 512 );
            E_(DecodeEncodedURI)( value );
            E_(SSPush)( st, value );
            free( name );
        }
        else if( !strcmp( s, "url_encode" ) )
        {
            char *url = E_(SSPop)( st );
            char *value;

            value = vlc_UrlEncode( url );
            free( url );
            E_(SSPush)( st, value );
            free( value );
        }
        else if( !strcmp( s, "addslashes" ) )
        {
            char *psz_src = E_(SSPop)( st );
            char *psz_dest;
            char *str = psz_src;

            p = psz_dest = malloc( strlen( str ) * 2 + 1 );

            while( *str != '\0' )
            {
                if( *str == '"' || *str == '\'' || *str == '\\' )
                {
                    *p++ = '\\';
                }
                *p++ = *str;
                str++;
            }
            *p = '\0';

            E_(SSPush)( st, psz_dest );
            free( psz_src );
            free( psz_dest );
        }
        else if( !strcmp( s, "stripslashes" ) )
        {
            char *psz_src = E_(SSPop)( st );
            char *psz_dest;

            p = psz_dest = strdup( psz_src );

            while( *psz_src )
            {
                if( *psz_src == '\\' && *(psz_src + 1) )
                {
                    psz_src++;
                }
                *p++ = *psz_src++;
            }
            *p = '\0';

            E_(SSPush)( st, psz_dest );
            free( psz_src );
            free( psz_dest );
        }
        else if( !strcmp( s, "htmlspecialchars" ) )
        {
            char *psz_src = E_(SSPop)( st );
            char *psz_dest;
            char *str = psz_src;

            p = psz_dest = malloc( strlen( str ) * 6 + 1 );

            while( *str != '\0' )
            {
                if( *str == '&' )
                {
                    strcpy( p, "&amp;" );
                    p += 5;
                }
                else if( *str == '\"' )
                {
                    strcpy( p, "&quot;" );
                    p += 6;
                }
                else if( *str == '\'' )
                {
                    strcpy( p, "&#039;" );
                    p += 6;
                }
                else if( *str == '<' )
                {
                    strcpy( p, "&lt;" );
                    p += 4;
                }
                else if( *str == '>' )
                {
                    strcpy( p, "&gt;" );
                    p += 4;
                }
                else
                {
                    *p++ = *str;
                }
                str++;
            }
            *p = '\0';

            E_(SSPush)( st, psz_dest );
            free( psz_src );
            free( psz_dest );
        }        
        /* 4. stack functions */
        else if( !strcmp( s, "dup" ) )
        {
            char *str = E_(SSPop)( st );
            E_(SSPush)( st, str );
            E_(SSPush)( st, str );
            free( str );
        }
        else if( !strcmp( s, "drop" ) )
        {
            char *str = E_(SSPop)( st );
            free( str );
        }
        else if( !strcmp( s, "swap" ) )
        {
            char *s1 = E_(SSPop)( st );
            char *s2 = E_(SSPop)( st );

            E_(SSPush)( st, s1 );
            E_(SSPush)( st, s2 );
            free( s1 );
            free( s2 );
        }
        else if( !strcmp( s, "flush" ) )
        {
            E_(SSClean)( st );
            E_(SSInit)( st );
        }
        else if( !strcmp( s, "store" ) )
        {
            char *value = E_(SSPop)( st );
            char *name  = E_(SSPop)( st );

            E_(mvar_PushNewVar)( vars, name, value );
            free( name );
            free( value );
        }
        else if( !strcmp( s, "value" ) )
        {
            char *name  = E_(SSPop)( st );
            char *value = E_(mvar_GetValue)( vars, name );

            E_(SSPush)( st, value );

            free( name );
        }        
        else
        {
            E_(SSPush)( st, s );
        }
    }
}
