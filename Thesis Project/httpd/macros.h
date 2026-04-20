/*****************************************************************************
 * macros.h : Macros mapping for the HTTP interface
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

enum macroType
{
    MVLC_UNKNOWN = 0,
    MVLC_CONTROL,

    MVLC_INCLUDE,
    MVLC_FOREACH,
    MVLC_IF,
    MVLC_RPN,
    MVLC_STACK,
    MVLC_ELSE,
    MVLC_END,
    MVLC_GET,
    MVLC_SET,
    MVLC_INT,
    MVLC_FLOAT,
    MVLC_STRING,
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
    { "control",    MVLC_CONTROL },
    /* player control */

    { "rpn",        MVLC_RPN },
    { "stack",      MVLC_STACK },

    { "include",    MVLC_INCLUDE },
    { "foreach",    MVLC_FOREACH },
    { "value",      MVLC_VALUE },

    { "if",         MVLC_IF },
    { "else",       MVLC_ELSE },
    { "end",        MVLC_END },
    { "get",        MVLC_GET },
    { "set",        MVLC_SET },
    { "int",        MVLC_INT },
    { "float",      MVLC_FLOAT },
    { "string",     MVLC_STRING },
    /* end */
    { NULL,         MVLC_UNKNOWN }
};
