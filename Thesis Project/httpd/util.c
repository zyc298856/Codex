/*****************************************************************************
 * util.c : Utility functions for HTTP interface
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

/****************************************************************************
 * File and directory functions
 ****************************************************************************/

/* ToUrl: create a good name for an url from filename */
char *E_(FileToUrl)( char *name, vlc_bool_t *pb_index )
{
    char *url, *p;

    url = p = malloc( strlen( name ) + 1 );

    *pb_index = VLC_FALSE;
    if( !url || !p )
    {
        return NULL;
    }

#ifdef WIN32
    while( *name == '\\' || *name == '/' )
#else
    while( *name == '/' )
#endif
    {
        name++;
    }

    *p++ = '/';
    strcpy( p, name );

#ifdef WIN32
    /* convert '\\' into '/' */
    name = p;
    while( *name )
    {
        if( *name == '\\' )
            *name = '/';
        name++;
    }
#endif

    /* index.* -> / */
    if( ( p = strrchr( url, '/' ) ) != NULL )
    {
        if( !strncmp( p, "/index.", 7 ) )
        {
            p[1] = '\0';
            *pb_index = VLC_TRUE;
        }
    }
    return url;
}

/* Load a file */
int E_(FileLoad)( FILE *f, char **pp_data, int *pi_data )
{
    int i_read;

    /* just load the file */
    *pi_data = 0;
    *pp_data = malloc( 1025 );  /* +1 for \0 */
    while( ( i_read = fread( &(*pp_data)[*pi_data], 1, 1024, f ) ) == 1024 )
    {
        *pi_data += 1024;
        *pp_data = realloc( *pp_data, *pi_data  + 1025 );
    }
    if( i_read > 0 )
    {
        *pi_data += i_read;
    }
    (*pp_data)[*pi_data] = '\0';

    return VLC_SUCCESS;
}

int vasprintf(char **strp, const char *fmt, va_list ap)
{
	/* Guess we need no more than 100 bytes. */
	int     i_size = 100;
	char    *p = malloc( i_size );
	int     n;
	
	if( p == NULL )
	{
		*strp = NULL;
		return -1;
	}
	
	for( ;; )
	{
		/* Try to print in the allocated space. */
		n = vsnprintf( p, i_size, fmt, ap );
		
		/* If that worked, return the string. */
		if (n > -1 && n < i_size)
		{
			*strp = p;
			return strlen( p );
		}
		/* Else try again with more space. */
		if (n > -1)    /* glibc 2.1 */
		{
			i_size = n+1; /* precisely what is needed */
		}
		else           /* glibc 2.0 */
		{
			i_size *= 2;  /* twice the old size */
		}
		if( (p = realloc( p, i_size ) ) == NULL)
		{
			*strp = NULL;
			return -1;
		}
	}
}

int asprintf( char **strp, const char *fmt, ... )
{
	va_list args;
	int i_ret;
	
	va_start( args, fmt );
	i_ret = vasprintf( strp, fmt, args );
	va_end( args );
	
	return i_ret;
}

/* Parse a directory and recursively add files */
int E_(ParseDirectory)( intf_sys_t     *p_sys)
{
    char           dir[MAX_DIR_SIZE];
	char psz_dir[256]= "./admin" ;
    vlc_acl_t    *p_acl = NULL;
    char          user[256] = "admin";
    char          password[256] = "admin";
	char          *files[4] = {"index.html", "log.html", "readme.html", "style.css"};
    int            i;
    char sep;
    FILE          *file;

	strcpy(psz_dir, p_sys->psz_dir);
	strcat(psz_dir, "http");

#if defined( WIN32 )
    sep = '\\';
#else
    sep = '/';
#endif
    sprintf( dir, "%s%c.access", psz_dir, sep );
	if( ( file = fopen( dir, "r" ) ) != NULL )
	{
		char line[1024];
		int  i_size;

		i_size = fread( line, 1, 1023, file );
		if( i_size > 0 )
		{
			char *p;
			while( i_size > 0 && ( line[i_size-1] == '\n' ||
				   line[i_size-1] == '\r' ) )
			{
				i_size--;
			}

			line[i_size] = '\0';

			p = strchr( line, ':' );
			if( p )
			{
				*p++ = '\0';
				strcpy(user, line );
				strcpy(password, p );
			}
		}

		fclose( file );
	}

	for( i = 0; i < 4; i++)
	{
		httpd_file_sys_t *f = NULL;
		vlc_bool_t b_index;
		char *psz_tmp, *psz_file, *psz_name;
		sprintf( dir, "%s%c%s", psz_dir, sep, files[i] );

		psz_file = strdup( dir );

		psz_tmp = strdup( &dir[strlen( psz_dir )] );//psz_root
		psz_name = E_(FileToUrl)( psz_tmp, &b_index );
		free( psz_tmp );            
		if( f == NULL )
		{
			f = malloc( sizeof( httpd_file_sys_t ) );
			f->b_handler = VLC_FALSE;
		}

		f->p_sys  = p_sys;
		f->p_file = NULL;
		f->p_redir = NULL;
		f->p_redir2 = NULL;
		f->file = psz_file;
		f->name = psz_name;
		f->b_html = strstr( &dir[strlen( psz_dir )], ".htm" ) ? VLC_TRUE : VLC_FALSE;//psz_root

		if( !f->name )
		{
			free( f );
			return( VLC_ENOMEM );
		}

		if( !f->b_handler )
		{
			f->p_file = httpd_FileNew( p_sys->p_httpd_host,
				f->name,
				f->b_html ? p_sys->psz_html_type :
				NULL,
				user, password, p_acl,
				E_(HttpCallback), f );
			if( f->p_file != NULL )
			{
				p_sys->pp_files[p_sys->i_files++] = f;
			}
		}           

		/* for url that ends by / add
		*  - a redirect from rep to rep/
		*  - in case of index.* rep/index.html to rep/ */
		if( f && f->name[strlen(f->name) - 1] == '/' )
		{
			char *psz_redir = strdup( f->name );
			char *p;
			psz_redir[strlen( psz_redir ) - 1] = '\0';

			f->p_redir = httpd_RedirectNew( p_sys->p_httpd_host, f->name, psz_redir );
			free( psz_redir );

			if( b_index && ( p = strstr( f->file, "index." ) ) )
			{
				asprintf( &psz_redir, "%s%s", f->name, p );

				f->p_redir2 = httpd_RedirectNew( p_sys->p_httpd_host,
					f->name, psz_redir );

				free( psz_redir );
			}
		}

	}
    return VLC_SUCCESS;
}


