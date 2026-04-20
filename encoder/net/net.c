/*****************************************************************************
 * net.c:
 *****************************************************************************
 * Copyright (C) 2004-2005 the VideoLAN team
 * $Id: net.c 12946 2005-10-23 16:24:30Z gbazin $
 *
 * Authors: Laurent Aimar <fenrir@videolan.org>
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

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#ifdef HAVE_SYS_TIME_H
#    include <sys/time.h>
#endif

#   include <unistd.h>

#include "network.h"

#include <ifaddrs.h>
#include <sys/ioctl.h>
#include <net/if.h>

#ifndef INADDR_ANY
#   define INADDR_ANY  0x00000000
#endif
#ifndef INADDR_NONE
#   define INADDR_NONE 0xFFFFFFFF
#endif


static struct
{
    int code;
    const char *msg;
} const __gai_errlist[] =
{
    { 0,              "Error 0" },
    { EAI_BADFLAGS,   "Invalid flag used" },
    { EAI_NONAME,     "Host or service not found" },
    { EAI_AGAIN,      "Temporary name service failure" },
    { EAI_FAIL,       "Non-recoverable name service failure" },
    { EAI_NODATA,     "No data for host name" },
    { EAI_FAMILY,     "Unsupported address family" },
    { EAI_SOCKTYPE,   "Unsupported socket type" },
    { EAI_SERVICE,    "Incompatible service for socket type" },
    { EAI_ADDRFAMILY, "Unavailable address family for host name" },
    { EAI_MEMORY,     "Memory allocation failure" },
    { EAI_SYSTEM,     "System error" },
    { 0,              NULL }
};

static const char *__gai_unknownerr = "Unrecognized error number";

/****************************************************************************
 * Converts an EAI_* error code into human readable english text.
 ****************************************************************************/
const char *vlc_gai_strerror( int errnum )
{
    int i;

    for (i = 0; __gai_errlist[i].msg != NULL; i++)
        if (errnum == __gai_errlist[i].code)
            return __gai_errlist[i].msg;

    return __gai_unknownerr;
}

int net_Socket( int i_family, int i_socktype,
                       int i_protocol )
{
    int fd, i_val;

    fd = socket( i_family, i_socktype, i_protocol );
    if( fd == -1 )
    {
        return -1;
    }

#if defined( WIN32 ) || defined( UNDER_CE )
    {
        unsigned long i_dummy = 1;
        if( ioctlsocket( fd, FIONBIO, &i_dummy ) != 0 )
	return -1;
            //msg_Err( p_this, "cannot set socket to non-blocking mode" );
    }
#else
    if( fd >= FD_SETSIZE )
    {
        /* We don't want to overflow select() fd_set */
        //msg_Err( p_this, "cannot create socket (too many already in use)" );
        net_Close( fd );
        return -1;
    }

    fcntl( fd, F_SETFD, FD_CLOEXEC );
    i_val = fcntl( fd, F_GETFL, 0 );
    fcntl( fd, F_SETFL, ((i_val != -1) ? i_val : 0) | O_NONBLOCK );
#endif

    i_val = 1;
    setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, (void *)&i_val,
                sizeof( i_val ) );

#ifdef IPV6_V6ONLY
    /*
     * Accepts only IPv6 connections on IPv6 sockets
     * (and open an IPv4 socket later as well if needed).
     * Only Linux and FreeBSD can map IPv4 connections on IPv6 sockets,
     * so this allows for more uniform handling across platforms. Besides,
     * it makes sure that IPv4 addresses will be printed as w.x.y.z rather
     * than ::ffff:w.x.y.z
     */
    if( i_family == AF_INET6 )
        setsockopt( fd, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&i_val,
                    sizeof( i_val ) );
#endif

    return fd;
}


