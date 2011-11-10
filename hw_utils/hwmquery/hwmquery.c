/*
	hwmquery.c
	$Id$

	HWMQUERY 0.2 HexenWorld Master Server Query
	Copyright (C) 2006-2011 O. Sezer <sezero@users.sourceforge.net>

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to:

		Free Software Foundation, Inc.
		51 Franklin St, Fifth Floor,
		Boston, MA  02110-1301, USA
*/


#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "arch_def.h"
#include "compiler.h"

#define	COMPILE_TIME_ASSERT(name, x)	\
	typedef int dummy_ ## name[(x) * 2 - 1]

#include "net_sys.h"
#include "qsnprint.h"
#if defined(PLATFORM_UNIX)
#include <sys/time.h>
#endif


//=============================================================================

typedef struct
{
	unsigned char	ip[4];
	unsigned short	port;
	unsigned short	pad;
} netadr_t;

//=============================================================================

#if defined(_MSC_VER)
#if defined(_WIN64)
#define ssize_t	SSIZE_T
#else
typedef int	ssize_t;
#endif	/* _WIN64 */
#endif	/* _MSC_VER */

#if defined(PLATFORM_WINDOWS)
#include "wsaerror.h"
static WSADATA		winsockdata;
#endif

static sys_socket_t	socketfd = INVALID_SOCKET;

void Sys_Error (const char *error, ...) __attribute__((__format__(__printf__,1,2), __noreturn__));
static void Sys_Quit (int error_state) __attribute__((__noreturn__));

//=============================================================================

void Sys_Error (const char *error, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr,error);
	q_vsnprintf (text, sizeof (text), error,argptr);
	va_end (argptr);

	printf ("\nERROR: %s\n\n", text);

	exit (1);
}

//=============================================================================

static void SockadrToNetadr (struct sockaddr_in *s, netadr_t *a)
{
	memcpy (a->ip, &s->sin_addr, 4);
	a->port = s->sin_port;
}

static void NetadrToSockadr (netadr_t *a, struct sockaddr_in *s)
{
	memset (s, 0, sizeof(*s));
	s->sin_family = AF_INET;

	memcpy (&s->sin_addr, a->ip, 4);
	s->sin_port = a->port;
}

const char *NET_AdrToString (netadr_t a)
{
	static	char	s[64];

	sprintf (s, "%i.%i.%i.%i:%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3],
							ntohs(a.port));

	return s;
}

static int NET_StringToAdr (const char *s, netadr_t *a)
{
	struct hostent		*h;
	struct sockaddr_in	sadr;
	char	*colon;
	char	copy[128];

	memset (&sadr, 0, sizeof(sadr));
	sadr.sin_family = AF_INET;
	sadr.sin_port = 0;

	strncpy (copy, s, sizeof(copy) - 1);
	copy[sizeof(copy) - 1] = '\0';
	// strip off a trailing :port if present
	for (colon = copy ; *colon ; colon++)
	{
		if (*colon == ':')
		{
			*colon = 0;
			sadr.sin_port = htons((short)atoi(colon+1));
		}
	}

	if (copy[0] >= '0' && copy[0] <= '9')
	{
		sadr.sin_addr.s_addr = inet_addr(copy);
	}
	else
	{
		h = gethostbyname (copy);
		if (!h)
			return 0;
		sadr.sin_addr.s_addr = *(in_addr_t *)h->h_addr_list[0];
	}

	SockadrToNetadr (&sadr, a);

	return 1;
}

static int NET_WaitReadTimeout (sys_socket_t fd, long sec, long usec)
{
	fd_set rfds;
	struct timeval tv;

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);

	tv.tv_sec = sec;
	tv.tv_usec = usec;

	return select(fd+1, &rfds, NULL, NULL, &tv);
}

static void NET_Init (void)
{
#if defined(PLATFORM_WINDOWS)
	int err = WSAStartup(MAKEWORD(1,1), &winsockdata);
	if (err != 0)
		Sys_Error ("Winsock initialization failed (%s)", socketerror(err));
#endif	/* PLATFORM_WINDOWS */
}

static void NET_Shutdown (void)
{
	if (socketfd != INVALID_SOCKET)
	{
		closesocket (socketfd);
		socketfd = INVALID_SOCKET;
	}
#if defined(PLATFORM_WINDOWS)
	WSACleanup ();
#endif
}

static void Sys_Quit (int error_state)
{
	NET_Shutdown ();
	exit (error_state);
}

