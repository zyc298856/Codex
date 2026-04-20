/*****************************************************************************
* http.h: Headers for the HTTP interface
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

#ifndef _HTTP_H_
#define _HTTP_H_

/*****************************************************************************
* Preamble
*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#if defined( WIN32 ) && !defined( UNDER_CE )
#include <Winsock2.h>
#include <Iphlpapi.h>
#endif

#include "vlc_httpd.h"
#include "vlc_acl.h"

#ifdef HAVE_SYS_STAT_H
#   include <sys/stat.h>
#endif

#   include <fcntl.h>
#   include <unistd.h>
#   include <stdarg.h>

#ifdef HAVE_DIRENT_H
#   include <dirent.h>
#endif

/* stat() support for large files on win32 */
#if defined( WIN32 ) && !defined( UNDER_CE )
#   define stat _stati64
#endif


#if defined( WIN32 ) && !defined( UNDER_CE )
#define WIN32_LEAN_AND_MEAN
#include <windows.h> /* for GetFileAttributes */
#include <vdmdbg.h>
#include <psapi.h>
#include <tchar.h>
#endif

#define SUFFIX  "*"
#define SLASH   "\\"

#define MAX_PM 32

/** \defgroup http_intf HTTP Interface
* This is the HTTP remote control interface. It is fully customizable
* by writing HTML pages using custom <vlc> tags.
*
* These tags use so-called macros.
*
* These macros can manipulate variables. For more complex operations,
* a custom RPN evaluator with many built-in functions is provided.
* @{
*/

/*****************************************************************************
* Local defines
*****************************************************************************/
#define MAX_DIR_SIZE 2560
#define MAX_LINE_LENGTH 2048
#define STACK_MAX 100        //< Maximum RPN stack size

//#define I64Fd "%I64d"
//#define S_ISDIR(m) ((m) & _S_IFDIR)
//#define S_ISREG(m)      (((m)&_S_IFMT) == _S_IFREG)

#define E_(function)  function
typedef void *vlc_iconv_t;
//typedef struct httpd_file_sys_t httpd_file_sys_t;
typedef struct intf_sys_t intf_sys_t;

/*****************************************************************************
* Utility functions
*****************************************************************************/

/* File and directory functions */

/** This function recursively parses a directory and adds all files */
int E_(ParseDirectory)( intf_sys_t* p_sys);
/** This function loads a file into a buffer */
int E_(FileLoad)( FILE *f, char **pp_data, int *pi_data );
/** This function creates a suitable URL for a filename */
char *E_(FileToUrl)( char *name, vlc_bool_t *pb_index );

/* URI Handling functions */

/** This function extracts the value for a given argument name
* from an HTTP request */
char *E_(ExtractURIValue)( char *psz_uri, const char *psz_name,
						  char *psz_value, int i_value_max );
/** \todo Describe this function */
int E_(TestURIParam)( char *psz_uri, const char *psz_name );
/** This function extracts the original value from an URL-encoded string */
void E_(DecodeEncodedURI)( char *psz );

/** Return the first word from a string (works in-place) */
char *E_(FirstWord)( char *psz, char *new );

/**@}*/

/****************************************************************************
* Variable handling functions
****************************************************************************/

/** \defgroup http_vars Macro variables
* \ingroup http_intf
* These variables can be used in the <vlc> macros and in the RPN evaluator.
* The variables make a tree: each variable can have an arbitrary
* number of "children" variables.
* A number of helper functions are provided to manipulate the main variable
* structure
* @{
*/

/**
* \struct mvar_t
* This structure defines a macro variable
*/
typedef struct mvar_s
{
	char *name;                 ///< Variable name
	char *value;                ///< Variable value

	int           i_field;      ///< Number of children variables
	struct mvar_s **field;      ///< Children variables array
} mvar_t;


