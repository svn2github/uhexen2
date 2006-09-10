/*
HWMQUERY 0.1 HexenWorld Master Server Query

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#ifdef _WIN32
#include <windows.h>
#endif
#ifdef PLATFORM_UNIX
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>
//#ifdef __sun__
#ifdef SUNOS
#include <sys/filio.h>
#endif
#endif

#include <errno.h>
#include <string.h>
#include <stdarg.h>

#ifdef PLATFORM_UNIX
#define ioctlsocket(A,B,C) ioctl(A,B,C)
#define closesocket(A) close(A)
#endif
#ifdef _WIN32
static WSADATA	winsockdata;
// socklen_t: on win32, it seems to be a winsock2 thing
#if !( defined(_WS2TCPIP_H) || defined(_WS2TCPIP_H_) )
typedef int	socklen_t;
#endif
#endif


//=============================================================================

typedef struct
{
	unsigned char	ip[4];
	unsigned short	port;
	unsigned short	pad;
} netadr_t;

static int		socketfd = -1;

//=============================================================================

void Sys_Error (char *error, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr,error);
	vsnprintf (text, sizeof (text), error,argptr);
	va_end (argptr);

	printf ("\nERROR: %s\n\n", text);

	exit (1);
}

//=============================================================================

static void SockadrToNetadr (struct sockaddr_in *s, netadr_t *a)
{
	*(int *)&a->ip = *(int *)&s->sin_addr;
	a->port = s->sin_port;
}

static void NetadrToSockadr (netadr_t *a, struct sockaddr_in *s)
{
	memset (s, 0, sizeof(*s));
	s->sin_family = AF_INET;

	*(int *)&s->sin_addr = *(int *)&a->ip;
	s->sin_port = a->port;
}

char *NET_AdrToString (netadr_t a)
{
	static	char	s[64];

	sprintf (s, "%i.%i.%i.%i:%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3], ntohs(a.port));

	return s;
}

static int NET_StringToAdr (char *s, netadr_t *a)
{
	struct hostent		*h;
	struct sockaddr_in	sadr;
	char	*colon;
	char	copy[128];

	memset (&sadr, 0, sizeof(sadr));
	sadr.sin_family = AF_INET;

	sadr.sin_port = 0;

	strcpy (copy, s);
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
		*(int *)&sadr.sin_addr = inet_addr(copy);
	}
	else
	{
		h = gethostbyname (copy);
		if (!h)
			return 0;
		*(int *)&sadr.sin_addr = *(int *)h->h_addr_list[0];
	}

	SockadrToNetadr (&sadr, a);

	return 1;
}

static int NET_WaitReadTimeout (int fd, long sec, long usec)
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
#ifdef _WIN32
	WORD	wVersionRequested;
	int		err;

// Init winsock
	wVersionRequested = MAKEWORD(1, 1);
	err = WSAStartup (MAKEWORD(1, 1), &winsockdata);
	if (err)
		Sys_Error ("Winsock initialization failed.");
#endif
}

static void NET_Shutdown (void)
{
	if (socketfd != -1)
		closesocket (socketfd);
#ifdef _WIN32
	WSACleanup ();
#endif
}

//=============================================================================

#define	VERSION_STR		"0.1"

#define	PORT_MASTER		26900
#define	PORT_SERVER		26950
#define	MAX_PACKET		2048
// number of 255s to put on the header
#define	HEADER_SIZE	5

#define	S2C_CHALLENGE		'c'
#define	M2C_MASTER_REPLY	'd'

int main (int argc, char *argv[])
{
	int		error_state = 0;
	int		size;
	unsigned int	pos;
	socklen_t	fromlen;
//	unsigned char	packet[3];
	char		packet[3];
	char		response[MAX_PACKET];
	netadr_t		ipaddress;
	struct sockaddr_in	hostaddress;
	unsigned long	_true = 1;

	printf ("HWMASTER QUERY %s\n", VERSION_STR);

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
	if ((socketfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		NET_Shutdown ();
		Sys_Error ("Couldn't open socket: %s", strerror(errno));
	}
// Set the socket to non-blocking mode
	if (ioctlsocket (socketfd, FIONBIO, &_true) == -1)
	{
		NET_Shutdown ();
		Sys_Error ("ioctl FIONBIO:", strerror(errno));
	}

	packet[0] = 255;
	packet[1] = S2C_CHALLENGE;
	packet[2] = '\0';

	// Send the packet
	printf ("Querying master server at %s\n", NET_AdrToString(ipaddress));
	size = sendto(socketfd, packet, 2, 0, 
			(struct sockaddr *)&hostaddress, sizeof(hostaddress));

	// See if it worked
	if (size != 2)
	{
		perror ("Sendto failed");
		printf ("Tried to send %i, sent %i\n", 2, size);
		error_state = 1;
		goto error_out;
	}

	// Read the response
	memset (response, 0, sizeof(response));
	fromlen = sizeof(hostaddress);
	if (NET_WaitReadTimeout (socketfd, 5, 0) <= 0)
	{
		printf ("*** timeout waiting for reply\n");
		error_state = 2;
	}
	else
	while (NET_WaitReadTimeout(socketfd, 0, 50000) > 0)
	{
		size = recvfrom(socketfd, response, sizeof(response), 0, 
				(struct sockaddr *)&hostaddress, &fromlen);
		if (size < 0)
		{
#ifdef _WIN32
			int err = WSAGetLastError();
			if (err != WSAEWOULDBLOCK)
			{
				printf ("Recv failed: %s\n", strerror(err));
				error_state = 1;
				goto error_out;
			}
#else
			if (errno != EWOULDBLOCK)
			{
				perror("Recv failed");
				error_state = 1;
				goto error_out;
			}
#endif
		}
		else if (response[HEADER_SIZE] != M2C_MASTER_REPLY)
		{
			printf ("Invalid response \'%c\' (%u) received\n", response[HEADER_SIZE], response[HEADER_SIZE]);
			error_state = 1;
			goto error_out;
		}
		else
		{
			char	*tmp = &response[HEADER_SIZE+2];
			struct in_addr	*addr;

			printf ("H2W Servers registered at %s:", NET_AdrToString(ipaddress));
			if (!strlen(tmp))
				printf ("  NONE\n");
			else
				printf ("\n");

			for (pos = 0; pos < strlen(tmp); pos = pos + 6)
			{
				addr = (struct in_addr*)(tmp + pos);
				printf (
					"%s:%u\n",
						inet_ntoa(*addr),
						htons( *((unsigned short*)(tmp + pos + 4)) )
					);
			}
		}
	}

error_out:
	NET_Shutdown ();
	exit (error_state);
}