int net_ConnectTCP( const char *psz_host, int i_port )
{
    struct addrinfo hints, *res, *ptr;
    const char      *psz_realhost;
    int             i_realport, i_val, i_handle = -1, i_saved_errno = 0;
    unsigned        u_errstep = 0;

    if( i_port == 0 )
        i_port = 80; /* historical VLC thing */

    memset( &hints, 0, sizeof( hints ) );
    hints.ai_socktype = SOCK_STREAM;

    {
        psz_realhost = psz_host;
        i_realport = i_port;

        //printf("net: connecting to %s port %d\n", psz_realhost,
        //         i_realport );
    }

    i_val = vlc_getaddrinfo( psz_realhost, i_realport, &hints, &res );
    if( i_val )
    {
        return -1;
    }

    for( ptr = res; ptr != NULL; ptr = ptr->ai_next )
    {
        int fd = net_Socket( ptr->ai_family, ptr->ai_socktype,
                             ptr->ai_protocol );
        if( fd == -1 )
        {
            if( u_errstep <= 0 )
            {
                u_errstep = 1;
                i_saved_errno = errno;
            }
            printf("socket error: %s\n", strerror( errno ) );
            continue;
        }

        if( connect( fd, ptr->ai_addr, ptr->ai_addrlen ) )
        {
            socklen_t i_val_size = sizeof( i_val );
            div_t d;
            struct timeval tv;
            int timeout = 5000;

            if( errno != EINPROGRESS )
            {
                if( u_errstep <= 1 )
                {
                    u_errstep = 2;
                    i_saved_errno = errno;
                }
                //printf("connect error: %s\n", strerror( errno ) );
                goto next_ai;
            }

            d = div( timeout, 100 );

            //printf( "connection in progress\n" );
            for (;;)
            {
                fd_set fds;
                int i_ret;

                /* Initialize file descriptor set */
                FD_ZERO( &fds );
                FD_SET( fd, &fds );

                /*
                 * We'll wait 0.1 second if nothing happens
                 * NOTE:
                 * time out will be shortened if we catch a signal (EINTR)
                 */
                tv.tv_sec = 0;
                tv.tv_usec = (d.quot > 0) ? 100000 : (1000 * d.rem);

                i_ret = select( fd + 1, NULL, &fds, NULL, &tv );
                if( i_ret == 1 )
                    break;

                if( ( i_ret == -1 ) && ( errno != EINTR ) )
                {
                    printf("select error: %s\n",
                              strerror( errno ) );
                    goto next_ai;
                }

                if( d.quot <= 0 )
                {
                    printf("select timed out\n" );
                    if( u_errstep <= 2 )
                    {
                        u_errstep = 3;
                        i_saved_errno = ETIMEDOUT;
                    }
                    goto next_ai;
                }

                d.quot--;
            }

#if !defined( SYS_BEOS ) && !defined( UNDER_CE )
            if( getsockopt( fd, SOL_SOCKET, SO_ERROR, (void*)&i_val,
                            &i_val_size ) == -1 || i_val != 0 )
            {
                u_errstep = 4;
                i_saved_errno = i_val;
                //printf("connect error (via getsockopt): %s\n",
                //         strerror( i_val ) );
                goto next_ai;
            }
#endif
        }

        i_handle = fd; /* success! */
        break;

next_ai: /* failure */
        net_Close( fd );
        continue;
    }

    vlc_freeaddrinfo( res );

    if( i_handle == -1 )
    {
        //printf( "Connection to %s port %d failed: %s\n", psz_host,
        //         i_port, strerror( errno ) );
        return -1;
    }

    return i_handle;
}

/*****************************************************************************
 * __net_ListenTCP:
 *****************************************************************************
 * Open TCP passive "listening" socket(s)
 * This function returns NULL in case of error.
 *****************************************************************************/