/****************************************************************************
 * URI Parsing functions
 ****************************************************************************/
int E_(TestURIParam)( char *psz_uri, const char *psz_name )
{
    char *p = psz_uri;

    while( (p = strstr( p, psz_name )) )
    {
        /* Verify that we are dealing with a post/get argument */
        if( (p == psz_uri || *(p - 1) == '&' || *(p - 1) == '\n')
              && p[strlen(psz_name)] == '=' )
        {
            return VLC_TRUE;
        }
        p++;
    }

    return VLC_FALSE;
}
char *E_(ExtractURIValue)( char *psz_uri, const char *psz_name,
                             char *psz_value, int i_value_max )
{
    char *p = psz_uri;

    while( (p = strstr( p, psz_name )) )
    {
        /* Verify that we are dealing with a post/get argument */
        if( (p == psz_uri || *(p - 1) == '&' || *(p - 1) == '\n')
              && p[strlen(psz_name)] == '=' )
            break;
        p++;
    }

    if( p )
    {
        int i_len;

        p += strlen( psz_name );
        if( *p == '=' ) p++;

        if( strchr( p, '&' ) )
        {
            i_len = (int)(strchr( p, '&' ) - p);
        }
        else
        {
            /* for POST method */
            if( strchr( p, '\n' ) )
            {
                i_len = (int)(strchr( p, '\n' ) - p);
                if( i_len && *(p+i_len-1) == '\r' ) i_len--;
            }
            else
            {
                i_len = strlen( p );
            }
        }
        i_len = MIN( i_value_max - 1, i_len );
        if( i_len > 0 )
        {
            strncpy( psz_value, p, i_len );
            psz_value[i_len] = '\0';
        }
        else
        {
            strncpy( psz_value, "", i_value_max );
        }
        p += i_len;
    }
    else
    {
        strncpy( psz_value, "", i_value_max );
    }

    return p;
}

void E_(DecodeEncodedURI)( char *psz )
{
    char *dup = strdup( psz );
    char *p = dup;

    while( *p )
    {
        if( *p == '%' )
        {
            char val[3];
            p++;
            if( !*p )
            {
                break;
            }

            val[0] = *p++;
            val[1] = *p++;
            val[2] = '\0';

            *psz++ = strtol( val, NULL, 16 );
        }
        else if( *p == '+' )
        {
            *psz++ = ' ';
            p++;
        }
        else
        {
            *psz++ = *p++;
        }
    }
    *psz++ = '\0';
    free( dup );
}

/* Since the resulting string is smaller we can work in place, so it is
 * permitted to have psz == new. new points to the first word of the
 * string, the function returns the remaining string. */
char *E_(FirstWord)( char *psz, char *new )
{
    vlc_bool_t b_end;

    while( *psz == ' ' )
        psz++;

    while( *psz != '\0' && *psz != ' ' )
    {
        if( *psz == '\'' )
        {
            char c = *psz++;
            while( *psz != '\0' && *psz != c )
            {
                if( *psz == '\\' && psz[1] != '\0' )
                    psz++;
                *new++ = *psz++;
            }
            if( *psz == c )
                psz++;
        }
        else
        {
            if( *psz == '\\' && psz[1] != '\0' )
                psz++;
            *new++ = *psz++;
        }
    }
    b_end = !*psz;

    *new++ = '\0';
    if( !b_end )
        return psz + 1;
    else
        return NULL;
}
