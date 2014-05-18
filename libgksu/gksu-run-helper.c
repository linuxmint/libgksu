/*
 * Gksu -- a library providing access to su functionality
 * Copyright (C) 2004 Gustavo Noronha Silva
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <glib.h>

#include "defines.h"

#include "../config.h"

void strip (gchar *string)
{
  if (string[strlen(string) - 1] == '\n')
    string[strlen(string) - 1] = '\0';
}

/**
 * clean_dir:
 * @dirname: the temporary directory created by gksu for xauth
 *
 * Removes the temporary directory created to hold the X authorization
 * file, and of course, the file itself.
 */
void
clean_dir (const gchar *dirname)
{
  gchar *xauthname;
  
  xauthname = g_strdup_printf ("%s/.Xauthority", dirname);
  unlink (xauthname);
  g_free (xauthname);

  xauthname = g_strdup_printf ("%s/.Xauthority.tmp", dirname);
  unlink (xauthname);
  g_free (xauthname);

  if (rmdir (dirname))
    fprintf (stderr, "ERROR: unable to remove directory %s: %s",
	     dirname, strerror (errno));
}

void read_gstring_from_stdin(GString *s)
{
  gchar buffer[255];
  char *readp;
  do
  {
     readp = fgets(buffer, sizeof(buffer), stdin);
     if(readp == NULL)
	return;
     strip (buffer);
     g_string_append(s, buffer);
  } while (sizeof(buffer)-1 == strlen(readp));
  return;
}

int
main (int argc, char **argv)
{
  gchar *command = NULL;

  gchar *xauth_dirtemplate = g_strdup ("/tmp/" PACKAGE_NAME "-XXXXXX");

  gchar *xauth_bin = NULL;

  gchar *xauth_dir = NULL;
  gchar *xauth_file = NULL;

  gchar *xauth_display = NULL;
  gchar *xauth_token = NULL;
  gchar *sn_id = NULL;

  gint return_code;

  if (argc < 2)
    {
      fprintf (stderr, "gksu: command missing");
      return 1;
    }

  xauth_dir = mkdtemp (xauth_dirtemplate);
  if (!xauth_dir)
    {
      fprintf (stderr, "gksu: failed creating xauth_dir\n");
      return 1;
    }

  fprintf (stderr, "gksu: waiting\n");

  xauth_file = g_strdup_printf ("%s/.Xauthority",
				xauth_dir);

  GString *s = g_string_sized_new(255);
  read_gstring_from_stdin(s);

  /* strlen ("gksu-run: ") == 10, see su.c */
  xauth_display = g_strdup_printf ("%s", s->str + 10);

  s = g_string_truncate(s,0);
  read_gstring_from_stdin(s);

  sn_id = g_strdup_printf ("%s", s->str + 10);
  setenv("DESKTOP_STARTUP_ID", sn_id, TRUE);

  /* cleanup the environment; some variables we bring from the user
   * environment make gconf-based applications misbehaving these days
   */
  unsetenv ("ORBIT_SOCKETDIR");
  unsetenv ("DBUS_SESSION_BUS_ADDRESS");

  s = g_string_truncate(s,0);
  read_gstring_from_stdin(s);

  xauth_token = g_strdup_printf ("%s", s->str + 10);

  /* a bit more security is always fine */
  {
    FILE *file;
    gchar *tmpfilename = g_strdup_printf ("%s.tmp",
					  xauth_file);

    file = fopen (tmpfilename, "w");
    if (!file)
      {
	fprintf (stderr, "gksu: error writing temporary auth file\n");
	return 1;
      }
    fwrite (xauth_token, sizeof (gchar), strlen (xauth_token), file);
    fclose (file);
    chmod (tmpfilename, S_IRUSR|S_IWUSR);
    
    setenv ("XAUTHORITY", xauth_file, TRUE);

    /* find out where the xauth binary is located */
    if (g_file_test ("/usr/bin/xauth", G_FILE_TEST_IS_EXECUTABLE))
      xauth_bin = "/usr/bin/xauth";
    else if (g_file_test ("/usr/X11R6/bin/xauth", G_FILE_TEST_IS_EXECUTABLE))
      xauth_bin = "/usr/X11R6/bin/xauth";
    else
      {
	fprintf (stderr,
		 _("Failed to obtain xauth key: xauth binary not found "
		   "at usual locations"));

	return 1;
      }
    command =
      g_strdup_printf ("%s add %s . \"`cat %s.tmp`\""
		       " > /dev/null 2>&1", xauth_bin,
		       xauth_display, xauth_file);

    system (command);

    return_code = system (argv[1]);
    
    clean_dir (xauth_dir);
    g_string_free(s, TRUE);
    if (WIFEXITED(return_code))
    {
      return WEXITSTATUS(return_code);
    } else if (WIFSIGNALED(return_code)) {
      return -1;
    }
  }

  return 0;
}