/** This function creates a new variable */
mvar_t  *E_(mvar_New)( const char *name, const char *value );
/** This function deletes a variable */
void     E_(mvar_Delete)( mvar_t *v );
/** This function adds f to the children variables of v, at last position */
void     E_(mvar_AppendVar)( mvar_t *v, mvar_t *f );
/** This function duplicates a variable */
mvar_t  *E_(mvar_Duplicate)( const mvar_t *v );
/** This function adds f to the children variables of v, at fist position */
void     E_(mvar_PushVar)( mvar_t *v, mvar_t *f );
/** This function removes f from the children variables of v */
void     E_(mvar_RemoveVar)( mvar_t *v, mvar_t *f );
/** This function retrieves the child variable named "name" */
mvar_t  *E_(mvar_GetVar)( mvar_t *s, const char *name );
/** This function retrieves the value of the child variable named "field" */
char    *E_(mvar_GetValue)( mvar_t *v, char *field );
/** This function creates a variable with the given name and value and
* adds it as first child of vars */
void     E_(mvar_PushNewVar)( mvar_t *vars, const char *name,
							 const char *value );
/** This function creates a variable with the given name and value and
* adds it as last child of vars */
void     E_(mvar_AppendNewVar)( mvar_t *vars, const char *name,
							   const char *value );
/** @} */

/** \defgroup http_sets Sets *
* \ingroup http_intf
* Sets are an application of the macro variables. There are a number of
* predefined functions that will give you variables whose children represent
* VLC internal data (playlist, stream info, ...)
* @{
*/

/** This function creates a set variable which represents a series of integer
* The arg parameter must be of the form "start[:stop[:step]]"  */
mvar_t *E_(mvar_IntegerSetNew)( const char *name, const char *arg );
mvar_t *E_(mvar_ProgramSetNew)( intf_sys_t* p_sys, char *name );
mvar_t *E_(mvar_EncodersSetNew)( intf_sys_t* p_sys, char *name );
mvar_t *E_(mvar_DeviceSetNew)( intf_sys_t* p_sys, char *name );
mvar_t *E_(mvar_AudioTrackSetNew)( intf_sys_t* p_sys, char *name );
mvar_t *E_(mvar_NetworkSetNew)( intf_sys_t* p_sys, char *name );

/**@}*/

/*****************************************************************************
* RPN Evaluator
*****************************************************************************/

/** \defgroup http_rpn RPN Evaluator
* \ingroup http_intf
* @{
*/

/**
* \struct rpn_stack_t
* This structure represents a stack of RPN commands for the HTTP interface
* It is attached to a request
*/
typedef struct
{
	char *stack[STACK_MAX];
	int  i_stack;
} rpn_stack_t;

/** This function creates the RPN evaluator stack */
void E_(SSInit)( rpn_stack_t * );
/** This function cleans the evaluator stack */
void E_(SSClean)( rpn_stack_t * );
/* Evaluate and execute the RPN Stack */
void  E_(EvaluateRPN)( intf_sys_t     *p_sys, mvar_t  *vars,
					  rpn_stack_t *st, char *exp );

/* Push an operand on top of the RPN stack */
void E_(SSPush)  ( rpn_stack_t *, const char * );
/* Remove the first operand from the RPN Stack */
char *E_(SSPop)  ( rpn_stack_t * );
/* Pushes an operand at a given position in the stack */
void E_(SSPushN) ( rpn_stack_t *, int );
/* Removes an operand at the given position in the stack */
int  E_(SSPopN)  ( rpn_stack_t *, mvar_t  * );

/**@}*/


/****************************************************************************
* Macro handling (<vlc ... stuff)
****************************************************************************/

/** \defgroup http_macros <vlc> Macros Handling
* \ingroup http_intf
* A macro is a code snippet in the HTML page looking like
* <vlc id="macro_id" param1="value1" param2="value2">
* Macros string ids are mapped to macro types, and specific handling code
* must be written for each macro type
* @{
*/


/** \struct macro_t
* This structure represents a HTTP Interface macro.
*/
typedef struct
{
	char *id;           ///< Macro ID string
	char *param1;       ///< First parameter
	char *param2;       ///< Second parameter
} macro_t;

