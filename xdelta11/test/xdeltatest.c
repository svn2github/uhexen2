/* -*- Mode: C;-*-
 *
 * This file is part of XDelta - A binary delta generator.
 *
 * Copyright (C) 1997, 1998  Josh MacDonald
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Author: Josh MacDonald <jmacd@CS.Berkeley.EDU>
 *
 * $Id: xdeltatest.c 1.3 Mon, 11 Jun 2001 03:10:40 -0700 jmacd $
 */

#include <sys/param.h>
#include <sys/types.h>
#include <unistd.h>
#include "config.h"

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <stdio.h>

#include "xdelta.h"

#define TEST_PREFIX "/tmp/xdeltatest"

typedef struct _File File;

struct _File
{
  char name[MAXPATHLEN];
};

#undef BUFSIZ
#define BUFSIZ (1<<20)

guint8 __tmp_buffer[BUFSIZ];

void
fail ()
{
  g_warning ("FAILURE\n");

  exit (1);
}

File*
empty_file ()
{
  static gint count = 0;
  File *file = g_new0 (File, 1);

  sprintf (file->name, "%s.%d.%d", TEST_PREFIX, getpid (), count++);

  return file;
}

void
compare_files (File* fromfile, File* tofile)
{
  gint pos = 0;

  FILE* toh;
  FILE* fromh;

  guint8 buf1[1024], buf2[1024];

  toh   = fopen (tofile->name, "r");
  fromh = fopen (fromfile->name, "r");

  if (!toh || !fromh)
    fail ();

  for (;;)
    {
      gint readf = fread (buf1, 1, 1024, fromh);
      gint readt = fread (buf2, 1, 1024, toh);
      gint i, m = MIN(readf, readt);

      if (readf < 0 || readt < 0)
	fail ();

      for (i = 0; i < m; i += 1, pos += 1)
	{
	  if (buf1[i] != buf2[i])
	    fail ();
	}

      if (m != 1024)
	{
	  if (readt == readf)
	    {
	      fclose (toh);
	      fclose (fromh);
	      return;
	    }

	  fail ();
	}
    }
}

const char* cmd_delta_program = "../xdelta";
const char* cmd_delta_profile = "xdelta";
const char* cmd_data_source = "data";
guint16     cmd_seed[3] = { 47384, 8594, 27489 };
guint       cmd_size = 1<<15;
guint       cmd_reps = 100;
guint       cmd_changes = 16;
guint       cmd_deletion_length = 128;
guint       cmd_insertion_length = 256;

FILE* data_source_handle;
guint data_source_length;
File* data_source_file;

typedef struct _Instruction Instruction;

struct _Instruction {
  guint32 offset;
  guint32 length;
  Instruction* next;
};

gboolean
write_file (File* file, Instruction* inst)
{
  FILE* h;
  int ret;

  if (! (h = fopen (file->name, "w"))) {
    perror (file->name);
    return FALSE;
  }

  for (; inst; inst = inst->next)
    {
      g_assert (inst->length < BUFSIZ);

      if ((ret = fseek (data_source_handle, inst->offset, SEEK_SET))) {
	perror ("fseek");
	return FALSE;
      }

      if ((ret = fread (__tmp_buffer, 1, inst->length, data_source_handle)) != inst->length) {
	perror ("fread");
	return FALSE;
      }

      if ((ret = fwrite (__tmp_buffer, 1, inst->length, h)) != inst->length) {
	perror ("fwrite");
	return FALSE;
      }
    }

  if ((ret = fclose (h))) {
    perror ("fclose");
    return FALSE;
  }

  return TRUE;
}

static struct timeval __usr_time;
static struct timeval __sys_time;
static long dsize;

void add_tv (struct timeval* accum, struct timeval* val)
{
  accum->tv_sec += val->tv_sec;
  accum->tv_usec += val->tv_usec;

  if (accum->tv_usec >= 1000000)
    {
      accum->tv_usec %= 1000000;
      accum->tv_sec += 1;
    }
}

gboolean
run_command (File* from, File* to, File* out, gboolean accounting)
{
  int pid, status, outfd;
  struct rusage usage;
  struct stat sbuf;

  unlink (out->name);

  if ((pid = vfork()) < 0)
    return FALSE;

  if (pid == 0)
    {
      if (strcmp (cmd_delta_profile, "xdelta") == 0)
	execl (cmd_delta_program,
	       cmd_delta_program,
	       "delta",
	       "-qn0",
	       from->name,
	       to->name,
	       out->name,
	       NULL);
      else if (strcmp (cmd_delta_profile, "xdeltaz") == 0)
	execl (cmd_delta_program,
	       cmd_delta_program,
	       "delta",
	       "-qn6",
	       from->name,
	       to->name,
	       out->name,
	       NULL);
      else if (strcmp (cmd_delta_profile, "diff") == 0)
	{
	  outfd = open (out->name, O_CREAT | O_TRUNC | O_WRONLY, 0777);

	  if (outfd < 0)
	    {
	      perror ("open");
	      fail ();
	    }

	  dup2(outfd, STDOUT_FILENO);

	  if (close (outfd))
	    {
	      perror ("close");
	      fail ();
	    }


	  execl (cmd_delta_program,
		 cmd_delta_program,
		 "--rcs",
		 "-a",
		 from->name,
		 to->name,
		 NULL);
	}
      else
	{
	  g_warning ("delta profile did not match, must be one of: xdelta, diff\n");
	  _exit (127);
	}

      perror ("execl failed");
      _exit (127);
    }

  if (wait4 (pid, &status, 0, & usage) != pid)
    {
      perror ("wait failed");
      fail ();
    }

  if (! WIFEXITED (status) || WEXITSTATUS (status) > 1)
    {
      g_warning ("delta command failed\n");
      fail ();
    }

  if (stat (out->name, & sbuf))
    {
      perror ("stat");
      fail ();
    }

  if (accounting)
    {
      add_tv (& __usr_time, & usage.ru_utime);
      add_tv (& __sys_time, & usage.ru_stime);
      dsize += sbuf.st_size;
    }

  // Now verify

  return TRUE;
}