int *net_ListenTCP(const char *psz_host, int i_port )
{
    struct addrinfo hints, *res, *ptr;
    int             i_val, *pi_handles, i_size;

    memset( &hints, 0, sizeof( hints ) );
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    i_val = vlc_getaddrinfo( psz_host, i_port, &hints, &res );
    if( i_val )
    {
        return NULL;
    }

    pi_handles = NULL;
    i_size = 1;
    for( ptr = res; ptr != NULL; ptr = ptr->ai_next )
    {
        int fd, *newpi;

        fd = net_Socket( ptr->ai_family, ptr->ai_socktype,
                         ptr->ai_protocol );

        if( fd == -1 )
            continue;

        /* Bind the socket */
        if( bind( fd, ptr->ai_addr, ptr->ai_addrlen ) )
        {
        	printf("cannot bind socket (%s)\n", strerror( errno ) );
            net_Close( fd );
            continue;
        }
 
        /* Listen */
        if( listen( fd, 100 ) == -1 )
        {
            net_Close( fd );
            continue;
        }

        newpi = (int *)realloc( pi_handles, (++i_size) * sizeof( int ) );
        if( newpi == NULL )
        {
            net_Close( fd );
            break;
        }
        else
        {
            newpi[i_size - 2] = fd;
            pi_handles = newpi;
        }
    }

    vlc_freeaddrinfo( res );

    if( pi_handles != NULL )
        pi_handles[i_size - 1] = -1;
    return pi_handles;
}

/*****************************************************************************
 * __net_Close:
 *****************************************************************************
 * Close a network handle
 *****************************************************************************/
void net_Close( int fd )
{
    close( fd );
}

void net_ListenClose( int *pi_fd )
{
    if( pi_fd != NULL )
    {
        int *pi;

        for( pi = pi_fd; *pi != -1; pi++ )
            net_Close( *pi );
        free( pi_fd );
    }
}

static void
__freeaddrinfo (struct addrinfo *res)
{
    if (res != NULL)
    {
        if (res->ai_canonname != NULL)
            free (res->ai_canonname);
        if (res->ai_addr != NULL)
            free (res->ai_addr);
        if (res->ai_next != NULL)
            free (res->ai_next);
        free (res);
    }
}

/*
 * Internal function that builds an addrinfo struct.
 */
static struct addrinfo *
makeaddrinfo (int af, int type, int proto,
              const struct sockaddr *addr, size_t addrlen,
              const char *canonname)
{
    struct addrinfo *res;

    res = (struct addrinfo *)malloc (sizeof (struct addrinfo));
    if (res != NULL)
    {
        res->ai_flags = 0;
        res->ai_family = af;
        res->ai_socktype = type;
        res->ai_protocol = proto;
        res->ai_addrlen = addrlen;
        res->ai_addr = malloc (addrlen);
        res->ai_canonname = NULL;
        res->ai_next = NULL;

        if (res->ai_addr != NULL)
        {
            memcpy (res->ai_addr, addr, addrlen);

            if (canonname != NULL)
            {
                res->ai_canonname = strdup (canonname);
                if (res->ai_canonname != NULL)
                    return res; /* success ! */
            }
            else
                return res;
        }
    }
    /* failsafe */
    vlc_freeaddrinfo (res);
    return NULL;
}


static struct addrinfo *
makeipv4info (int type, int proto, u_long ip, u_short port, const char *name)
{
    struct sockaddr_in addr;

    memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
# ifdef HAVE_SA_LEN
    addr.sin_len = sizeof (addr);
# endif
    addr.sin_port = port;
    addr.sin_addr.s_addr = ip;

    return makeaddrinfo (AF_INET, type, proto,
                         (struct sockaddr*)&addr, sizeof (addr), name);
}


/*
 * getaddrinfo() non-thread-safe IPv4-only implementation
 * Address-family-independant hostname to address resolution.
 *
 * This is meant for IPv6-unaware systems that do probably not provide
 * getaddrinfo(), but still have old function gethostbyname().
 *
 * Only UDP and TCP over IPv4 are supported here.
 */
