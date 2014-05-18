/*
 * Gksu -- a library providing access to su functionality
 * Copyright (C) 2004 Gustavo Noronha Silva
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gksu.h"

gchar *
su_ask_pass (GksuContext *context, gchar *prompt,
	     gpointer data, GError **error)
{
  return getpass (prompt);
}

void
password_not_needed (GksuContext *context,
		     gpointer data)
{
  fprintf (stderr, "Will run %s as %s!\n", context->command, context->user);
}

int
main (int argc, char **argv)
{
  GksuContext *context;
  GError *error = NULL;
  gboolean try_su = TRUE;
  gboolean try_sudo = TRUE;
  gboolean try_run = TRUE;

  if (argc > 1)
    {
      try_su = try_sudo = try_run = FALSE;
      if (!strcmp (argv[1], "--su"))
	try_su = TRUE;
      else if (!strcmp (argv[1], "--sudo"))
	try_sudo = TRUE;
      else if (!strcmp (argv[1], "--run"))
	try_run = TRUE;
    }

  gtk_init (&argc, &argv);

  context = gksu_context_new ();

  context->debug = TRUE;
  context->command = g_strdup ("/usr/bin/xterm");

  if (try_su)
    {
      printf ("Testing gksu_su...\n");
      gksu_su ("/usr/bin/xterm", &error);
      if (error)
	fprintf (stderr, "gksu_su failed: %s\n", error->message);

      error = NULL;
      printf ("Testing gksu_su_full...\n");
      gksu_su_full (context,
		    su_ask_pass, NULL,
		    password_not_needed, NULL,
		    &error);
    }

  /* of course you need to set up /etc/sudoers for this to work */
  if (try_sudo)
    {
      printf ("Testing gksu_sudo...\n");
      error = NULL;
      gksu_sudo ("/usr/bin/xterm", &error);
      if (error)
	fprintf (stderr, "gksu_sudo failed: %s\n", error->message);

      printf ("Testing gksu_sudo_full...\n");
      error = NULL;
      gksu_sudo_full (context,
		      su_ask_pass, NULL,
		      password_not_needed, NULL,
		      &error);
      if (error)
	fprintf (stderr, "gksu_sudo_full failed: %s\n", error->message);
    }

  if (try_run)
    {
      printf ("Testing gksu_run...\n");
      error = NULL;
      gksu_run ("/usr/bin/xterm", &error);
      if (error)
	fprintf (stderr, "gksu_run failed: %s\n", error->message);

      printf ("Testing gksu_run_full...\n");
      error = NULL;
      gksu_run_full (context,
		      su_ask_pass, NULL,
		      password_not_needed, NULL,
		      &error);
      if (error)
	fprintf (stderr, "gksu_run_full failed: %s\n", error->message);
    }

  return 0;
}