void
report (void)
{
  double t = (__usr_time.tv_sec + __sys_time.tv_sec + (__usr_time.tv_usec + __sys_time.tv_usec) / 1000000.0) / (double) cmd_reps;
  double s = (dsize / (double) cmd_reps) / (double) (cmd_changes * cmd_insertion_length);

  g_print ("time %f normalized compression %f\n", t, s);
}

guint32
random_offset (guint len)
{
  return lrand48 () % (data_source_length - len);
}

Instruction*
perform_change_rec (Instruction* inst, guint32 change_off, guint* total_len)
{
  if (change_off < inst->length)
    {
      guint32 to_delete = cmd_deletion_length;
      guint32 avail = inst->length;
      guint32 this_delete = MIN (to_delete, avail);
      Instruction* new_inst;

      // One delete
      inst->length -= this_delete;
      to_delete -= this_delete;

      while (to_delete > 0 && inst->next->length < to_delete)
	{
	  to_delete -= inst->next->length;
	  inst->next = inst->next->next;
	}

      if (to_delete > 0)
	inst->next->offset += to_delete;

      // One insert
      new_inst = g_new0 (Instruction, 1);

      new_inst->offset = random_offset (cmd_insertion_length);
      new_inst->length = cmd_insertion_length;
      new_inst->next = inst->next;
      inst->next = new_inst;

      (* total_len) += cmd_insertion_length - cmd_deletion_length;

      return inst;
    }
  else
    {
      inst->next = perform_change_rec (inst->next, change_off - inst->length, total_len);
      return inst;
    }
}

Instruction*
perform_change (Instruction* inst, guint* len)
{
  return perform_change_rec (inst, lrand48() % ((* len) - cmd_deletion_length), len);
}

void
test1 ()
{
  File* out_file;
  File* from_file;
  File* to_file;

  int ret;
  guint i, change, current_size = cmd_size;
  guint end_size = (cmd_changes * cmd_insertion_length) + cmd_size;
  Instruction* inst;
  struct stat sbuf;

  seed48 (cmd_seed);

  if ((ret = stat (cmd_data_source, & sbuf)))
    {
      perror (cmd_data_source);
      fail ();
    }

  if (! (data_source_handle = fopen (cmd_data_source, "r")))
    {
      perror (cmd_data_source);
      fail ();
    }

  data_source_length = sbuf.st_size;

  /* arbitrary checks */
  if (data_source_length < (64 * end_size))
    g_warning ("data source should be longer\n");

  if ((cmd_changes * cmd_deletion_length) > cmd_size)
    {
      g_warning ("no copies are expected\n");
      fail ();
    }

  from_file = empty_file ();
  to_file   = empty_file ();
  out_file  = empty_file ();

  inst = g_new0 (Instruction, 1);

  inst->offset = random_offset (cmd_size);
  inst->length = cmd_size;

  if (! write_file (from_file, inst))
    fail ();

  for (change = 0; change < cmd_changes; change += 1)
    inst = perform_change (inst, & current_size);

  if (! write_file (to_file, inst))
    fail ();

  /* warm caches */
  if (! run_command (from_file, to_file, out_file, FALSE) ||
      ! run_command (from_file, to_file, out_file, FALSE))
    fail ();

  for (i = 0; i < cmd_reps; i += 1)
    {
      if (! run_command (from_file, to_file, out_file, TRUE))
	fail ();
    }

  report ();
}

int
main (gint argc, gchar** argv)
{
  if (argc != 9)
    {
      g_print ("usage: %s PROGRAM PROFILE DATASOURCE FILESIZE REPETITIONS CHANGES INSERTIONLENGTH DELETIONLENGTH\n", argv[0]);
      exit (2);
    }

  cmd_delta_program = argv[1];
  cmd_delta_profile = argv[2];
  cmd_data_source = argv[3];

  if (! strtoui_checked (argv[4], & cmd_size, "Size"))
    return 2;

  if (! strtoui_checked (argv[5], & cmd_reps, "Repetitions"))
    return 2;

  if (! strtoui_checked (argv[6], & cmd_changes, "Changes"))
    return 2;

  if (! strtoui_checked (argv[7], & cmd_insertion_length, "Insertion length"))
    return 2;

  if (! strtoui_checked (argv[8], & cmd_deletion_length, "Deletion length"))
    return 2;

  /* body */

  system ("rm -rf " TEST_PREFIX "*");

  test1 ();

  /*system ("rm -rf " TEST_PREFIX "*");*/

  return 0;
}