static int
__getaddrinfo (const char *node, const char *service,
               const struct addrinfo *hints, struct addrinfo **res)
{
    struct addrinfo *info;
    u_long ip;
    u_short port;
    int protocol = 0, flags = 0;
    const char *name = NULL;

    if (hints != NULL)
    {
        flags = hints->ai_flags;

        if (flags & ~_AI_MASK)
            return EAI_BADFLAGS;
        /* only accept AF_INET and AF_UNSPEC */
        if (hints->ai_family && (hints->ai_family != AF_INET))
            return EAI_FAMILY;

        /* protocol sanity check */
        switch (hints->ai_socktype)
        {
            case SOCK_STREAM:
                protocol = IPPROTO_TCP;
                break;

            case SOCK_DGRAM:
                protocol = IPPROTO_UDP;
                break;

#ifndef SYS_BEOS
            case SOCK_RAW:
#endif
            case 0:
                break;

            default:
                return EAI_SOCKTYPE;
        }
        if (hints->ai_protocol && protocol
         && (protocol != hints->ai_protocol))
            return EAI_SERVICE;
    }

    *res = NULL;

    /* default values */
    if (node == NULL)
    {
        if (flags & AI_PASSIVE)
            ip = htonl (INADDR_ANY);
        else
            ip = htonl (INADDR_LOOPBACK);
    }
    else
    if ((ip = inet_addr (node)) == INADDR_NONE)
    {
        struct hostent *entry = NULL;

        /* hostname resolution */
        if (!(flags & AI_NUMERICHOST))
            entry = gethostbyname (node);

        if (entry == NULL)
            return EAI_NONAME;

        if ((entry->h_length != 4) || (entry->h_addrtype != AF_INET))
            return EAI_FAMILY;

        ip = *((u_long *) entry->h_addr);
        if (flags & AI_CANONNAME)
            name = entry->h_name;
    }

    if ((flags & AI_CANONNAME) && (name == NULL))
        name = node;

    /* service resolution */
    if (service == NULL)
        port = 0;
    else
    {
        long d;
        char *end;

        d = strtoul (service, &end, 0);
        if (end[0] /* service is not a number */
         || (d > 65535))
        {
            struct servent *entry;
            const char *protoname;

            switch (protocol)
            {
                case IPPROTO_TCP:
                    protoname = "tcp";
                    break;

                case IPPROTO_UDP:
                    protoname = "udp";
                    break;

                default:
                    protoname = NULL;
                    break;
            }

            entry = getservbyname (service, protoname);
            if (entry == NULL)
                return EAI_SERVICE;

            port = entry->s_port;
        }
        else
            port = htons ((u_short)d);
    }

    /* building results... */
    if ((!protocol) || (protocol == IPPROTO_UDP))
    {
        info = makeipv4info (SOCK_DGRAM, IPPROTO_UDP, ip, port, name);
        if (info == NULL)
        {
            return -11;
        }
        if (flags & AI_PASSIVE)
            info->ai_flags |= AI_PASSIVE;
        *res = info;
    }
    if ((!protocol) || (protocol == IPPROTO_TCP))
    {
        info = makeipv4info (SOCK_STREAM, IPPROTO_TCP, ip, port, name);
        if (info == NULL)
        {
            return -11;
        }
        info->ai_next = *res;
        if (flags & AI_PASSIVE)
            info->ai_flags |= AI_PASSIVE;
        *res = info;
    }

    return 0;
}

/* TODO: support for setting sin6_scope_id */
int vlc_getaddrinfo(  const char *node,
                     int i_port, const struct addrinfo *p_hints,
                     struct addrinfo **res )
{
    struct addrinfo hints;
    char psz_buf[NI_MAXHOST], *psz_node, psz_service[6];

    /*
     * In VLC, we always use port number as integer rather than strings
     * for historical reasons (and portability).
     */
    if( ( i_port > 65535 ) || ( i_port < 0 ) )
    {
        return EAI_SERVICE;
    }

    /* cannot overflow */
    snprintf( psz_service, 6, "%d", i_port );

    /* Check if we have to force ipv4 or ipv6 */
    if( p_hints == NULL )
        memset( &hints, 0, sizeof( hints ) );
    else
        memcpy( &hints, p_hints, sizeof( hints ) );

    if( hints.ai_family == AF_UNSPEC )
    {
        hints.ai_family = AF_INET;

#ifdef AF_INET6
        //hints.ai_family = AF_INET6;
#endif
    }