//=============================================================================

#define	VER_HWMQUERY_MAJ	0
#define	VER_HWMQUERY_MID	2
#define	VER_HWMQUERY_MIN	3

#define	PORT_MASTER		26900
#define	PORT_SERVER		26950
#define	MAX_PACKET		2048

#define	S2C_CHALLENGE		'c'
#define	M2C_SERVERLST		'd'

static const unsigned char query_msg[] =
		{ 255, S2C_CHALLENGE, '\0' };

static const unsigned char reply_hdr[] =
		{ 255, 255, 255, 255,
		  255, M2C_SERVERLST, '\n' };

int main (int argc, char **argv)
{
	ssize_t		size;
	socklen_t	fromlen;
	unsigned char	response[MAX_PACKET];
	netadr_t		ipaddress;
	struct sockaddr_in	hostaddress;
	unsigned long	_true = 1;
	int		err;

	printf ("HWMASTER QUERY %d.%d.%d\n", VER_HWMQUERY_MAJ, VER_HWMQUERY_MID, VER_HWMQUERY_MIN);

// Command Line Sanity Checking
	if (argc < 2)
	{
		printf ("Usage: %s <address>[:port]\n", argv[0]);
		exit (1);
	}

// Init OS-specific network stuff
	NET_Init ();

// Decode the address and port
	if (!NET_StringToAdr(argv[1], &ipaddress))
	{
		NET_Shutdown ();
		Sys_Error ("Unable to resolve address %s", argv[1]);
	}
	if (ipaddress.port == 0)
		ipaddress.port = htons(PORT_MASTER);
	NetadrToSockadr(&ipaddress, &hostaddress);

// Open the Socket
	socketfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (socketfd == INVALID_SOCKET)
	{
		err = SOCKETERRNO;
		NET_Shutdown ();
		Sys_Error ("Couldn't open socket: %s", socketerror(err));
	}
// Set the socket to non-blocking mode
	if (ioctlsocket (socketfd, FIONBIO, &_true) == SOCKET_ERROR)
	{
		err = SOCKETERRNO;
		NET_Shutdown ();
		Sys_Error ("ioctl FIONBIO: %s", socketerror(err));
	}

	// Send the query packet
	printf ("Querying master server at %s\n", NET_AdrToString(ipaddress));
	size = sendto(socketfd, (const char *)query_msg, sizeof(query_msg), 0,
			(struct sockaddr *)&hostaddress, sizeof(hostaddress));

	// See if it worked
	if (size != sizeof(query_msg))
	{
		err = SOCKETERRNO;
		printf ("Sendto failed: %s\n", socketerror(err));
		Sys_Quit (1);
	}

	// Read the response
	memset (response, 0, sizeof(response));
	fromlen = sizeof(hostaddress);
	if (NET_WaitReadTimeout (socketfd, 5, 0) <= 0)
	{
		printf ("*** timeout waiting for reply\n");
		Sys_Quit (2);
	}

	size = recvfrom(socketfd, (char *)response, sizeof(response), 0,
			(struct sockaddr *)&hostaddress, &fromlen);
	if (size == SOCKET_ERROR)
	{
		err = SOCKETERRNO;
		if (err != NET_EWOULDBLOCK)
		{
			printf ("Recv failed: %s\n", socketerror(err));
			Sys_Quit (1);
		}
	}
	else if (size == sizeof(response))
	{
		printf ("Received oversized packet!\n");
		Sys_Quit (1);
	}
	else if (memcmp(response, reply_hdr, 7) != 0)
	{
		printf ("Invalid response received\n");
		Sys_Quit (1);
	}
	else
	{
		unsigned char	*tmp;
		unsigned short	port;

		printf ("H2W Servers registered at %s:", NET_AdrToString(ipaddress));

		tmp = &response[7];
		size -= 7;
		if (!size)
			printf (" NONE\n");
		else
		{
			printf (" %d entries\n", (int)size / 6);
			if (size % 6 != 0)
				printf ("Warning: not counting truncated last entry\n");
			// each address is 4 bytes (ip) + 2 bytes (port) == 6 bytes
			for (; size >= 6; size -= 6, tmp += 6)
			{
				port = ntohs (tmp[4] + (tmp[5] << 8));
				printf ("%u.%u.%u.%u:%u\n", tmp[0], tmp[1], tmp[2], tmp[3], port);
			}
		}
	}

	NET_Shutdown ();
	exit (0);
}

