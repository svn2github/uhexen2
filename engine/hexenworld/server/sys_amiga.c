/*
	sys_amiga.c
	Amiga system interface code

	$Id$
*/

#include "quakedef.h"

#include <proto/exec.h>
#include <proto/dos.h>

#include <time.h>


// heapsize: minimum 8 mb, standart 16 mb, max is 32 mb.
// -heapsize argument will abide by these min/max settings
// unless the -forcemem argument is used
#define MIN_MEM_ALLOC	0x0800000
#define STD_MEM_ALLOC	0x1000000
#define MAX_MEM_ALLOC	0x2000000

cvar_t		sys_nostdout = {"sys_nostdout", "0", CVAR_NONE};
int		devlog;	/* log the Con_DPrintf and Sys_DPrintf content when !developer.integer */

static double		starttime;
static qboolean		first = true;


/*
===============================================================================

FILE IO

===============================================================================
*/

int Sys_mkdir(const char *path, qboolean crash)
{
	BPTR lock = CreateDir((const STRPTR) path);

	if (lock)
	{
		UnLock(lock);
		return 0;
	}

	if (IoErr() == ERROR_OBJECT_EXISTS)
		return 0;

	if (crash)
		Sys_Error("Unable to create directory %s", path);
	return -1;
}

int Sys_rmdir (const char *path)
{
	if (DeleteFile((const STRPTR) path) != 0)
		return 0;
	return -1;
}

int Sys_unlink (const char *path)
{
	if (DeleteFile((const STRPTR) path) != 0)
		return 0;
	return -1;
}

int Sys_rename (const char *oldp, const char *newp)
{
	if (Rename((const STRPTR) oldp, (const STRPTR) newp) != 0)
		return 0;
	return -1;
}

long Sys_filesize (const char *path)
{
	long size = -1;
	BPTR fh = Open((const STRPTR) path, MODE_OLDFILE);
	if (fh != NULL)
	{
		struct FileInfoBlock *fib = (struct FileInfoBlock*)
					AllocDosObject(DOS_FIB, NULL);
		if (fib != NULL)
		{
			if (ExamineFH(fh, fib))
				size = fib->fib_Size;
			FreeDosObject(DOS_FIB, fib);
		}
		Close(fh);
	}
	return size;
}

int Sys_FileType (const char *path)
{
	long type = FS_ENT_NONE;
	BPTR fh = Open((const STRPTR) path, MODE_OLDFILE);
	if (fh != NULL)
	{
		struct FileInfoBlock *fib = (struct FileInfoBlock*)
					AllocDosObject(DOS_FIB, NULL);
		if (fib != NULL)
		{
			if (ExamineFH(fh, fib))
			{
				if (fib->fib_DirEntryType >= 0)
					type = FS_ENT_DIRECTORY;
				else	type = FS_ENT_FILE;
			}
			FreeDosObject(DOS_FIB, fib);
		}
		Close(fh);
	}
	return type;
}

#define	COPY_READ_BUFSIZE		8192	/* BUFSIZ */
int Sys_CopyFile (const char *frompath, const char *topath)
{
	char buf[COPY_READ_BUFSIZE];
	long remaining = 0;
	long count;
	BPTR in, out;
	struct FileInfoBlock *fib;

	in = Open((const STRPTR) frompath, MODE_OLDFILE);
	if (!in)
	{
		Con_Printf ("%s: unable to open %s\n", frompath, __thisfunc__);
		return 1;
	}
	fib = (struct FileInfoBlock*) AllocDosObject(DOS_FIB, NULL);
	if (fib != NULL)
	{
		if (ExamineFH(in, fib))
			remaining = fib->fib_Size;
		FreeDosObject(DOS_FIB, fib);
	}
	else
	{
		Con_Printf ("%s: can't allocate the FileInfoBlock %s\n", frompath, __thisfunc__);
		return 1;
	}
	out = Open((const STRPTR) topath, MODE_NEWFILE);
	if (!out)
	{
		Con_Printf ("%s: unable to open %s\n", topath, __thisfunc__);
		Close(in);
		return 1;
	}

	memset (buf, 0, sizeof(buf));
	while (remaining)
	{
		if (remaining < sizeof(buf))
			count = remaining;
		else
			count = sizeof(buf);

		FRead(in, buf, 1, count);
		if (IoErr())
			break;

		FWrite(out, buf, 1, count);
		if (IoErr())
			break;

		remaining -= count;
	}

	Close(in);
	Close(out);

	if (remaining != 0)
		return 1;

	return 0;
}

/*
=================================================
simplified findfirst/findnext implementation:
Sys_FindFirstFile and Sys_FindNextFile return
filenames only, not a dirent struct. this is
what we presently need in this engine.
=================================================
*/

#define PATH_SIZE 1024
static struct AnchorPath *apath;
static struct FileInfoBlock *find_fib;
static BPTR oldcurrentdir;