/** This function creates a macro from a <vlc ....> tag */
int E_(MacroParse)( macro_t *m, char *psz_src );
/** This function cleans a macro */
void E_(MacroClean)( macro_t *m );

/** This function returns the macro type identifier from its id= string value
* It uses the StrToMacroTypeTab mapping array for this */
int E_(StrToMacroType)( char *name );
/** This function actually executes the macro */
void E_(MacroDo)( httpd_file_sys_t *p_args, macro_t *m,
				 char *p_request, int i_request, char **pp_data,
				 int *pi_data, char **pp_dst );
/** This function looks for macros in a string */
char *E_(MacroSearch)( char *src, char *end,
					  int i_mvlc, vlc_bool_t b_after );

/** This function parses a file for macros */
void E_(Execute)( httpd_file_sys_t *p_args,
				 char *p_request, int i_request,
				 char **pp_data, int *pi_data,
				 char **pp_dst,
				 char *_src, char *_end );

/**@}*/

/**
* Core stuff
*/
/** \struct
* This structure represent a single HTML file to be parsed by the macros
* handling engine */
struct httpd_file_sys_t
{
	intf_sys_t    *p_sys;
	httpd_file_t     *p_file;
	httpd_redirect_t *p_redir;
	httpd_redirect_t *p_redir2;

	char          *file;
	char          *name;

	vlc_bool_t    b_html, b_handler;

	/* inited for each access */
	rpn_stack_t   stack;
	mvar_t        *vars;
};

/** \struct
* Structure associating an extension to an external program
*/
typedef struct http_association_t
{
	char                *psz_ext;
	int                 i_argc;
	char                **ppsz_argv;
} http_association_t;

/** \struct
* This structure represent a single CGI file to be parsed by the macros
* handling engine */
struct httpd_handler_sys_t
{
	httpd_file_sys_t file;

	/* HACK ALERT: this is added below so that casting httpd_handler_sys_t
	* to httpd_file_sys_t works */
	httpd_handler_t  *p_handler;
	http_association_t *p_association;
};

enum
{
	NULL_IN = 0,
    AV_IN   = 1,
	STRM_IN = 2,
};

enum
{
	NULL_OUT = 0,
    UDP_OUT  = 1,
	URL_OUT  = 2,
};


typedef enum ESTYPE_{
	ES_V_MPG2=1,
	ES_V_264,
	ES_V_265,
	ES_A_MPG2=1,
	ES_A_AC3,
	ES_A_EAC3,
	ES_A_AAC,
	ES_S_DVB=1,
	ES_S_TEL,
}ESTYPE;

typedef struct http_param_sys_
{
	uint8_t     psz_url[128];
	uint8_t     stream_state[256];
	int         i_filter_type;
	int         i_input_type;
	int  	    i_max;
	int  	    i_min;
	double      f_fps;
	int      	b_image_unet;
	int         b_object_detect;
	int         b_object_show;

	volatile int system_request;
	volatile int status_request;
	volatile int program_request;
	volatile int param_request;
	volatile int param_change;
	volatile int status_change;
	int64_t last_time;
}http_param_sys_t;

/** \struct
* Internal service structure for the HTTP interface
*/
struct intf_sys_t
{
	http_param_sys_t params[MAX_PM];
	httpd_host_t    *p_httpd_host;

	int				 id;
	int              i_number;
	char			 psz_dir[256];

	int              i_files;
	httpd_file_sys_t *pp_files[8];

	char             *psz_html_type;
	vlc_iconv_t       iconv_from_utf8, iconv_to_utf8;
};

void *http_open(char *psz_dir, int port);
void http_close (void *param);

/** This function is the main HTTPD Callback used by the HTTP Interface */
int E_(HttpCallback)( httpd_file_sys_t *p_args,
					 httpd_file_t *,
					 uint8_t *p_request,
					 uint8_t **pp_data, int *pi_data );

#endif

