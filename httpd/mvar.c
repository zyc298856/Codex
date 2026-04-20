/*****************************************************************************
 * mvar.c : Variables handling for the HTTP Interface
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

mvar_t *E_(mvar_New)( const char *name, const char *value )
{
    mvar_t *v = malloc( sizeof( mvar_t ) );

    if( !v ) return NULL;
    v->name = strdup( name );
    v->value = strdup( value ? value : "" );

    v->i_field = 0;
    v->field = malloc( sizeof( mvar_t * ) );
    v->field[0] = NULL;

    return v;
}

void E_(mvar_Delete)( mvar_t *v )
{
    int i;

    free( v->name );
    free( v->value );

    for( i = 0; i < v->i_field; i++ )
    {
        E_(mvar_Delete)( v->field[i] );
    }
    free( v->field );
    free( v );
}

void E_(mvar_AppendVar)( mvar_t *v, mvar_t *f )
{
    v->field = realloc( v->field, sizeof( mvar_t * ) * ( v->i_field + 2 ) );
    v->field[v->i_field] = f;
    v->i_field++;
}

mvar_t *E_(mvar_Duplicate)( const mvar_t *v )
{
    int i;
    mvar_t *n;

    n = E_(mvar_New)( v->name, v->value );
    for( i = 0; i < v->i_field; i++ )
    {
        E_(mvar_AppendVar)( n, E_(mvar_Duplicate)( v->field[i] ) );
    }

    return n;
}

void E_(mvar_PushVar)( mvar_t *v, mvar_t *f )
{
    v->field = realloc( v->field, sizeof( mvar_t * ) * ( v->i_field + 2 ) );
    if( v->i_field > 0 )
    {
        memmove( &v->field[1], &v->field[0], sizeof( mvar_t * ) * v->i_field );
    }
    v->field[0] = f;
    v->i_field++;
}

void E_(mvar_RemoveVar)( mvar_t *v, mvar_t *f )
{
    int i;
    for( i = 0; i < v->i_field; i++ )
    {
        if( v->field[i] == f )
        {
            break;
        }
    }
    if( i >= v->i_field )
    {
        return;
    }

    if( i + 1 < v->i_field )
    {
        memmove( &v->field[i], &v->field[i+1], sizeof( mvar_t * ) * ( v->i_field - i - 1 ) );
    }
    v->i_field--;
    /* FIXME should do a realloc */
}

mvar_t *E_(mvar_GetVar)( mvar_t *s, const char *name )
{
    int i;
    char base[512], *field, *p;
    int  i_index;

    /* format: name[index].field */

    field = strchr( name, '.' );
    if( field )
    {
        int i = field - name;
        strncpy( base, name, i );
        base[i] = '\0';
        field++;
    }
    else
    {
        strcpy( base, name );
    }

    if( ( p = strchr( base, '[' ) ) )
    {
        *p++ = '\0';
        sscanf( p, "%d]", &i_index );
        if( i_index < 0 )
        {
            return NULL;
        }
    }
    else
    {
        i_index = 0;
    }

    for( i = 0; i < s->i_field; i++ )
    {
        if( !strcmp( s->field[i]->name, base ) )
        {
            if( i_index > 0 )
            {
                i_index--;
            }
            else
            {
                if( field )
                {
                    return E_(mvar_GetVar)( s->field[i], field );
                }
                else
                {
                    return s->field[i];
                }
            }
        }
    }
    return NULL;
}

char *E_(mvar_GetValue)( mvar_t *v, char *field )
{
    if( *field == '\0' )
    {
        return v->value;
    }
    else
    {
        mvar_t *f = E_(mvar_GetVar)( v, field );
        if( f )
        {
            return f->value;
        }
        else
        {
            return field;
        }
    }
}

void E_(mvar_PushNewVar)( mvar_t *vars, const char *name,
                          const char *value )
{
    mvar_t *f = E_(mvar_New)( name, value );
    E_(mvar_PushVar)( vars, f );
}

void E_(mvar_AppendNewVar)( mvar_t *vars, const char *name,
                            const char *value )
{
    mvar_t *f = E_(mvar_New)( name, value );
    E_(mvar_AppendVar)( vars, f );
}


/* arg= start[:stop[:step]],.. */
mvar_t *E_(mvar_IntegerSetNew)( const char *name, const char *arg )
{
    char *dup = strdup( arg );
    char *str = dup;
    mvar_t *s = E_(mvar_New)( name, "set" );

    while( str )
    {
        char *p;
        int  i_start,i_stop,i_step;
        int  i_match;

        p = strchr( str, ',' );
        if( p )
        {
            *p++ = '\0';
        }

        i_step = 0;
        i_match = sscanf( str, "%d:%d:%d", &i_start, &i_stop, &i_step );

        if( i_match == 1 )
        {
            i_stop = i_start;
            i_step = 1;
        }
        else if( i_match == 2 )
        {
            i_step = i_start < i_stop ? 1 : -1;
        }

        if( i_match >= 1 )
        {
            int i;

            if( ( i_start <= i_stop && i_step > 0 ) ||
                ( i_start >= i_stop && i_step < 0 ) )
            {
                for( i = i_start; ; i += i_step )
                {
                    char   value[79];

                    if( ( i_step > 0 && i > i_stop ) ||
                        ( i_step < 0 && i < i_stop ) )
                    {
                        break;
                    }

                    sprintf( value, "%d", i );

                    E_(mvar_PushNewVar)( s, name, value );
                }
            }
        }
        str = p;
    }

    free( dup );
    return s;
}