static STRPTR pattern_helper (const char *pat)
{
	char	*pdup;
	const char	*p;
	int	n;

	for (n = 0, p = pat; p != NULL; )
	{
		p = strchr (p, '*');
		if (p != NULL)
		{
			++n;	++p;
		}
	}

	if (n == 0)
		pdup = Z_Strdup(pat);
	else
	{
	/* replace each "*" by "#?" */
		n += (int) strlen(pat) + 1;
		pdup = (char *) Z_Malloc(n, Z_MAINZONE);

		for (n = 0, p = pat; *p != '\0'; ++p, ++n)
		{
			if (*p != '*')
				pdup[n] = *p;
			else
			{
				pdup[n] = '#'; ++n;
				pdup[n] = '?';
			}
		}
		pdup[n] = '\0';
	}

	return (STRPTR) pdup;
}

const char *Sys_FindFirstFile (const char *path, const char *pattern)
{
	STRPTR patfix;
	LONG err;

	if (apath)
		Sys_Error ("Sys_FindFirst without FindClose");

	apath = AllocMem (sizeof(struct AnchorPath) + PATH_SIZE, MEMF_CLEAR);
	if (!apath)
		return NULL;

	apath->ap_Strlen = PATH_SIZE;
	apath->ap_BreakBits = 0;
	apath->ap_Flags = APB_DOWILD | !APB_DODIR;
	oldcurrentdir = CurrentDir(Lock((const STRPTR) path, SHARED_LOCK));
	patfix = pattern_helper (pattern);
	err = MatchFirst((const STRPTR) patfix, apath);
	Z_Free (patfix);

	if (err == 0)
	{
		find_fib = &(apath->ap_Info);
		return (const char *) find_fib->fib_FileName;
	}

	return NULL;
}

const char *Sys_FindNextFile (void)
{
	if (!apath)
		return NULL;

	if (MatchNext(apath) == 0)
	{
		find_fib = &(apath->ap_Info);
		return (const char *) find_fib->fib_FileName;
	}

	return NULL;
}

void Sys_FindClose (void)
{
	if (apath == NULL)
		return;
	MatchEnd(apath);
	FreeMem(apath, sizeof(struct AnchorPath) + PATH_SIZE);
	CurrentDir(oldcurrentdir);
	oldcurrentdir = NULL;
	apath = NULL;
	find_fib = NULL;
}

/*
===============================================================================

SYSTEM IO

===============================================================================
*/

/*
================
Sys_Init
================
*/
static void Sys_Init (void)
{
}

#define ERROR_PREFIX	"\nFATAL ERROR: "
void Sys_Error (const char *error, ...)
{
	va_list		argptr;
	char		text[MAX_PRINTMSG];
	const char	text2[] = ERROR_PREFIX;
	const unsigned char	*p;

	va_start (argptr, error);
	q_vsnprintf (text, sizeof(text), error, argptr);
	va_end (argptr);

	if (sv_logfile)
	{
		fprintf (sv_logfile, ERROR_PREFIX "%s\n\n", text);
		fflush (sv_logfile);
	}

	for (p = (const unsigned char *) text2; *p; p++)
		putc (*p, stderr);
	for (p = (const unsigned char *) text ; *p; p++)
		putc (*p, stderr);
	putc ('\n', stderr);
	putc ('\n', stderr);

	exit (1);
}

void Sys_PrintTerm (const char *msgtxt)
{
	const unsigned char	*p;

	if (sys_nostdout.integer)
		return;

	for (p = (const unsigned char *) msgtxt; *p; p++)
		putc (*p, stdout);
}

void Sys_Quit (void)
{
	exit (0);
}


/*
================
Sys_DoubleTime
================
*/
double Sys_DoubleTime (void)
{
	struct timeval	tp;
	double		now;

	GetSysTime(&tp);

	now = tp.tv_sec + tp.tv_usec / 1e6;

	if (first)
	{
		first = false;
		starttime = now;
		return 0.0;
	}

	return now - starttime;
}

char *Sys_DateTimeString (char *buf)
{
	static char strbuf[24];
	time_t t;
	struct tm *l;
	int val;

	if (!buf) buf = strbuf;

	t = time(NULL);
	l = localtime(&t);

	val = l->tm_mon + 1;	/* tm_mon: months since January [0,11] */
	buf[0] = val / 10 + '0';
	buf[1] = val % 10 + '0';
	buf[2] = '/';
	val = l->tm_mday;
	buf[3] = val / 10 + '0';
	buf[4] = val % 10 + '0';
	buf[5] = '/';
	val = l->tm_year / 100 + 19;	/* tm_year: #years since 1900. */
	buf[6] = val / 10 + '0';
	buf[7] = val % 10 + '0';
	val = l->tm_year % 100;
	buf[8] = val / 10 + '0';
	buf[9] = val % 10 + '0';

	buf[10] = ' ';

	val = l->tm_hour;
	buf[11] = val / 10 + '0';
	buf[12] = val % 10 + '0';
	buf[13] = ':';
	val = l->tm_min;
	buf[14] = val / 10 + '0';
	buf[15] = val % 10 + '0';
	buf[16] = ':';
	val = l->tm_sec;
	buf[17] = val / 10 + '0';
	buf[18] = val % 10 + '0';

	buf[19] = '\0';

	return buf;
}