    /* 
     * VLC extensions :
     * - accept "" as NULL
     * - ignore square brackets
     */
    if( ( node == NULL ) || (node[0] == '\0' ) )
    {
        psz_node = NULL;
    }
    else
    {
    	int len = strlen(node);
        strncpy( psz_buf, node, len );
        psz_buf[len] = '\0';

        psz_node = psz_buf;

        if( psz_buf[0] == '[' )
        {
            char *ptr;

            ptr = strrchr( psz_buf, ']' );
            if( ( ptr != NULL ) && (ptr[1] == '\0' ) )
            {
                *ptr = '\0';
                psz_node++;
            }
        }
    }

	return __getaddrinfo( psz_node, psz_service, &hints, res );
}


void vlc_freeaddrinfo( struct addrinfo *infos )
{
    __freeaddrinfo( infos );
}

/*****************************************************************************
 * __net_ReadNonBlock:
 *****************************************************************************
 * Read from a network socket, non blocking mode (with timeout)
 *****************************************************************************/
int net_ReadNonBlock(int fd,
                        uint8_t *p_data, int i_data, int64_t i_wait)
{
    struct timeval  timeout;
    fd_set          fds_r, fds_e;
    int             i_recv;
    int             i_ret;

    /* Initialize file descriptor set */
    FD_ZERO( &fds_r );
    FD_SET( fd, &fds_r );
    FD_ZERO( &fds_e );
    FD_SET( fd, &fds_e );

    timeout.tv_sec = 0;
    timeout.tv_usec = i_wait;

    i_ret = select(fd + 1, &fds_r, NULL, &fds_e, &timeout);

    if( i_ret < 0 && errno == EINTR )
    {
        return 0;
    }
    else if( i_ret < 0 )
    {
        //printf("network select error (%s)", strerror(errno) );
        return -1;
    }
    else if( i_ret == 0)
    {
        return 0;
    }
    else
    {
        if( ( i_recv = recv( fd, p_data, i_data, 0 ) ) < 0 )
        {
            //printf( "recv failed (%s)", strerror(errno) );
            return -1;
        }

        return i_recv ? i_recv : -1;  /* !i_recv -> connection closed if tcp */
    }

    /* We will never be here */
    return -1;
}


/* Write exact amount requested */
int net_WriteNonBlock( int fd,
                 const uint8_t *p_data, int i_data, int64_t i_wait)
{
    struct timeval  timeout;
    fd_set          fds_w, fds_e;
    int             i_send;

    int             i_ret;

    /* Initialize file descriptor set */
    FD_ZERO( &fds_w );
    FD_SET( fd, &fds_w );
    FD_ZERO( &fds_e );
    FD_SET( fd, &fds_e );

    /* We'll wait 0.5 second if nothing happens */
    timeout.tv_sec = 0;
    timeout.tv_usec = i_wait;


	i_ret = select(fd + 1, NULL, &fds_w, &fds_e, &timeout);

	if( i_ret < 0 )
	{
	    //printf( "network selection error (%s)", strerror(errno) );
	    return -1;
	}

	if( ( i_send = send( fd, p_data, i_data, 0 ) ) < 0 )
	{
	    /* XXX With udp for example, it will issue a message if the host
	     * isn't listening */
	    /* msg_Err( p_this, "send failed (%s)", strerror(errno) ); */
	    return -1;
	}

    return i_send;
}


/*****************************************************************************
 * __net_Accept:
 *****************************************************************************
 * Accept a connection on a set of listening sockets and return it
 *****************************************************************************/
