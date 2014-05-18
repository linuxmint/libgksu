/*
 * libgksuui -- Gtk+ widget for requesting passwords
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

#include <gtk/gtk.h>

#include "gksuui-dialog.h"

int 
main (gint argc, gchar **argv)
{
  GtkWidget *gksuui_dialog;
  GdkPixbuf *pixbuf;
  gint response;
  gchar *password;

  gtk_init (&argc, &argv);

  gksuui_dialog = gksuui_dialog_new (FALSE);
  gtk_window_set_title (GTK_WINDOW(gksuui_dialog), "My test!");
  gksuui_dialog_set_message (GKSUUI_DIALOG(gksuui_dialog), 
			   "<b>Gimme the damn password, luser!</b>");
  pixbuf = gdk_pixbuf_new_from_file ("/usr/share/pixmaps/apple-green.png",
				     NULL);
  gksuui_dialog_set_icon (GKSUUI_DIALOG(gksuui_dialog), pixbuf);

  gtk_widget_show_all (gksuui_dialog);

  response = gtk_dialog_run (GTK_DIALOG(gksuui_dialog));
  fprintf (stderr, "response ID: %d\n", response);

  password = gksuui_dialog_get_password (GKSUUI_DIALOG(gksuui_dialog));
  fprintf (stderr, "password: %s\n", password);

  gtk_widget_hide (gksuui_dialog);
  while (gtk_events_pending ())
    gtk_main_iteration_do (FALSE);

  return 0;
}
