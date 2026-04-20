/*****************************************************************************
 * network.h: interface to communicate with network plug-ins
 *****************************************************************************
 * Copyright (C) 2002-2005 the VideoLAN team
 * $Id: network.h 12898 2005-10-20 12:52:05Z md $
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
 *          Laurent Aimar <fenrir@via.ecp.fr>
 *          Rémi Denis-Courmont <rem # videolan.org>
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

#ifndef __VLC_NETWORK_H
# define __VLC_NETWORK_H

#if defined( WIN32 )
#   if defined(UNDER_CE) && defined(sockaddr_storage)
#       undef sockaddr_storage
#   endif
#   if defined(UNDER_CE)
#       define HAVE_STRUCT_ADDRINFO
#   else
#       include <io.h>
#   endif
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   define ENETUNREACH WSAENETUNREACH
#else
#   include <sys/socket.h>
#   include <netinet/in.h>
#   include <arpa/inet.h>
#   include <netdb.h>
#endif

# ifdef __cplusplus
extern "C" {
# endif

int isurlsafe( int c );

#define net_ListenTCP(b, c) __net_ListenTCP(b, c)
 int * __net_ListenTCP (  const char *, int  );

#define net_ReadNonBlock(a, b, c, d) __net_ReadNonBlock(a, b, c, d)
int __net_ReadNonBlock(int fd, uint8_t *p_data, int i_data, int64_t i_wait);

#define net_WriteNonBlock(a, b, c, d) __net_WriteNonBlock(a, b, c, d)
int __net_WriteNonBlock( int fd, const uint8_t *p_data, int i_data, int64_t i_wait);

#define net_Accept(a, b) __net_Accept(a, b)
int __net_Accept( int *pi_fd, int64_t i_wait );

 void net_Close ( int fd  );
 void net_ListenClose ( int *fd  );

/*****************************************************************************
 * net_StopRecv/Send
 *****************************************************************************
 * Wrappers for shutdown()
 *****************************************************************************/
#if defined (SHUT_WR)
/* the standard way */
# define net_StopSend( fd ) (void)shutdown( fd, SHUT_WR )
# define net_StopRecv( fd ) (void)shutdown( fd, SHUT_RD )
#elif defined (SD_SEND)
/* the Microsoft seemingly-purposedly-different-for-the-sake-of-it way */
# define net_StopSend( fd ) (void)shutdown( fd, SD_SEND )
# define net_StopRecv( fd ) (void)shutdown( fd, SD_RECEIVE )
#else
# ifndef SYS_BEOS /* R5 just doesn't have a working shutdown() */
#  warning FIXME: implement shutdown on your platform!
# endif
# define net_StopSend( fd ) (void)0
# define net_StopRecv( fd ) (void)0
#endif

/* Portable network names/addresses resolution layer */

/* GAI error codes */
# ifndef EAI_BADFLAGS
#  define EAI_BADFLAGS -1
# endif
# ifndef EAI_NONAME
#  define EAI_NONAME -2
# endif
# ifndef EAI_AGAIN
#  define EAI_AGAIN -3
# endif
# ifndef EAI_FAIL
#  define EAI_FAIL -4
# endif
# ifndef EAI_NODATA
#  define EAI_NODATA -5
# endif
# ifndef EAI_FAMILY
#  define EAI_FAMILY -6
# endif
# ifndef EAI_SOCKTYPE
#  define EAI_SOCKTYPE -7
# endif
# ifndef EAI_SERVICE
#  define EAI_SERVICE -8
# endif
# ifndef EAI_ADDRFAMILY
#  define EAI_ADDRFAMILY -9
# endif
# ifndef EAI_MEMORY
#  define EAI_MEMORY -10
# endif
# ifndef EAI_SYSTEM
#  define EAI_SYSTEM -11
# endif


# ifndef NI_MAXHOST
#  define NI_MAXHOST 1025
#  define NI_MAXSERV 32
# endif
# define NI_MAXNUMERICHOST 64

# ifndef NI_NUMERICHOST
#  define NI_NUMERICHOST 0x01
#  define NI_NUMERICSERV 0x02
#  define NI_NOFQDN      0x04
#  define NI_NAMEREQD    0x08
#  define NI_DGRAM       0x10
# endif

#define _NI_MASK (NI_NUMERICHOST|NI_NUMERICSERV|NI_NOFQDN|NI_NAMEREQD|\
NI_DGRAM)
#define _AI_MASK (AI_PASSIVE|AI_CANONNAME|AI_NUMERICHOST)

int  vlc_getnameinfo( const struct sockaddr *sa, int salen,
							 char *host, int hostlen, int *portnum, int flags );
void vlc_freeaddrinfo( struct addrinfo *infos );
int  vlc_getaddrinfo(  const char *node,
							 int i_port, const struct addrinfo *p_hints,struct addrinfo **res );

# ifdef __cplusplus
}
# endif

#endif