/*
================
Sys_ConsoleInput
================
*/
const char *Sys_ConsoleInput (void)
{
	static char	con_text[256];
	static int	textlen;
	char		c;

	while (WaitForChar(Input(),10))
	{
		Read (0, &c, 1);
		if (c == '\n' || c == '\r')
		{
			con_text[textlen] = '\0';
			textlen = 0;
			return con_text;
		}
		else if (c == 8)
		{
			if (textlen)
			{
				textlen--;
				con_text[textlen] = '\0';
			}
			continue;
		}
		con_text[textlen] = c;
		textlen++;
		if (textlen < (int) sizeof(con_text))
			con_text[textlen] = '\0';
		else
		{
		// buffer is full
			textlen = 0;
			con_text[0] = '\0';
			Sys_PrintTerm("\nConsole input too long!\n");
			break;
		}
	}

	return NULL;
}

static int Sys_GetBasedir (char *argv0, char *dst, size_t dstsize)
{
	struct Task *self;
	BPTR lock;

	self = FindTask(NULL);
	lock = ((struct Process *) self)->pr_CurrentDir;

	if (NameFromLock(lock, (STRPTR) dst, dstsize) != 0)
		return 0;
	return -1;
}

static void PrintVersion (void)
{
	Sys_Printf ("HexenWorld server %4.2f (%s)\n", ENGINE_VERSION, PLATFORM_STRING);
	Sys_Printf ("Hammer of Thyrion, release %s (%s)\n", HOT_VERSION_STR, HOT_VERSION_REL_DATE);
}

/*
===============================================================================

MAIN

===============================================================================
*/
static quakeparms_t	parms;
static char	cwd[MAX_OSPATH];

int main (int argc, char **argv)
{
	int			i;
	double		newtime, time, oldtime;

	PrintVersion();

	if (argc > 1)
	{
		for (i = 1; i < argc; i++)
		{
			if ( !(strcmp(argv[i], "-v")) || !(strcmp(argv[i], "-version" )) ||
				!(strcmp(argv[i], "--version")) )
			{
				exit(0);
			}
			else if ( !(strcmp(argv[i], "-h")) || !(strcmp(argv[i], "-help" )) ||
				  !(strcmp(argv[i], "-?")) || !(strcmp(argv[i], "--help")) )
			{
				Sys_PrintTerm ("See the documentation for details\n");
				exit (0);
			}
		}
	}

	memset (cwd, 0, sizeof(cwd));
	if (Sys_GetBasedir(argv[0], cwd, sizeof(cwd)) != 0)
		Sys_Error ("Couldn't determine current directory");

	/* initialize the host params */
	memset (&parms, 0, sizeof(parms));
	parms.basedir = cwd;
	parms.userdir = cwd;
	parms.argc = argc;
	parms.argv = argv;
	host_parms = &parms;

	devlog = COM_CheckParm("-devlog");

	Sys_Printf("basedir is: %s\n", parms.basedir);
	Sys_Printf("userdir is: %s\n", parms.userdir);

	COM_ValidateByteorder ();

	parms.memsize = STD_MEM_ALLOC;

	i = COM_CheckParm ("-heapsize");
	if (i && i < com_argc-1)
	{
		parms.memsize = atoi (com_argv[i+1]) * 1024;

		if ((parms.memsize > MAX_MEM_ALLOC) && !(COM_CheckParm ("-forcemem")))
		{
			Sys_Printf ("Requested memory (%d Mb) too large, using the default maximum.\n", parms.memsize/(1024*1024));
			Sys_Printf ("If you are sure, use the -forcemem switch.\n");
			parms.memsize = MAX_MEM_ALLOC;
		}
		else if ((parms.memsize < MIN_MEM_ALLOC) && !(COM_CheckParm ("-forcemem")))
		{
			Sys_Printf ("Requested memory (%d Mb) too little, using the default minimum.\n", parms.memsize/(1024*1024));
			Sys_Printf ("If you are sure, use the -forcemem switch.\n");
			parms.memsize = MIN_MEM_ALLOC;
		}
	}

	parms.membase = malloc (parms.memsize);

	if (!parms.membase)
		Sys_Error ("Insufficient memory.\n");

	Sys_Init ();

	SV_Init();

// report the filesystem to the user
	Sys_Printf("fs_gamedir is: %s\n", fs_gamedir);
	Sys_Printf("fs_userdir is: %s\n", fs_userdir);

//
// main loop
//
	oldtime = Sys_DoubleTime () - HX_FRAME_TIME;
	while (1)
	{
		if (NET_CheckReadTimeout(0, 10000) == -1)
			continue;

		newtime = Sys_DoubleTime ();
		time = newtime - oldtime;
		oldtime = newtime;

		SV_Frame (time);
	}

	return 0;
}