int net_Accept( int *pi_fd, int64_t i_wait )
{

	int i_val = -1, *pi, *pi_end;
	struct timeval timeout;
	fd_set fds_r, fds_e;

	pi = pi_fd;

	/* Initialize file descriptor set */
	FD_ZERO( &fds_r );
	FD_ZERO( &fds_e );

	for( pi = pi_fd; *pi != -1; pi++ )
	{
		int i_fd = *pi;

		if( i_fd > i_val )
			i_val = i_fd;

		FD_SET( i_fd, &fds_r );
		FD_SET( i_fd, &fds_e );
	}
	pi_end = pi;

	timeout.tv_sec = 0;
	timeout.tv_usec = i_wait;

	i_val = select( i_val + 1, &fds_r, NULL, &fds_e, &timeout );
	if( ( ( i_val < 0 ) && ( errno == EINTR ) ) || i_val == 0 )
	{
		return -1;
	}
	else if( i_val < 0 )
	{
		//printf("network select error (%s)",
		//		 strerror( errno ) );
		return -1;
	}

	for( pi = pi_fd; *pi != -1; pi++ )
	{
		int i_fd = *pi;

		if( !FD_ISSET( i_fd, &fds_r ) && !FD_ISSET( i_fd, &fds_e ) )
			continue;

		i_val = accept( i_fd, NULL, 0 );
		if( i_val < 0 )
			;//printf("accept failed (%s)",
					 //strerror( errno ) );
#ifndef WIN32
		else if( i_val >= FD_SETSIZE )
		{
			net_Close( i_val ); /* avoid future overflows in FD_SET */
			//printf("accept failed (too many sockets opened)" );
		}
#endif
		else
		{
			const int yes = 1;
			setsockopt( i_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof( yes ));
			fcntl( i_fd, F_SETFD, FD_CLOEXEC );
			/*
			 * This round-robin trick ensures that the first sockets in
			 * pi_fd won't prevent the last ones from getting accept'ed.
			 */
#if 0
			--pi_end;
			memmove( pi, pi + 1, (char*)pi_end - (char*)pi);
			*pi_end = i_fd;
#endif
			return i_val;
		}
	}

    return -1;
}

void getaddr(char* ip_addr)
{
	struct ifaddrs * ifAddr=NULL;
	void *tmpAddrPtr=NULL;
	getifaddrs(&ifAddr);
	struct ifaddrs * ifAddrStruct=ifAddr;

	strcpy(ip_addr,"127.0.0.1");
	while (ifAddrStruct!=NULL) {
		if (ifAddrStruct->ifa_addr->sa_family==AF_INET)
		{
			// is a valid IP4 Address
			tmpAddrPtr=&((struct sockaddr_in *)ifAddrStruct->ifa_addr)->sin_addr;
			char addressBuffer[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
			if(strcmp(addressBuffer,"127.0.0.1"))
			{
				strcpy(ip_addr, addressBuffer);
			}
		}
#if 0
		else if (ifAddrStruct->ifa_addr->sa_family==AF_INET6) {
			// is a valid IP6 Address
			tmpAddrPtr=&((struct sockaddr_in *)ifAddrStruct->ifa_addr)->sin_addr;
			char addressBuffer[INET6_ADDRSTRLEN];
			inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
			//printf("%s IP Address %s\n", ifAddrStruct->ifa_name, addressBuffer);
		}
#endif
		ifAddrStruct=ifAddrStruct->ifa_next;
	}

	if(ifAddr)
	freeifaddrs(ifAddr);
}

int getifaddr(char* ifadress, char* ipadress)
{
	struct ifaddrs * ifAddr=NULL;
	void * tmpAddrPtr=NULL;
	int ret = 1;

	getifaddrs(&ifAddr);
	struct ifaddrs * ifAddrStruct=ifAddr;

	while (ifAddrStruct!=NULL) {
		if (ifAddrStruct->ifa_addr->sa_family==AF_INET)
		{
			// is a valid IP4 Address
			tmpAddrPtr=&((struct sockaddr_in *)ifAddrStruct->ifa_addr)->sin_addr;
			char addressBuffer[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
			//printf("%s IP Address %s\n", ifAddrStruct->ifa_name, addressBuffer);
			ret = strcmp(ifadress,ifAddrStruct->ifa_name);
			if(!ret)
			{
				strcpy(ipadress, addressBuffer);
				break;
			}
		}
#if 0
		else if (ifAddrStruct->ifa_addr->sa_family==AF_INET6) {
			// is a valid IP6 Address
			tmpAddrPtr=&((struct sockaddr_in *)ifAddrStruct->ifa_addr)->sin_addr;
			char addressBuffer[INET6_ADDRSTRLEN];
			inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
			//printf("%s IP Address %s\n", ifAddrStruct->ifa_name, addressBuffer);
		}
#endif
		ifAddrStruct=ifAddrStruct->ifa_next;
	}

	if(ifAddr)
	freeifaddrs(ifAddr);
	return ret;
}
