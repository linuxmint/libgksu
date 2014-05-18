/*
 * Gksu -- a library providing access to su functionality
 * Copyright (C) 2004-2009 Gustavo Noronha Silva
 * Portions Copyright (C) 2009 VMware, Inc.
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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <pty.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <errno.h>

#include <glibtop.h>
#include <glibtop/procstate.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#define SN_API_NOT_YET_FROZEN
#include <libsn/sn.h>

#include <gtk/gtk.h>
#include <locale.h>

#include <gconf/gconf-client.h>
#include <gnome-keyring.h>

#include "defines.h"
#include "../config.h"

#include "libgksu.h"
#include "../libgksuui/gksuui-dialog.h"

static void
gksu_context_launch_complete (GksuContext *context);

static void
read_line (int fd, gchar *buffer, int n);

GType
gksu_error_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GKSU_ERROR_HELPER, "GKSU_ERROR_HELPER", "helper" },
      { GKSU_ERROR_NOCOMMAND, "GKSU_ERROR_NOCOMMAND", "nocommand" },
      { GKSU_ERROR_NOPASSWORD, "GKSU_ERROR_NOPASSWORD", "nopassword" },
      { GKSU_ERROR_FORK, "GKSU_ERROR_FORK", "fork" },
      { GKSU_ERROR_EXEC, "GKSU_ERROR_EXEC", "exec" },
      { GKSU_ERROR_PIPE, "GKSU_ERROR_PIPE", "pipe" },
      { GKSU_ERROR_PIPEREAD, "GKSU_ERROR_PIPEREAD", "piperead" },
      { GKSU_ERROR_WRONGPASS, "GKSU_ERROR_WRONGPASS", "wrongpass" },
      { GKSU_ERROR_CHILDFAILED, "GKSU_ERROR_CHILDFAILED", "childfailed" },
      { GKSU_ERROR_CANCELED, "GKSU_ERROR_CANCELED", "canceled" },
      { GKSU_ERROR_WRONGAUTOPASS, "GKSU_ERROR_WRONGAUTOPASS", "wrongautopass" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GksuError", values);
  }
  return etype;
}

static pid_t
test_lock(const char* fname)
{
   int FD = open(fname, 0);
   if(FD < 0) {
      if(errno == ENOENT) {
	 // File does not exist
	 return 0;
      } else {
	 perror("open");
	 return(-1);
      }
   }
   struct flock fl;
   fl.l_type = F_WRLCK;
   fl.l_whence = SEEK_SET;
   fl.l_start = 0;
   fl.l_len = 0;
   if (fcntl(FD, F_GETLK, &fl) < 0) {
      g_critical("fcntl error");
      close(FD);
      return(-1);
   }
   close(FD);
   // lock is available
   if(fl.l_type == F_UNLCK)
      return(0);
   // file is locked by another process
   return (fl.l_pid);
}

static int
get_lock(const char *File)
{
   int FD = open(File,O_RDWR | O_CREAT | O_TRUNC,0640);
   if (FD < 0)
   {
      // Read only .. cant have locking problems there.
      if (errno == EROFS)
      {
	 g_warning(_("Not using locking for read only lock file %s"),File);
	 return dup(0);       // Need something for the caller to close
      }

      // Feh.. We do this to distinguish the lock vs open case..
      errno = EPERM;
      return -1;
   }
   fcntl(FD,F_SETFD, FD_CLOEXEC);

   // Aquire a write lock
   struct flock fl;
   fl.l_type = F_WRLCK;
   fl.l_whence = SEEK_SET;
   fl.l_start = 0;
   fl.l_len = 0;
   if (fcntl(FD,F_SETLK,&fl) == -1)
   {
      if (errno == ENOLCK)
      {
	 g_warning(_("Not using locking for nfs mounted lock file %s"), File);
	 unlink(File);
	 close(FD);
	 return dup(0);       // Need something for the caller to close
      }

      int Tmp = errno;
      close(FD);
      errno = Tmp;
      return -1;
   }

   return FD;
}

/*
 * code 'stolen' from gnome-session's logout.c
 *
 * Written by Owen Taylor <otaylor@redhat.com>
 * Copyright (C) Red Hat
 */
typedef struct {
  GdkScreen    *screen;
  int           monitor;
  GdkRectangle  area;
  int           rowstride;
  GdkWindow    *root_window;
  GdkWindow    *draw_window;
  GdkPixbuf    *start_pb, *end_pb, *frame;
  guchar       *start_p, *end_p, *frame_p;
  GTimeVal      start_time;
  GdkGC        *gc;
} FadeoutData;

FadeoutData *fade_data = NULL;
static GList *fade_data_l = NULL;
static GList *fadeout_windows = NULL;

#define FADE_DURATION 500.0

int
gsm_screen_get_width (GdkScreen *screen,
		      int        monitor)
{
	GdkRectangle geometry;

	gdk_screen_get_monitor_geometry (screen, monitor, &geometry);

	return geometry.width;
}

int
gsm_screen_get_height (GdkScreen *screen,
		       int        monitor)
{
	GdkRectangle geometry;

	gdk_screen_get_monitor_geometry (screen, monitor, &geometry);

	return geometry.height;
}

int
gsm_screen_get_x (GdkScreen *screen,
		  int        monitor)
{
	GdkRectangle geometry;

	gdk_screen_get_monitor_geometry (screen, monitor, &geometry);

	return geometry.x;
}

int
gsm_screen_get_y (GdkScreen *screen,
		  int        monitor)
{
	GdkRectangle geometry;

	gdk_screen_get_monitor_geometry (screen, monitor, &geometry);

	return geometry.y;
}

typedef void (*GsmScreenForeachFunc) (GdkScreen *screen,
                                     int        monitor);

void
gsm_foreach_screen (GsmScreenForeachFunc callback)
{
       GdkDisplay *display;
       int         n_screens, i;

       display = gdk_display_get_default ();

       n_screens = gdk_display_get_n_screens (display);
       for (i = 0; i < n_screens; i++) {
               GdkScreen *screen;
               int        n_monitors, j;

               screen = gdk_display_get_screen (display, i);

               n_monitors = gdk_screen_get_n_monitors (screen);
               for (j = 0; j < n_monitors; j++)
                       callback (screen, j);
       }
}

static void
get_current_frame (FadeoutData *fadeout,
		   double    sat)
{
  guchar *sp, *ep, *fp;
  int i, j, width, offset;

  width = fadeout->area.width * 3;
  offset = 0;

  for (i = 0; i < fadeout->area.height; i++)
    {
      sp = fadeout->start_p + offset;
      ep = fadeout->end_p   + offset;
      fp = fadeout->frame_p + offset;

      for (j = 0; j < width; j += 3)
	{
	  guchar r = abs (*(sp++) - ep[0]);
	  guchar g = abs (*(sp++) - ep[1]);
	  guchar b = abs (*(sp++) - ep[2]);

	  *(fp++) = *(ep++) + r * sat;
	  *(fp++) = *(ep++) + g * sat;
	  *(fp++) = *(ep++) + b * sat;
	}

      offset += fadeout->rowstride;
    }
}

static void
darken_pixbuf (GdkPixbuf *pb)
{
  int width, height, rowstride;
  int i, j;
  guchar *p, *pixels;

  width     = gdk_pixbuf_get_width (pb) * 3;
  height    = gdk_pixbuf_get_height (pb);
  rowstride = gdk_pixbuf_get_rowstride (pb);
  pixels    = gdk_pixbuf_get_pixels (pb);

  for (i = 0; i < height; i++)
    {
      p = pixels + (i * rowstride);
      for (j = 0; j < width; j++)
	p [j] >>= 1;
    }
}

static gboolean
fadeout_callback (FadeoutData *fadeout)
{
  GTimeVal current_time;
  double elapsed, percent;

  g_get_current_time (&current_time);
  elapsed = ((((double)current_time.tv_sec - fadeout->start_time.tv_sec) * G_USEC_PER_SEC +
	      (current_time.tv_usec - fadeout->start_time.tv_usec))) / 1000.0;

  if (elapsed < 0)
    {
      g_warning ("System clock seemed to go backwards?");
      elapsed = G_MAXDOUBLE;
    }

  if (elapsed > FADE_DURATION)
    {
      gdk_draw_pixbuf (fadeout->draw_window,
		       fadeout->gc,
		       fadeout->end_pb,
		       0, 0,
		       0, 0,
		       fadeout->area.width,
		       fadeout->area.height,
		       GDK_RGB_DITHER_NONE,
		       0, 0);

      return FALSE;
    }

  percent = elapsed / FADE_DURATION;

  get_current_frame (fadeout, 1.0 - percent);
  gdk_draw_pixbuf (fadeout->draw_window,
		   fadeout->gc,
		   fadeout->frame,
		   0, 0,
		   0, 0,
		   fadeout->area.width,
		   fadeout->area.height,
		   GDK_RGB_DITHER_NONE,
		   0, 0);

  gdk_flush ();

  return TRUE;
}

static void
hide_fadeout_windows (void)
{
  GList *l;

  for (l = fadeout_windows; l; l = l->next)
    {
      gdk_window_hide (GDK_WINDOW (l->data));
      g_object_unref (l->data);
    }

  g_list_free (fadeout_windows);
  fadeout_windows = NULL;
}

static gboolean
fadein_callback (FadeoutData *fadeout)
{
  GTimeVal current_time;
  double elapsed, percent;

  g_get_current_time (&current_time);
  elapsed = ((((double)current_time.tv_sec - fadeout->start_time.tv_sec) * G_USEC_PER_SEC +
	      (current_time.tv_usec - fadeout->start_time.tv_usec))) / 1000.0;

  if (elapsed < 0)
    {
      g_warning ("System clock seemed to go backwards?");
      elapsed = G_MAXDOUBLE;
    }

  if (elapsed > FADE_DURATION)
    {
      gdk_draw_pixbuf (fadeout->draw_window,
		       fadeout->gc,
		       fadeout->end_pb,
		       0, 0,
		       0, 0,
		       fadeout->area.width,
		       fadeout->area.height,
		       GDK_RGB_DITHER_NONE,
		       0, 0);

      g_object_unref (fadeout->gc);
      g_object_unref (fadeout->start_pb);
      g_object_unref (fadeout->end_pb);
      g_object_unref (fadeout->frame);

      g_free (fadeout);

      hide_fadeout_windows ();

      return FALSE;
    }

  percent = elapsed / FADE_DURATION;

  get_current_frame (fadeout, percent);
  gdk_draw_pixbuf (fadeout->draw_window,
		   fadeout->gc,
		   fadeout->frame,
		   0, 0,
		   0, 0,
		   fadeout->area.width,
		   fadeout->area.height,
		   GDK_RGB_DITHER_NONE,
		   0, 0);

  gdk_flush ();

  return TRUE;
}

static void
fadeout_screen (GdkScreen *screen,
		int        monitor)
{
  GdkWindowAttr attr;
  int attr_mask;
  GdkGCValues values;
  FadeoutData *fadeout;

  fadeout = g_new (FadeoutData, 1);

  fadeout->screen = screen;
  fadeout->monitor = monitor;

  fadeout->area.x = gsm_screen_get_x (screen, monitor);
  fadeout->area.y = gsm_screen_get_y (screen, monitor);
  fadeout->area.width = gsm_screen_get_width (screen, monitor);
  fadeout->area.height = gsm_screen_get_height (screen, monitor);

  fadeout->root_window = gdk_screen_get_root_window (screen);
  attr.window_type = GDK_WINDOW_CHILD;
  attr.x = fadeout->area.x;
  attr.y = fadeout->area.y;
  attr.width = fadeout->area.width;
  attr.height = fadeout->area.height;
  attr.wclass = GDK_INPUT_OUTPUT;
  attr.visual = gdk_screen_get_system_visual (fadeout->screen);
  attr.colormap = gdk_screen_get_default_colormap (fadeout->screen);
  attr.override_redirect = TRUE;
  attr_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP | GDK_WA_NOREDIR;

  fadeout->draw_window = gdk_window_new (fadeout->root_window, &attr, attr_mask);
  fadeout_windows = g_list_prepend (fadeout_windows, fadeout->draw_window);

  fadeout->start_pb = gdk_pixbuf_get_from_drawable (NULL,
						    fadeout->root_window,
						    NULL,
						    fadeout->area.x,
						    fadeout->area.y,
						    0, 0,
						    fadeout->area.width,
						    fadeout->area.height);

  fadeout->end_pb = gdk_pixbuf_copy (fadeout->start_pb);
  darken_pixbuf (fadeout->end_pb);

  fadeout->frame = gdk_pixbuf_copy (fadeout->start_pb);
  fadeout->rowstride = gdk_pixbuf_get_rowstride (fadeout->start_pb);

  fadeout->start_p = gdk_pixbuf_get_pixels (fadeout->start_pb);
  fadeout->end_p   = gdk_pixbuf_get_pixels (fadeout->end_pb);
  fadeout->frame_p = gdk_pixbuf_get_pixels (fadeout->frame);

  values.subwindow_mode = GDK_INCLUDE_INFERIORS;

  fadeout->gc = gdk_gc_new_with_values (fadeout->root_window, &values, GDK_GC_SUBWINDOW);

  gdk_window_set_back_pixmap (fadeout->draw_window, NULL, FALSE);
  gdk_window_show (fadeout->draw_window);
  gdk_draw_pixbuf (fadeout->draw_window,
		   fadeout->gc,
		   fadeout->frame,
		   0, 0,
		   0, 0,
		   fadeout->area.width,
		   fadeout->area.height,
		   GDK_RGB_DITHER_NONE,
		   0, 0);

  g_get_current_time (&fadeout->start_time);
  g_idle_add ((GSourceFunc) fadeout_callback, fadeout);

  fade_data_l = g_list_prepend (fade_data, fadeout);
}

static void
fadein (void)
{
  GList *l;
  GList *next;

  /* set start_time for all screens */
  for (l = fade_data_l; l; l = l->next)
    {
      FadeoutData *fd;
      fd = (FadeoutData*)l->data;
      g_get_current_time (&fd->start_time);
    }

  /* iterate through all screens and call the fadein_callback 
   * until all of them return FALSE */
  next = fade_data_l;
  while (next != NULL)
    {
      l = next;
      next = l->next;

      /* remove from list when finished fading */
      if (fadein_callback ((FadeoutData*)l->data) == FALSE)
	{
	  fade_data_l = g_list_remove(fade_data, l->data);
	}

      /* this wrapping around needs to be delayed because fade_data_l could
       * have changed above */
      if (next == NULL)
	{
	  next = fade_data_l;
	}
    }

  /* free the list. The FadeoutData structs are being free'd from
   * within the fadein_callback's */
  g_list_free (fade_data_l);
  fade_data_l = NULL;
} 

/* End of 'stolen' code */

#define GRAB_TRIES	16
#define GRAB_WAIT	250 /* milliseconds */

typedef enum
  {
    FAILED_GRAB_MOUSE,
    FAILED_GRAB_KEYBOARD
  } FailedGrabWhat;

void
report_failed_grab (FailedGrabWhat what)
{
  GtkWidget *dialog;

  dialog = g_object_new (GTK_TYPE_MESSAGE_DIALOG,
			 "message-type", GTK_MESSAGE_WARNING,
			 "buttons", GTK_BUTTONS_CLOSE,
			 NULL);

  switch (what)
    {
    case FAILED_GRAB_MOUSE:
      gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG(dialog),
				     _("<b><big>Could not grab your mouse.</big></b>"
				       "\n\n"
				       "A malicious client may be eavesdropping "
				       "on your session or you may have just clicked "
				       "a menu or some application just decided to get "
				       "focus."
				       "\n\n"
				       "Try again."));

      break;
    case FAILED_GRAB_KEYBOARD:
      gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG(dialog),
				     _("<b><big>Could not grab your keyboard.</big></b>"
				       "\n\n"
				       "A malicious client may be eavesdropping "
				       "on your session or you may have just clicked "
				       "a menu or some application just decided to get "
				       "focus."
				       "\n\n"
				       "Try again."));
      break;
    }

  gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);
  gtk_dialog_run (GTK_DIALOG(dialog));
  gtk_widget_destroy (dialog);

  while (gtk_events_pending ())
    gtk_main_iteration ();

}

int
grab_keyboard_and_mouse (GtkWidget *dialog)
{
  GdkGrabStatus status;
  gint grab_tries = 0;
  gint lock = -1;
  pid_t pid;

  gchar *fname = g_strdup (getenv ("GKSU_LOCK_FILE"));
  if (fname == NULL)
    fname = g_strdup_printf ("%s/.gksu.lock", getenv ("HOME"));

  pid = test_lock (fname);

  if (pid != 0)
    {
      g_warning ("Lock taken by pid: %i. Exiting.", pid);
      exit (0);
    }

  lock = get_lock(fname);
  if( lock < 0)
    g_warning ("Unable to create lock file.");
  g_free (fname);

  gdk_threads_enter ();
  gsm_foreach_screen (fadeout_screen);
  gtk_widget_show_all (dialog);

  /* reset cursor */
  gdk_window_set_cursor(dialog->window, gdk_cursor_new(GDK_LEFT_PTR));

  for(;;)
    {
      status = gdk_pointer_grab ((GTK_WIDGET(dialog))->window, TRUE, 0, NULL,
				 NULL, GDK_CURRENT_TIME);
      if (status == GDK_GRAB_SUCCESS)
	break;
      usleep (GRAB_WAIT * 1000);
      if (++grab_tries > GRAB_TRIES)
	{
	  gtk_widget_hide (dialog);
	  fadein();
	  report_failed_grab (FAILED_GRAB_MOUSE);
	  exit (1);
	  break;
	}
    }

  for(;;)
    {
      status = gdk_keyboard_grab ((GTK_WIDGET(dialog))->window,
				  FALSE, GDK_CURRENT_TIME);
      if (status == GDK_GRAB_SUCCESS)
	break;

      usleep(GRAB_WAIT * 1000);

      if (++grab_tries > GRAB_TRIES)
	{
	  gtk_widget_hide (dialog);
	  fadein();
	  report_failed_grab (FAILED_GRAB_KEYBOARD);
	  exit (1);
	  break;
	}
    }

  /* we "raise" the window because there is a race here for
   * focus-follow-mouse and auto-raise WMs that may put the window
   * in the background and confuse users
   */
  gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);

  while (gtk_events_pending ())
    gtk_main_iteration ();

  return lock;
}

void
ungrab_keyboard_and_mouse (int lock)
{
  /* Ungrab */
  gdk_pointer_ungrab(GDK_CURRENT_TIME);
  gdk_keyboard_ungrab(GDK_CURRENT_TIME);
  gdk_flush();

  fadein();
  gdk_threads_leave();

  close(lock);
}

static gchar*
get_gnome_keyring_password (GksuContext *context)
{
  GnomeKeyringAttributeList *attributes;
  GnomeKeyringAttribute attribute;
  GnomeKeyringResult result;
  GList *list;

  attributes = gnome_keyring_attribute_list_new ();

  attribute.name = g_strdup ("user");
  attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
  attribute.value.string = g_strdup (gksu_context_get_user (context));
  g_array_append_val (attributes, attribute);

  attribute.name = g_strdup ("type");
  attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
  attribute.value.string = g_strdup ("local");
  g_array_append_val (attributes, attribute);

  attribute.name = g_strdup ("creator");
  attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
  attribute.value.string = g_strdup ("gksu");
  g_array_append_val (attributes, attribute);

  list = g_list_alloc();

  result = gnome_keyring_find_items_sync (GNOME_KEYRING_ITEM_GENERIC_SECRET,
					  attributes,
					  &list);
  gnome_keyring_attribute_list_free (attributes);
  if (
      (result == GNOME_KEYRING_RESULT_OK) &&
      (g_list_length(list) >= 1)
      )
    {
      GnomeKeyringFound *found = list->data;
      gint password_length = strlen (found->secret);
      gchar *password;

      password = g_locale_from_utf8 (found->secret,
				     password_length,
				     NULL, NULL, NULL);
      password_length = strlen (password);

      if (password[password_length-1] == '\n')
	password[password_length-1] = '\0';
      return password;
    }

  return NULL;
}

static void
keyring_create_item_cb (GnomeKeyringResult result,
                        guint32 id, gpointer keyring_loop)
{
  g_main_loop_quit (keyring_loop);
}

static void
set_gnome_keyring_password (GksuContext *context, gchar *password)
{
  GConfClient *gconf_client;
  gboolean save_to_keyring;

  gconf_client = context->gconf_client;
  save_to_keyring = gconf_client_get_bool (gconf_client, BASE_PATH"save-to-keyring", NULL);

  if (password && save_to_keyring)
    {
      static GMainLoop *keyring_loop = NULL;
      GnomeKeyringAttributeList *attributes;
      GnomeKeyringAttribute attribute;
      GnomeKeyringResult result;

      gchar *keyring_name;
      gchar *key_name;

      attributes = gnome_keyring_attribute_list_new ();

      attribute.name = g_strdup ("user");
      attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
      attribute.value.string = g_strdup (gksu_context_get_user (context));
      g_array_append_val (attributes, attribute);

      attribute.name = g_strdup ("type");
      attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
      attribute.value.string = g_strdup ("local");
      g_array_append_val (attributes, attribute);

      attribute.name = g_strdup ("creator");
      attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
      attribute.value.string = g_strdup ("gksu");
      g_array_append_val (attributes, attribute);

      key_name = g_strdup_printf ("Local password for user %s",
				  gksu_context_get_user (context));

      keyring_loop = g_main_loop_new (NULL, FALSE);

      keyring_name = gconf_client_get_string (gconf_client, BASE_PATH"save-keyring", NULL);
      if (keyring_name == NULL)
	keyring_name = g_strdup ("session");

      /* make sure the keyring exists; if an error occurs, use
         the session keyring */
      result = gnome_keyring_create_sync(keyring_name, NULL);
      if ((result != GNOME_KEYRING_RESULT_OK) &&
	  (result != GNOME_KEYRING_RESULT_ALREADY_EXISTS))
	keyring_name = g_strdup ("session");

      gnome_keyring_item_create (keyring_name,
				 GNOME_KEYRING_ITEM_GENERIC_SECRET,
				 key_name,
				 attributes,
				 password,
				 TRUE,
				 keyring_create_item_cb,
				 keyring_loop, NULL);
      gnome_keyring_attribute_list_free (attributes);
      g_free (keyring_name);
      g_main_loop_run (keyring_loop);
    }
}

static void
unset_gnome_keyring_password (GksuContext *context)
{
  GConfClient *gconf_client;
  gboolean save_to_keyring;

  GnomeKeyringAttributeList *attributes;
  GnomeKeyringAttribute attribute;
  GnomeKeyringResult result;
  GList *list;

  gconf_client = context->gconf_client;
  save_to_keyring = gconf_client_get_bool (gconf_client, BASE_PATH"save-to-keyring", NULL);

  if (save_to_keyring)
    {
      attributes = gnome_keyring_attribute_list_new ();

      attribute.name = g_strdup ("user");
      attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
      attribute.value.string = g_strdup (gksu_context_get_user (context));
      g_array_append_val (attributes, attribute);

      attribute.name = g_strdup ("type");
      attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
      attribute.value.string = g_strdup ("local");
      g_array_append_val (attributes, attribute);

      attribute.name = g_strdup ("creator");
      attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
      attribute.value.string = g_strdup ("gksu");
      g_array_append_val (attributes, attribute);

      list = g_list_alloc();

      result = gnome_keyring_find_items_sync (GNOME_KEYRING_ITEM_GENERIC_SECRET,
					      attributes,
					      &list);
      gnome_keyring_attribute_list_free (attributes);
      if (
	  (result == GNOME_KEYRING_RESULT_OK) &&
	  (g_list_length(list) == 1)
	  )
	{
	  GnomeKeyringFound *found = list->data;

	  gnome_keyring_item_delete_sync (found->keyring,
					  found->item_id);
	}
    }
}

void
get_configuration_options (GksuContext *context)
{
  GConfClient *gconf_client = context->gconf_client;
  gboolean force_grab;

  context->grab = !gconf_client_get_bool (gconf_client, BASE_PATH "disable-grab",
					  NULL);
  force_grab = gconf_client_get_bool (gconf_client, BASE_PATH "force-grab",
				      NULL);
  if (force_grab)
    context->grab = TRUE;

  context->sudo_mode = gconf_client_get_bool (gconf_client, BASE_PATH "sudo-mode",
					      NULL);
}

/**
 * su_ask_password:
 * @context: a #GksuContext
 * @prompt: the prompt that should be used instead of "Password: "
 * @data: data that is passed by gksu_*_full
 * @error: a pointer to pointer #GError that will be filled with
 * data if an error happens.
 *
 * This is a convenience function to create a #GksuuiDialog and
 * request the password.
 *
 * Returns: a newly allocated gchar containing the password
 * or NULL if an error happens or the user cancels the action
 */
static gchar*
su_ask_password (GksuContext *context, gchar *prompt,
		 gpointer data, GError **error)
{
  GtkWidget *dialog = NULL;
  gchar *msg;
  gchar *password = NULL, *tmp = NULL;
  int retvalue = 0;
  int lock = 0;
  GQuark gksu_quark;

  gksu_quark = g_quark_from_string (PACKAGE);

  if (context->grab)
    dialog = g_object_new (GKSUUI_TYPE_DIALOG,
			   "type", GTK_WINDOW_POPUP,
			   "sudo-mode", context->sudo_mode,
			   NULL);
  else
    dialog = gksuui_dialog_new (context->sudo_mode);

  if (prompt)
    gksuui_dialog_set_prompt (GKSUUI_DIALOG(dialog), _(prompt));

  if (context->message)
    gksuui_dialog_set_message (GKSUUI_DIALOG(dialog), context->message);
  else
    {
      gchar *command = NULL;
      if (context->description)
	command = context->description;
      else
	command = context->command;

      if (context->sudo_mode)
	{
	  if (!strcmp(context->user, "root"))
	    msg = g_strdup_printf (_("<b><big>Enter your password to perform"
				     " administrative tasks</big></b>\n\n"
				     "The application '%s' lets you "
				     "modify essential parts of your "
				     "system."),
				   command);
	  else
	    msg = g_strdup_printf (_("<b><big>Enter your password to run "
				     "the application '%s' as user %s"
				     "</big></b>"),
				   command, context->user);
	}
      else
	{
        if (strcmp(gksu_context_get_user (context), "root") == 0)
          msg = g_strdup_printf (_("<b><big>Enter the administrative password"
                                   "</big></b>\n\n"
                                   "The application '%s' lets you "
                                   "modify essential parts of your "
                                   "system."),
				   command);
        else
          msg = g_strdup_printf (_("<b><big>Enter the password of %s to run "
                                   "the application '%s'"
                                   "</big></b>"),
				   context->user, command);
      }

      gksuui_dialog_set_message (GKSUUI_DIALOG(dialog), msg);
      g_free (msg);
    }

  if (context->alert)
    gksuui_dialog_set_alert (GKSUUI_DIALOG(dialog), context->alert);

  if (context->grab)
    lock = grab_keyboard_and_mouse (dialog);
  retvalue = gtk_dialog_run (GTK_DIALOG(dialog));
  gtk_widget_hide (dialog);
  if (context->grab)
    ungrab_keyboard_and_mouse (lock);

  while (gtk_events_pending ())
    gtk_main_iteration ();

  if (retvalue != GTK_RESPONSE_OK)
    {
      switch (retvalue)
	{
	case GTK_RESPONSE_CANCEL:
	case GTK_RESPONSE_DELETE_EVENT:
	  g_set_error (error, gksu_quark,
		       GKSU_ERROR_CANCELED,
		       _("Password prompt canceled."));
	  if (context->sn_context)
	    gksu_context_launch_complete (context);
	}

      gtk_widget_destroy (dialog);
      while (gtk_events_pending ())
	gtk_main_iteration ();

      return NULL;
    }

  tmp = gksuui_dialog_get_password (GKSUUI_DIALOG(dialog));
  password = g_locale_from_utf8 (tmp, strlen (tmp), NULL, NULL, NULL);
  g_free (tmp);

  gtk_widget_destroy (dialog);
  while (gtk_events_pending ())
    gtk_main_iteration ();

  return password;
}

static void
cb_toggled_cb (GtkWidget *button, gpointer data)
{
  GConfClient *gconf_client;
  gchar *key;
  gboolean toggled;
  gchar *key_name;

  g_return_if_fail (data != NULL);

  key_name = (gchar*)data;

  gconf_client = gconf_client_get_default ();
  toggled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(button));

  key = g_strdup_printf (BASE_PATH "%s", key_name);

  if (!strcmp (key_name, "display-no-pass-info"))
    {
      /* the meaning of the key is the exact opposite of the meaning
	 of the answer - when the check button is checked the key must
	 be off
      */
      gconf_client_set_bool (gconf_client, key, !toggled, NULL);
    }
  else
    gconf_client_set_bool (gconf_client, key, toggled, NULL);

  g_object_unref (gconf_client);

  g_free (key);
}

void
no_pass (GksuContext *context, gpointer data)
{
  GtkWidget *dialog;
  GtkWidget *alignment;
  GtkWidget *check_button;

  gchar *command = NULL;

  if (context->description)
    command = context->description;
  else
    command = context->command;

  dialog = gtk_message_dialog_new_with_markup (NULL, 0,
					       GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
					       _("<b><big>Granted permissions without asking "
						 "for password</big></b>"
						 "\n\n"
						 "The '%s' program was started with "
						 "the privileges of the %s user without "
						 "the need to ask for a password, due to "
						 "your system's authentication mechanism "
						 "setup."
						 "\n\n"
						 "It is possible that you are being allowed "
						 "to run specific programs as user %s "
						 "without the need for a password, or that "
						 "the password is cached."
						 "\n\n"
						 "This is not a problem report; it's "
						 "simply a notification to make sure "
						 "you are aware of this."),
					       command,
					       context->user,
					       context->user);

  alignment = gtk_alignment_new (0.5, 0.5, 0.6, 1);
  gtk_box_pack_start (GTK_BOX(GTK_DIALOG(dialog)->vbox), alignment, TRUE, TRUE, 2);

  check_button = gtk_check_button_new_with_mnemonic (_("Do _not display this message again"));
  g_signal_connect (G_OBJECT(check_button), "toggled",
		    G_CALLBACK(cb_toggled_cb), "display-no-pass-info");
  gtk_container_add (GTK_CONTAINER(alignment), check_button);

  gtk_widget_show_all (dialog);
  gtk_dialog_run (GTK_DIALOG(dialog));
  gtk_widget_destroy (GTK_WIDGET(dialog));

  while (gtk_events_pending ())
    gtk_main_iteration ();
}

static void
gksu_prompt_grab (GksuContext *context)
{
  GtkWidget *d;

  d = gtk_message_dialog_new_with_markup (NULL, 0, GTK_MESSAGE_QUESTION,
					  GTK_BUTTONS_YES_NO,
					  _("<b>Would you like your screen to be \"grabbed\"\n"
					    "while you enter the password?</b>"
					    "\n\n"
					    "This means all applications will be paused to avoid\n"
					    "the eavesdropping of your password by a a malicious\n"
					    "application while you type it."));

  if (gtk_dialog_run (GTK_DIALOG(d)) == GTK_RESPONSE_NO)
    context->grab = FALSE;
  else
    context->grab = TRUE;

  gtk_widget_destroy (d);
}

static void
nullify_password (gchar *pass)
{
  if (pass)
    {
      memset(pass, 0, strlen(pass));
      g_free (pass);
    }
  pass = NULL;
}

static gchar *
get_process_name (pid_t pid)
{
  static gboolean init;
  glibtop_proc_state buf;

  if (!init) {
    glibtop_init();
    init = TRUE;
  }

  glibtop_get_proc_state (&buf, pid);
  return strdup(buf.cmd);
}

static gchar *
get_xauth_token (GksuContext *context, gchar *display)
{
  gchar *xauth_bin = NULL;
  FILE *xauth_output;
  gchar *tmp = NULL;
  gchar *xauth = g_new0 (gchar, 256);

  /* find out where the xauth binary is located */
  if (g_file_test ("/usr/bin/xauth", G_FILE_TEST_IS_EXECUTABLE))
    xauth_bin = "/usr/bin/xauth";
  else if (g_file_test ("/usr/X11R6/bin/xauth", G_FILE_TEST_IS_EXECUTABLE))
    xauth_bin = "/usr/X11R6/bin/xauth";
  else
    {
      fprintf (stderr,
	       "Failed to obtain xauth key: xauth binary not found "
	       "at usual locations");

      return NULL;
    }

  /* get the authorization token */
  tmp = g_strdup_printf ("%s list %s | "
			 "head -1 | awk '{ print $3 }'",
			 xauth_bin,
			 display);
  if ((xauth_output = popen (tmp, "r")) == NULL)
    {
      fprintf (stderr,
	       "Failed to obtain xauth key: %s",
	       strerror(errno));
      return NULL;
    }
  fread (xauth, sizeof(char), 255, xauth_output);
  pclose (xauth_output);
  g_free (tmp);

  if (context->debug)
    {
      fprintf(stderr,
	      "xauth: -%s-\n"
	      "display: -%s-\n",
	      xauth, display);
    }

  return xauth;
}

/**
 * prepare_xauth:
 *
 * Sets up the variables with values for the $DISPLAY
 * environment variable and xauth-related stuff. Also
 * creates a temporary directory to hold a .Xauthority
 *
 * Returns: TRUE if it suceeds, FALSE if it fails.
 */
static int
prepare_xauth (GksuContext *context)
{
  gchar *display = NULL;
  gchar *xauth = NULL;

  display = g_strdup (getenv ("DISPLAY"));
  xauth = get_xauth_token (context, display);
  if (xauth == NULL)
    {
      g_free (display);
      return FALSE;
    }

  /* If xauth is the empty string, then try striping the
   * hostname part of the DISPLAY string for getting the
   * auth token; this is needed for ssh-forwarded usage
   */
  if (!strcmp ("", xauth))
    {
      gchar *cut_display = NULL;

      g_free (xauth);
      cut_display = g_strdup (g_strrstr (display, ":"));
      xauth = get_xauth_token (context, cut_display);

      g_free (display);
      display = cut_display;
    }

  context->xauth = xauth;
  context->display = display;

  if (context->debug)
    {
      fprintf(stderr,
	      "final xauth: -%s-\n"
	      "final display: -%s-\n",
	      context->xauth, context->display);
    }

  return TRUE;
}

/* Write all of buf, even if write(2) is interrupted. */
static ssize_t
full_write (int d, const char *buf, size_t nbytes)
{
  ssize_t r, w = 0;

  /* Loop until nbytes of buf have been written. */
  while (w < nbytes) {
    /* Keep trying until write succeeds without interruption. */
    do {
      r = write(d, buf + w, nbytes - w);
    } while (r < 0 && errno == EINTR);

    if (r < 0) {
      return -1;
    }

    w += r;
  }

  return w;
}

static gboolean
copy (const char *fn, const char *dir)
{
  int in, out;
  int r;
  char *newfn;
  char buf[BUFSIZ] = "";

  newfn = g_strdup_printf("%s/.Xauthority", dir);

  out = open(newfn, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (out == -1)
    {
      if (errno == EEXIST)
	fprintf (stderr,
		 "Impossible to create the .Xauthority file: a file "
		 "already exists. This might be a security issue; "
		 "please investigate.");
      else
	fprintf (stderr,
		 "Error copying '%s' to '%s': %s",
		 fn, dir, strerror(errno));

      return FALSE;
    }

  in = open(fn, O_RDONLY);
  if (in == -1)
    {
      fprintf (stderr,
	       "Error copying '%s' to '%s': %s",
	       fn, dir, strerror(errno));
      return FALSE;
    }

  while ((r = read(in, buf, BUFSIZ)) > 0)
    {
      if (full_write(out, buf, r) == -1)
	{
	  fprintf (stderr,
		   "Error copying '%s' to '%s': %s",
		   fn, dir, strerror(errno));
	  return FALSE;
	}
    }

  if (r == -1)
    {
      fprintf (stderr,
	       "Error copying '%s' to '%s': %s",
	       fn, dir, strerror(errno));
      return FALSE;
    }

  return TRUE;
}

static gboolean
sudo_prepare_xauth (GksuContext *context)
{
  gchar template[] = "/tmp/" PACKAGE "-XXXXXX";
  gboolean error_copying = FALSE;
  gchar *xauth = NULL;

  context->dir = g_strdup (mkdtemp(template));
  if (!context->dir)
    {
      fprintf (stderr, "%s", strerror(errno));
      return FALSE;
    }

  xauth = g_strdup(g_getenv ("XAUTHORITY"));
  if (xauth == NULL)
    xauth = g_strdup_printf ("%s/.Xauthority", g_get_home_dir());

  error_copying = !copy (xauth, context->dir);
  g_free (xauth);

  if (error_copying)
    return FALSE;

  return TRUE;
}

static void
sudo_reset_xauth (GksuContext *context, gchar *xauth,
		  gchar *xauth_env)
{
  /* reset the env var as it was before or clean it  */
  if (xauth_env)
    setenv ("XAUTHORITY", xauth_env, TRUE);
  else
    unsetenv ("XAUTHORITY");

  if (context->debug)
    fprintf (stderr, "xauth: %s\nxauth_env: %s\ndir: %s\n",
	     xauth, xauth_env, context->dir);

  unlink (xauth);
  rmdir (context->dir);

  g_free (xauth);
}

static void
startup_notification_initialize (GksuContext *context)
{
  SnDisplay *sn_display;
  sn_display = sn_display_new (GDK_DISPLAY_XDISPLAY(gdk_display_get_default()),
			       NULL, NULL);
  context->sn_context = sn_launcher_context_new (sn_display, gdk_screen_get_number (gdk_display_get_default_screen (gdk_display_get_default ())));
  sn_launcher_context_set_description (context->sn_context, _("Granting Rights"));
  sn_launcher_context_set_name (context->sn_context, g_get_prgname ());
}

/**
 * gksu_context_new
 *
 * This function should be used when creating a new #GksuContext to
 * pass to gksu_su_full or gksu_sudo_full. The #GksuContext must be
 * freed with gksu_context_free.
 *
 * Returns: a newly allocated #GksuContext
 */
GksuContext*
gksu_context_new ()
{
  GksuContext *context;

  context = g_new (GksuContext, 1);

  context->xauth = NULL;
  context->dir = NULL;
  context->display = NULL;

  context->gconf_client = gconf_client_get_default ();

  context->sudo_mode = FALSE;

  context->user = g_strdup ("root");
  context->command = NULL;

  context->login_shell = FALSE;
  context->keep_env = FALSE;

  context->description = NULL;
  context->message = NULL;
  context->alert = NULL;
  context->grab = TRUE;
  context->always_ask_password = FALSE;

  context->debug = FALSE;

  context->sn_context = NULL;
  context->sn_id = NULL;
  
  context->ref_count = 1;

  get_configuration_options (context);
  startup_notification_initialize (context);

  return context;
}

/**
 * gksu_context_set_user:
 * @context: the #GksuContext you want to modify
 * @username: the target username
 *
 * Sets up what user the command will be run as. The default
 * is root, but you can run the command as any user.
 *
 */
void
gksu_context_set_user (GksuContext *context, gchar *username)
{
  g_assert (username != NULL);

  if (context->user)
    g_free (context->user);
  context->user = g_strdup (username);
}

/**
 * gksu_context_get_user:
 * @context: the #GksuContext from which to grab the information
 *
 * Gets the user the command will be run as, as set
 * by gksu_context_set_user.
 *
 * Returns: a string with the user or NULL if not set.
 */
const gchar*
gksu_context_get_user (GksuContext *context)
{
  return context->user;
}

/**
 * gksu_context_set_command:
 * @context: the #GksuContext you want to modify
 * @command: the command that shall be ran
 *
 * Sets up what command will run with the target user.
 *
 */
void
gksu_context_set_command (GksuContext *context, gchar *command)
{
  g_assert (command != NULL);

  if (context->command)
    g_free (context->command);
  context->command = g_strdup (command);

  /* startup notification */
  sn_launcher_context_set_binary_name (context->sn_context,
				       command);
}

/**
 * gksu_context_get_command:
 * @context: the #GksuContext from which to grab the information
 *
 * Gets the command that will be run, as set by
 * gksu_context_set_command.
 *
 * Returns: a string with the command or NULL if not set.
 */
const gchar*
gksu_context_get_command (GksuContext *context)
{
  return context->command;
}

/**
 * gksu_context_set_login_shell:
 * @context: the #GksuContext you want to modify
 * @value: TRUE or FALSE
 *
 * Should the shell in which the command will be run be
 * a login shell?
 */
void
gksu_context_set_login_shell (GksuContext *context, gboolean value)
{
  context->login_shell = value;
}

/**
 * gksu_context_get_login_shell:
 * @context: the #GksuContext from which to grab the information
 *
 * Finds out if the shell created by the underlying su process
 * will be a login shell.
 *
 * Returns: TRUE if the shell will be a login shell, FALSE otherwise.
 */
gboolean
gksu_context_get_login_shell (GksuContext *context)
{
  return context->login_shell;
}

/**
 * gksu_context_set_keep_env:
 * @context: the #GksuContext you want to modify
 * @value: TRUE or FALSE
 *
 * Should the environment be kept as it is? Defaults to
 * TRUE. Notice that setting this to FALSE may cause the
 * X authorization stuff to fail.
 */
void
gksu_context_set_keep_env (GksuContext *context, gboolean value)
{
  context->keep_env = value;
}

/**
 * gksu_context_get_keep_env:
 * @context: the #GksuContext from which to grab the information
 *
 * Finds out if the environment in which the program will be
 * run will be reset.
 *
 * Returns: TRUE if the environment is going to be kept,
 * FALSE otherwise.
 */
gboolean
gksu_context_get_keep_env (GksuContext *context)
{
  return context->keep_env;
}

/**
 * gksu_context_set_description:
 * @context: the #GksuContext you want to modify
 * @description: a string to set the description for
 *
 * Set the nice name for the action that is being run that the window
 * that asks for the password will have.  This is only meant to be
 * used if the default window is used, of course.
 */
void
gksu_context_set_description (GksuContext *context, gchar *description)
{
  if (context->description)
    g_free (context->description);
  context->description = g_strdup (description);
}

/**
 * gksu_context_get_description:
 * @context: the #GksuContext you want to get the description from.
 *
 * Get the description that the window will have when the
 * default function for requesting the password is
 * called.
 *
 * Returns: a string with the description or NULL if not set.
 */
gchar*
gksu_context_get_description (GksuContext *context)
{
  return context->description;
}

/**
 * gksu_context_set_message:
 * @context: the #GksuContext you want to modify
 * @message: a string to set the message for
 *
 * Set the message that the window that asks for the password will have.
 * This is only meant to be used if the default window is used, of course.
 */
void
gksu_context_set_message (GksuContext *context, gchar *message)
{
  if (context->message)
    g_free (context->message);
  context->message = g_strdup (message);
}

/**
 * gksu_context_get_message:
 * @context: the #GksuContext you want to get the message from.
 *
 * Get the message that the window will have when the
 * default function for requesting the password is
 * called.
 *
 * Returns: a string with the message or NULL if not set.
 */
gchar*
gksu_context_get_message (GksuContext *context)
{
  return context->message;
}

/**
 * gksu_context_set_alert:
 * @context: the #GksuContext you want to modify
 * @alert: a string to set the alert for
 *
 * Set the alert that the window that asks for the password will have.
 * This is only meant to be used if the default window is used, of course.
 * This alert should be used to display messages such as 'incorrect password',
 * for instance.
 */
void
gksu_context_set_alert (GksuContext *context, gchar *alert)
{
  if (context->alert)
    g_free (context->alert);
  context->alert = g_strdup (alert);
}

/**
 * gksu_context_get_alert:
 * @context: the #GksuContext you want to get the alert from.
 *
 * Get the alert that the window will have when the
 * default function for requesting the password is
 * called.
 *
 * Returns: a string with the alert or NULL if not set.
 */
gchar*
gksu_context_get_alert (GksuContext *context)
{
  return context->alert;
}

/**
 * gksu_context_set_debug:
 * @context: the #GksuContext you want to modify
 * @value: TRUE or FALSE
 *
 * Set up if debuging information should be printed.
 */
void
gksu_context_set_grab (GksuContext *context, gboolean value)
{
  context->grab = value;
}

/**
 * gksu_context_get_grab:
 * @context: the #GksuContext you want to ask whether a grab will be done.
 *
 * Returns TRUE if gksu has been asked to do a grab on keyboard and mouse
 * when asking for the password.
 *
 * Returns: TRUE if yes, FALSE otherwise.
 */
gboolean
gksu_context_get_grab (GksuContext *context)
{
  return context->grab;
}

/**
 * gksu_context_set_always_ask_password:
 * @context: the #GksuContext you want to modify
 * @value: TRUE or FALSE
 *
 * Set up if gksu should always ask for a password. Notice that this
 * will only work when passwords are cached, as done by gnome-keyring
 * for gksu's su mode and by sudo for gksu's sudo mode, but will have no
 * effect if su or sudo are set up to not require the password at all.
 */
void
gksu_context_set_always_ask_password (GksuContext *context, gboolean value)
{
  context->always_ask_password = value;
}

/**
 * gksu_context_get_always_ask_password:
 * @context: the #GksuContext you want to ask whether a grab will be done.
 *
 * Returns TRUE if gksu has been asked to always ask for a password 
 * (even if sudo or gnome-keyring have cached it)
 *
 * Returns: TRUE if yes, FALSE otherwise.
 */
gboolean
gksu_context_get_always_ask_password (GksuContext *context)
{
   return context->always_ask_password;
}


/**
 * gksu_context_set_launcher_context:
 * @context: the #GksuContext you want to set the sn context in
 * @sn_context: the #SnLauncherContext you want to set
 *
 * Tell libgksu to use the given #SnLauncherContext for startup notification.
 * Currently the library will use this to set DESKTOP_STARTUP_ID in the
 * environment of the child and to issue initiate and complete events.
 * Notice that you don't need to use this function unless you want to
 * override gksu's default behavior on startup notification, since the
 * library will create its own context.
 *
 * Returns: the #SnLauncherContext which is set, or NULL if none was set
 */
void
gksu_context_set_launcher_context (GksuContext *context,
				   SnLauncherContext *sn_context)
{
  if (context->sn_context)
    sn_launcher_context_unref (context->sn_context);
  context->sn_context = sn_context;
}

/**
 * gksu_context_get_launcher_context:
 * @context: the #GksuContext you want to get the sn context from
 *
 * Gets the current startup notification launcher context
 *
 * Returns: the #SnLauncherContext which is set, or NULL if none was set
 */
SnLauncherContext*
gksu_context_get_launcher_context (GksuContext *context)
{
  return context->sn_context;
}

/**
 * gksu_context_set_launcher_id:
 * @context: the #GksuContext you want to set the sn id in
 * @sn_context: the sn_id you want to set, as a #gchar
 */
void
gksu_context_set_launcher_id (GksuContext *context,
			      gchar *sn_id)
{
  if (context->sn_id)
    g_free (context->sn_id);
  context->sn_id = g_strdup(sn_id);
}

/**
 * gksu_context_launch_initiate:
 * @context: the #GksuContext you want to initiate the launch for
 *
 * Initiates the launch, as far as Startup Notification is concerned;
 * This will only be used internally, usually.
 */
static void
gksu_context_launch_initiate (GksuContext *context)
{
  gchar *sid = NULL;
  guint32 launch_time = gdk_x11_display_get_user_time (gdk_display_get_default ());
  static gboolean initiated = FALSE;

  if (!initiated)
    initiated = TRUE;
  else
    return;

  sn_launcher_context_initiate (context->sn_context,
				g_get_prgname (),
				gksu_context_get_command (context),
				launch_time);

  sid = g_strdup_printf ("%s", sn_launcher_context_get_startup_id (context->sn_context));
  gksu_context_set_launcher_id (context, sid);

  if (context->debug)
    fprintf (stderr, "STARTUP_ID: %s\n", sid);
  setenv ("DESKTOP_STARTUP_ID", sid, TRUE);
  g_free(sid);
}

/**
 * gksu_context_launch_complete:
 * @context: the #GksuContext you want to complete the launch for
 *
 * Completes the launch, as far as Startup Notification is concerned;
 * This will only be used internally, usually.
 */
static void
gksu_context_launch_complete (GksuContext *context)
{
  sn_launcher_context_complete(context->sn_context);
}

/**
 * gksu_context_set_debug:
 * @context: the #GksuContext you want to modify
 * @value: TRUE or FALSE
 *
 * Set up if debuging information should be printed.
 */
void
gksu_context_set_debug (GksuContext *context, gboolean value)
{
  context->debug = value;
}

/**
 * gksu_context_get_debug:
 * @context: the #GksuContext from which to grab the information
 *
 * Finds out if the library is configured to print debuging
 * information.
 *
 * Returns: TRUE if it is, FALSE otherwise.
 */
gboolean
gksu_context_get_debug (GksuContext *context)
{
  return context->debug;
}

/**
 * gksu_context_free
 * @context: the #GksuContext to be freed.
 *
 * Frees the given #GksuContext.
 */
void
gksu_context_free (GksuContext *context)
{
  g_free (context->xauth);
  g_free (context->dir);
  g_free (context->display);

  g_object_unref (context->gconf_client);

  g_free (context->description);
  g_free (context->message);

  g_free (context->user);
  g_free (context->command);

  g_free (context);
}

/**
 * gksu_context_ref
 * @context: A #GksuContext struct.
 *
 * Increments the reference count of the given #GksuContext.
 */
GksuContext*
gksu_context_ref (GksuContext *context)
{
  context->ref_count++;
  return context;
}

/**
 * gksu_context_unref
 * @context: A #GksuContext struct.
 *
 * Decrements the reference count of the given #GksuContext struct,
 * freeing it if the reference count falls to zero.
 */
void
gksu_context_unref (GksuContext *context)
{
  if (--context->ref_count == 0)
    {
        gksu_context_free (context);
    }
}

GType
gksu_context_get_type (void)
{
  static GType type_gksu_context = 0;

  if (!type_gksu_context)
    type_gksu_context = g_boxed_type_register_static
      ("GksuContext", 
       (GBoxedCopyFunc) gksu_context_ref,
       (GBoxedFreeFunc) gksu_context_unref);

  return type_gksu_context;
}


/**
 * gksu_su_full:
 * @context: a #GksuContext
 * @ask_pass: a #GksuAskPassFunc to be called when the lib determines
 * requesting a password is necessary; it may be NULL, in which case
 * the standard password request dialog will be used
 * @ask_pass_data: a #gpointer with user data to be passed to the
 * #GksuAskPasswordFunc
 * @pass_not_needed: a #GksuPassNotNeededFunc that will be called
 * when the command is being run without the need for requesting
 * a password; it will only be called if the display-no-pass-info
 * gconf key is enabled; NULL will have the standard dialog be shown
 * @pass_not_needed_data: a #gpointer with the user data to be passed to the
 * #GksuPasswordNotNeededFunc
 * @error: a #GError object to be filled with the error code or NULL
 *
 * This is a compatibility shim over gksu_su_fuller, which, for
 * compatibility reasons, lacks the 'exit_status' argument. You should
 * check the documentation for gksu_su_fuller for information about
 * the arguments.
 *
 * Returns: TRUE if all went fine, FALSE if failed
 */

gboolean
gksu_su_full (GksuContext *context,
	      GksuAskPassFunc ask_pass,
	      gpointer ask_pass_data,
	      GksuPassNotNeededFunc pass_not_needed,
	      gpointer pass_not_needed_data,
	      GError **error)
{
  return gksu_su_fuller(context,
  			ask_pass, ask_pass_data,
			pass_not_needed, pass_not_needed_data,
			NULL, error);
}


/**
 * gksu_su_fuller:
 * @context: a #GksuContext
 * @ask_pass: a #GksuAskPassFunc to be called when the lib determines
 * requesting a password is necessary; it may be NULL, in which case
 * the standard password request dialog will be used
 * @ask_pass_data: a #gpointer with user data to be passed to the
 * #GksuAskPasswordFunc
 * @pass_not_needed: a #GksuPassNotNeededFunc that will be called
 * when the command is being run without the need for requesting
 * a password; it will only be called if the display-no-pass-info
 * gconf key is enabled; NULL will have the standard dialog be shown
 * @pass_not_needed_data: a #gpointer with the user data to be passed to the
 * #GksuPasswordNotNeededFunc
 * @exit_status: an optional pointer to a #gint8 which will be filled with
 * the exit status of the child process
 * @error: a #GError object to be filled with the error code or NULL
 *
 * This could be considered one of the main functions in GKSu.
 * it is responsible for doing the 'user changing' magic calling
 * the #GksuAskPassFunc function to request a password if needed.
 * and the #GksuPassNotNeededFunc function if a password won't be
 * needed, so the application has the oportunity of warning the user
 * what it's doing.
 *
 * This function uses su as backend.
 *
 * Returns: TRUE if all went fine, FALSE if failed
 */
gboolean
gksu_su_fuller (GksuContext *context,
	        GksuAskPassFunc ask_pass,
	        gpointer ask_pass_data,
	        GksuPassNotNeededFunc pass_not_needed,
	        gpointer pass_not_needed_data,
	        gint8 *exit_status,
	        GError **error)
{
  GQuark gksu_quark;
  int i = 0;

  gchar auxcommand[] = PREFIX "/lib/" PACKAGE "/gksu-run-helper";

  int fdpty;
  pid_t pid;

  context->sudo_mode = FALSE;

  gksu_quark = g_quark_from_string (PACKAGE);

  if (!context->command)
    {
      g_set_error (error, gksu_quark, GKSU_ERROR_NOCOMMAND,
		   _("gksu_run needs a command to be run, "
		     "none was provided."));
      return FALSE;
    }

  if (!context->user)
    context->user = g_strdup ("root");

  if (!g_file_test (auxcommand, G_FILE_TEST_IS_EXECUTABLE))
    {
      g_set_error (error, gksu_quark, GKSU_ERROR_HELPER,
		   _("The gksu-run-helper command was not found or "
		     "is not executable."));
      return FALSE;
    }

  if (!prepare_xauth (context))
    {
      g_set_error (error, gksu_quark, GKSU_ERROR_XAUTH,
		   _("Unable to copy the user's Xauthorization file."));
      return FALSE;
    }

  if (context->sn_context)
    gksu_context_launch_initiate (context);

  pid = forkpty(&fdpty, NULL, NULL, NULL);
  if (pid == 0)
    {
      gchar **cmd = g_malloc (sizeof(gchar*)*7);

      setsid();   // make us session leader
      cmd[i] = g_strdup ("/bin/su"); i++;
      if (context->login_shell)
	{
	  cmd[i] = g_strdup ("-"); i++;
	}
      cmd[i] = g_strdup (context->user); i++;
      if (context->keep_env)
	{
	  cmd[i] = g_strdup ("-p"); i++;
	}
      cmd[i] = g_strdup ("-c"); i++;

      /* needs to get X authorization prior to running the program */
      cmd[i] = g_strdup_printf ("%s \"%s\"", auxcommand,
				context->command); i++;

      cmd[i] = NULL;

      /* executes the command */
      if (execv (cmd[0], cmd) == -1)
	{
	  fprintf (stderr,
		   "Unable to run /bin/su: %s",
		   strerror(errno));
	}

      for (i = 0 ; cmd[i] != NULL ; i++)
	g_free (cmd[i]);
      g_free(cmd);

      _exit(1);
    }
  else if (pid == -1)
    {
      g_set_error (error, gksu_quark, GKSU_ERROR_FORK,
		   _("Failed to fork new process: %s"),
		   strerror(errno));
      return FALSE;
    }
  else
    {
      fd_set rfds;

      struct timeval tv;

      struct passwd *pwd = NULL;
      gint target_uid = -1;
      gint my_uid = 0;

      gchar buf[256] = {0};
      gint status;

      gchar *password = NULL;
      gchar *cmdline = NULL;
      gboolean password_needed = FALSE;
      gboolean used_gnome_keyring = FALSE;

      my_uid = getuid();
      pwd = getpwnam (context->user);
      if (pwd)
	target_uid = pwd->pw_uid;

      if (ask_pass == NULL)
	{
	  ask_pass = su_ask_password;
	}

      if (pass_not_needed == NULL)
	{
	  pass_not_needed = no_pass;
	}

      /* no need to ask for password if we're already root */
      if (my_uid != target_uid && my_uid)
	{
	  gint count;
	  struct termios tio;

	  read (fdpty, buf, 255);
	  if (context->debug)
	    fprintf (stderr, "gksu_context_run: buf: -%s-\n", buf);

	  /* make sure we notice that ECHO is turned off, if it gets
	     turned off */
	  tcgetattr (fdpty, &tio);
	  for (count = 0; (tio.c_lflag & ECHO) && count < 15; count++)
	    {
	      usleep (1000);
	      tcgetattr (fdpty, &tio);
	    }

	  if (!(tio.c_lflag & ECHO))
	    {
	      gboolean prompt_grab;
	      prompt_grab = gconf_client_get_bool (context->gconf_client, BASE_PATH "prompt",
						   NULL);

	      if (prompt_grab)
		gksu_prompt_grab (context);

              /* try to get the password from the GNOME Keyring first, but
	       * only if we have not been requested to always ask for the
	       * password
	       */
	      if (!context->always_ask_password)
	        password = get_gnome_keyring_password (context);
	      if (password == NULL)
		{
		  password = ask_pass (context, buf, ask_pass_data, error);
		  if (context->debug)
		    {
		      fprintf (stderr, "no password on keyring\n");
		      if (password == NULL)
			fprintf (stderr, "no password from ask_pass!\n");
		    }
		}
	      else
		{
		  if (context->debug)
		    fprintf (stderr, "password from keyring found\n");
		  used_gnome_keyring = TRUE;
		}
	      if (password == NULL || (error && (*error)))
		{
		  if (context->debug)
		    fprintf (stderr, "gksu_su_full: problem getting password - getting out\n");
		  if (context->debug && error)
		    fprintf (stderr, "error: %s\n", (*error)->message);
		  nullify_password (password);
		  return TRUE;
		}

	      write (fdpty, password, strlen(password) + 1);
	      write (fdpty, "\n", 1);
	      password_needed = TRUE;
	    }
	}

      if (context->debug)
	fprintf (stderr, "DEBUG (run:after-pass) buf: -%s-\n", buf);
      if (strncmp (buf, "gksu", 4) && strncmp (buf, "su", 2))
	{
	  /* drop the \n echoed on password entry if su did request
	     a password */
	  if (password_needed)
	    read_line (fdpty, buf, 255);
	  if (context->debug)
	    fprintf (stderr, "DEBUG (run:post-after-pass) buf: -%s-\n", buf);
	  read_line (fdpty, buf, 255);
	  if (context->debug)
	    fprintf (stderr, "DEBUG (run:post-after-pass) buf: -%s-\n", buf);
	}

      FD_ZERO (&rfds);
      FD_SET (fdpty, &rfds);
      tv.tv_sec = 1;
      tv.tv_usec = 0;
      int loop_count = 0;
      while (TRUE)
	{
	  int retval = 0;

	  /* Red Hat's su shows the full path to su in its error messages. */
	  if (!strncmp (buf, "su:", 3) ||
	      !strncmp (buf, "/bin/su:", 7))
	    {
	      gchar **strings;

	      if (password)
		{
		  nullify_password (password);
		  unset_gnome_keyring_password (context);
		}

	      strings = g_strsplit (buf, ":", 2);

	      /* Red Hat and Fedora use 'incorrect password'. */
	      if (strings[1] &&
	          (g_str_has_prefix(strings[1], " Authentication failure") ||
	           g_str_has_prefix(strings[1], " incorrect password")))
		{
		  if (used_gnome_keyring)
		    g_set_error (error, gksu_quark,
				 GKSU_ERROR_WRONGAUTOPASS,
				 _("Wrong password got from keyring."));
		  else
		    g_set_error (error, gksu_quark,
				 GKSU_ERROR_WRONGPASS,
				 _("Wrong password."));
		}
	      g_strfreev (strings);

	      if (context->debug)
		fprintf (stderr, "DEBUG (auth_failed) buf: -%s-\n", buf);

	      break;
	    }
	  else if (!strncmp (buf, "gksu: waiting", 13))
	    {
	      gchar *line;

	      if (password)
		{
		  set_gnome_keyring_password (context, password);
		  nullify_password (password);
		}

	      if (context->debug)
		fprintf (stderr, "DEBUG (gksu: waiting) buf: -%s-\n", buf);

	      line = g_strdup_printf ("gksu-run: %s\n", context->display);
	      write (fdpty, line, strlen(line));
	      g_free (line);

	      line = g_strdup_printf ("gksu-run: %s\n", context->sn_id);
	      write (fdpty, line, strlen(line));
	      g_free (line);

	      line = g_strdup_printf ("gksu-run: %s\n", context->xauth);
	      write (fdpty, line, strlen(line));
	      g_free (line);

#ifndef __FreeBSD_kernel__
	      tcdrain (fdpty);
#endif

	      bzero (buf, 256);
	      read (fdpty, buf, 255);

	      break;
	    }

	  retval = select (fdpty + 1, &rfds, NULL, NULL, &tv);
	  if ((loop_count > 50) || (!retval))
	    {
	      gchar *emsg = NULL;
	      gchar *converted_str = NULL;
	      GError *converr = NULL;

	      if (password)
		nullify_password (password);

	      converted_str = g_locale_to_utf8 (buf, -1, NULL, NULL, &converr);
	      if (converr)
		{
		  g_warning (_("Failed to communicate with "
			       "gksu-run-helper.\n\n"
			       "Received:\n"
			       " %s\n"
			       "While expecting:\n"
			       " %s"), buf, "gksu: waiting");
		  emsg = g_strdup_printf (_("Failed to communicate with "
					    "gksu-run-helper.\n\n"
					    "Received bad string "
					    "while expecting:\n"
					    " %s"), "gksu: waiting");
		  g_error_free (converr);
		}
	      else
		emsg = g_strdup_printf (_("Failed to communicate with "
					  "gksu-run-helper.\n\n"
					  "Received:\n"
					  " %s\n"
					  "While expecting:\n"
					  " %s"), converted_str, "gksu: waiting");
	      g_free (converted_str);

	      g_set_error_literal (error, gksu_quark, GKSU_ERROR_HELPER, emsg);
	      g_free (emsg);

	      if (context->debug)
		fprintf (stderr, "DEBUG (failed!) buf: -%s-\n", buf);

	      return FALSE;
	    }
	  else if (retval == -1)
	    {
	      if (context->debug)
		fprintf (stderr, "DEBUG (select failed!) buf: %s\n", buf);
	      return FALSE;
	    }

	  read (fdpty, buf, 255);
	  if (context->debug)
	    fprintf (stderr, "DEBUG (run:after-pass) buf: -%s-\n", buf);
	  loop_count++;
	}

      if (!password_needed || used_gnome_keyring)
	{
	  gboolean should_display;

	  should_display = gconf_client_get_bool (context->gconf_client,
						  BASE_PATH "display-no-pass-info", NULL);

	  /* configuration tells us to show this message */
	  if (should_display)
	    {
	      if (context->debug)
		fprintf (stderr, "Calling pass_not_needed window...\n");
	      pass_not_needed (context, pass_not_needed_data);
	      /* make sure it is displayed */
	      while (gtk_events_pending ())
		gtk_main_iteration ();
	    }
	}

      cmdline = g_strdup("bin/su");
      /* wait for the child process to end or become something other
	 than su */
      pid_t pid_exited;
      while ((!(pid_exited = waitpid (pid, &status, WNOHANG))) &&
	     (g_str_has_suffix(cmdline, "bin/su")))
	{
	  if (cmdline)
	    g_free (cmdline);
	  cmdline = get_process_name (pid);
	  usleep(100000);
	}

      if (context->sn_context)
	gksu_context_launch_complete (context);

      bzero(buf, 256);
      while (read (fdpty, buf, 255) > 0)
	{
	  fprintf (stderr, "%s", buf);
	  bzero(buf, 256);
	}

      if (pid_exited != pid)
	waitpid(pid, &status, 0);

      if (exit_status)
      {
      	if (WIFEXITED(status)) {
      	  *exit_status = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
	  *exit_status = -1;
	}
      }

      if (WEXITSTATUS(status))
	{
	  if(cmdline)
	    {
	      /* su already exec()ed something else, don't report
	       * exit status errors in that case
	       */
	      if (!g_str_has_suffix (cmdline, "su"))
		{
		  g_free (cmdline);
		  return FALSE;
		}
	      g_free (cmdline);
	    }

	  if (error == NULL)
	    g_set_error (error, gksu_quark,
			 GKSU_ERROR_CHILDFAILED,
			 _("su terminated with %d status"),
			 WEXITSTATUS(status));
	}
    }

  if (error)
    return FALSE;

  return TRUE;
}

/**
 * gksu_su
 * @command_line: the command line that will be executed as other user
 * @error: a #GError to be set with the error condition, if an error
 * happens
 *
 * This function is a wrapper for gksu_su_run_full. It will call it
 * without giving the callback functions, which leads to the standard
 * ones being called. A simple #GksuContext is created to hold the
 * user name and the command.
 *
 * Returns: TRUE if all went well, FALSE if an error happend.
 */
gboolean
gksu_su (gchar *command_line, GError **error)
{
  GksuContext *context = gksu_context_new ();
  gboolean retval;

  context->command = g_strdup (command_line);
  context->user = g_strdup ("root");
  retval = gksu_su_full (context,
			 NULL, NULL,
			 NULL, NULL,
			 error);
  gksu_context_free (context);
  return retval;
}

static void
read_line (int fd, gchar *buffer, int n)
{
  gint counter = 0;
  gchar tmp[2] = {0};

  for (; counter < (n - 1); counter++)
    {
      tmp[0] = '\0';
      read (fd, tmp, 1);
      if (tmp[0] == '\n')
	break;
      buffer[counter] = tmp[0];
    }
  buffer[counter] = '\0';
}

/**
 * gksu_sudo_full:
 * @context: a #GksuContext
 * @ask_pass: a #GksuAskPassFunc to be called when the lib determines
 * requesting a password is necessary; it may be NULL, in which case
 * the standard password request dialog will be used
 * @ask_pass_data: a #gpointer with user data to be passed to the
 * #GksuAskPasswordFunc
 * @pass_not_needed: a #GksuPassNotNeededFunc that will be called
 * when the command is being run without the need for requesting
 * a password; it will only be called if the display-no-pass-info
 * gconf key is enabled; NULL will have the standard dialog be shown
 * @pass_not_needed_data: a #gpointer with the user data to be passed to the
 * #GksuPasswordNotNeededFunc
 * @error: a #GError object to be filled with the error code or NULL
 *
 * This is a compatibility shim over gksu_sudo_fuller, which, for
 * compatibility reasons, lacks the 'exit_status' argument. You should
 * check the documentation for gksu_sudo_fuller for information about
 * the arguments.
 *
 * Returns: TRUE if all went fine, FALSE if failed
 */

gboolean
gksu_sudo_full (GksuContext *context,
		GksuAskPassFunc ask_pass,
		gpointer ask_pass_data,
		GksuPassNotNeededFunc pass_not_needed,
		gpointer pass_not_needed_data,
		GError **error)
{
  return gksu_sudo_fuller(context,
  			  ask_pass, ask_pass_data,
			  pass_not_needed, pass_not_needed_data,
			  NULL, error);
}

/**
 * gksu_sudo_fuller:
 * @context: a #GksuContext
 * @ask_pass: a #GksuAskPassFunc to be called when the lib determines
 * requesting a password is necessary; it may be NULL, in which case
 * the standard password request dialog will be used
 * @ask_pass_data: a #gpointer with user data to be passed to the
 * #GksuAskPasswordFunc
 * @pass_not_needed: a #GksuPassNotNeededFunc that will be called
 * when the command is being run without the need for requesting
 * a password; it will only be called if the display-no-pass-info
 * gconf key is enabled; NULL will have the standard dialog be shown
 * @pass_not_needed_data: a #gpointer with the user data to be passed to the
 * #GksuPasswordNotNeededFunc
 * @error: a #GError object to be filled with the error code or NULL
 * @exit_status: an optional pointer to a #gint8 which will be filled with
 * the exit status of the child process
 *
 * This could be considered one of the main functions in GKSu.
 * it is responsible for doing the 'user changing' magic calling
 * the #GksuAskPassFunc function to request a password if needed.
 * and the #GksuPassNotNeededFunc function if a password won't be
 * needed, so the application has the oportunity of warning the user
 * what it's doing.
 *
 * This function uses the sudo backend.
 *
 * Returns: TRUE if all went fine, FALSE if failed
 */
gboolean
gksu_sudo_fuller (GksuContext *context,
		  GksuAskPassFunc ask_pass,
		  gpointer ask_pass_data,
		  GksuPassNotNeededFunc pass_not_needed,
		  gpointer pass_not_needed_data,
		  gint8 *exit_status,
		  GError **error)
{
  char **cmd;
  char buffer[256] = {0};
  char *child_stderr = NULL;

#ifdef SUDO_FORKPTY
  /* This command is used to gain a token */
  char *const verifycmd[] =
    {
      "/usr/bin/sudo", "-p", "GNOME_SUDO_PASS", "-v", NULL
    };
#endif

  int argcount = 8;
  int i, j;

  GQuark gksu_quark;

  gchar *xauth = NULL,
    *xauth_env = NULL;

  pid_t pid;
  int status;
#ifdef SUDO_FORKPTY
  FILE *fdfile = NULL;
  int fdpty = -1;
#else
  FILE *infile, *outfile;
  int parent_pipe[2];  /* For talking to the parent */
  int child_pipe[2];   /* For talking to the child */
#endif

  context->sudo_mode = TRUE;

  gksu_quark = g_quark_from_string (PACKAGE);

  if (!context->command)
    {
      g_set_error (error, gksu_quark, GKSU_ERROR_NOCOMMAND,
		   _("gksu_sudo_run needs a command to be run, "
		     "none was provided."));
      return FALSE;
    }

  if (!context->user)
    context->user = g_strdup ("root");

  if (ask_pass == NULL)
    {
      if (context->debug)
	fprintf (stderr, "No ask_pass set, using default!\n");
      ask_pass = su_ask_password;
    }
  if (pass_not_needed == NULL)
    {
      pass_not_needed = no_pass;
    }

  if (context->always_ask_password)
    {
       gint exit_status;
       g_spawn_command_line_sync("/usr/bin/sudo -K", NULL, NULL, &exit_status, NULL);
    }


  /*
     FIXME: need to set GError in a more detailed way
  */
  if (!sudo_prepare_xauth (context))
    {
      g_set_error (error, gksu_quark, GKSU_ERROR_XAUTH,
		   _("Unable to copy the user's Xauthorization file."));
      return FALSE;
    }

  /* sets XAUTHORITY */
  xauth = g_strdup_printf ("%s/.Xauthority", context->dir);
  xauth_env = getenv ("XAUTHORITY");
  setenv ("XAUTHORITY", xauth, TRUE);
  if (context->debug)
    fprintf (stderr, "xauth: %s\n", xauth);

  /* set startup id */
  if (context->sn_context)
    gksu_context_launch_initiate (context);

  cmd = g_new (gchar *, argcount + 1);

  argcount = 0;

  /* sudo binary */
  cmd[argcount] = g_strdup("/usr/bin/sudo");
  argcount++;

  if (!context->keep_env)
    {
      /* Make sudo set $HOME */
      cmd[argcount] = g_strdup("-H");
      argcount++;
    }

  /* Make sudo read from stdin */
  cmd[argcount] = g_strdup("-S");
  argcount++;

#ifdef SUDO_FORKPTY
  /* Make sudo noninteractive (we should already have a token) */
  cmd[argcount] = g_strdup("-n");
  argcount++;
#endif

  /* Make sudo use next arg as prompt */
  cmd[argcount] = g_strdup("-p");
  argcount++;

  /* prompt */
  cmd[argcount] = g_strdup("GNOME_SUDO_PASS");
  argcount++;

  /* Make sudo use the selected user */
  cmd[argcount] = g_strdup("-u");
  argcount++;

  /* user */
  cmd[argcount] = g_strdup(context->user);
  argcount++;

  /* sudo does not understand this if we do not use -H
     weird.
  */
  if (!context->keep_env)
    {
      /* Make sudo stop processing options */
      cmd[argcount] = g_strdup("--");
      argcount++;
    }

  {
    gchar *tmp_arg = g_malloc (sizeof(gchar)*1);
    gboolean inside_quotes = FALSE;

    tmp_arg[0] = '\0';

    for (i = j = 0; ; i++)
      {
	if ((context->command[i] == '\'') && (context->command[i-1] != '\\'))
	  {
	    i = i + 1;
	    inside_quotes = !inside_quotes;
	  }

	if ((context->command[i] == ' ' && inside_quotes == FALSE)
	    || context->command[i] == '\0')
	  {
	    tmp_arg = g_realloc (tmp_arg, sizeof(gchar)*(j+1));
	    tmp_arg[j] = '\0';
	    cmd = g_realloc (cmd, sizeof(gchar*) * (argcount + 1));
	    cmd[argcount] = g_strdup (tmp_arg);

	    g_free (tmp_arg);

	    argcount = argcount + 1;
	    j = 0;

	    if (context->command[i] == '\0')
	      break;

	    tmp_arg = g_malloc (sizeof(gchar)*1);
	    tmp_arg[0] = '\0';
	  }
	else
	  {
	    if (context->command[i] == '\\' && context->command[i+1] != '\\')
	      i = i + 1;
	    tmp_arg = g_realloc (tmp_arg, sizeof(gchar)*(j+1));
	    tmp_arg[j] = context->command[i];
	    j = j + 1;
	  }
      }
  }
  cmd = g_realloc (cmd, sizeof(gchar*) * (argcount + 1));
  cmd[argcount] = NULL;

  if (context->debug)
    {
      for (i = 0; cmd[i] != NULL; i++)
	fprintf (stderr, "cmd[%d]: %s\n", i, cmd[i]);
    }

#ifdef SUDO_FORKPTY
  pid = forkpty(&fdpty, NULL, NULL, NULL);
#else
  if ((pipe(parent_pipe)) == -1)
    {
      g_set_error (error, gksu_quark, GKSU_ERROR_PIPE,
                   _("Error creating pipe: %s"),
                   strerror(errno));
      sudo_reset_xauth (context, xauth, xauth_env);
      return FALSE;
    }
  if ((pipe(child_pipe)) == -1)
    {
      g_set_error (error, gksu_quark, GKSU_ERROR_PIPE,
                   _("Error creating pipe: %s"),
                   strerror(errno));
      sudo_reset_xauth (context, xauth, xauth_env);
      return FALSE;
    }

  pid = fork();
#endif

  if (pid == 0)
    {
      // Child
      setsid ();   // make us session leader

#ifdef SUDO_FORKPTY
      execv (verifycmd[0], verifycmd);
#else
      close (child_pipe[1]);
      dup2 (child_pipe[0], STDIN_FILENO);
      dup2 (parent_pipe[1], STDERR_FILENO);
      execv (cmd[0], cmd);
#endif

      g_set_error (error, gksu_quark, GKSU_ERROR_EXEC,
		   _("Failed to exec new process: %s"),
		   strerror(errno));
      sudo_reset_xauth (context, xauth, xauth_env);
      return FALSE;
    }
  else if (pid == -1)
    {
      g_set_error (error, gksu_quark, GKSU_ERROR_FORK,
		   _("Failed to fork new process: %s"),
		   strerror(errno));
      sudo_reset_xauth (context, xauth, xauth_env);
      return FALSE;
    }

  else
    {
      gint counter = 0;
      gchar *cmdline = NULL;

      // Parent
#ifdef SUDO_FORKPTY
      fdfile = fdopen(fdpty, "w+");

      fcntl (fdpty, F_SETFL, O_NONBLOCK);
#else
      close(parent_pipe[1]);

      infile = fdopen(parent_pipe[0], "r");
      if (!infile)
       {
         g_set_error (error, gksu_quark, GKSU_ERROR_PIPE,
                      _("Error opening pipe: %s"),
                      strerror(errno));
         sudo_reset_xauth (context, xauth, xauth_env);
         return FALSE;
       }

      outfile = fdopen(child_pipe[1], "w");
      if (!outfile)
       {
         g_set_error (error, gksu_quark, GKSU_ERROR_PIPE,
                      _("Error opening pipe: %s"),
                      strerror(errno));
         sudo_reset_xauth (context, xauth, xauth_env);
         return FALSE;
       }

      fcntl (parent_pipe[0], F_SETFL, O_NONBLOCK);
#endif

      { /* no matter if we can read, since we're using
	   O_NONBLOCK; this is just to avoid the prompt
	   showing up after the read */
	fd_set rfds;
	struct timeval tv;

	FD_ZERO(&rfds);
#ifdef SUDO_FORKPTY
	FD_SET(fdpty, &rfds);
#else
        FD_SET(parent_pipe[0], &rfds);
#endif
	tv.tv_sec = 1;
	tv.tv_usec = 0;

#ifdef SUDO_FORKPTY
	select (fdpty + 1, &rfds, NULL, NULL, &tv);
#else
        select (parent_pipe[0] + 1, &rfds, NULL, NULL, &tv);
#endif
      }

      /* Try hard to find the prompt; it may happen that we're
       * seeing sudo's lecture, or that some pam module is spitting
       * debugging stuff at the screen
       */
      for (counter = 0; counter < 50; counter++)
	{
	  if (strncmp (buffer, "GNOME_SUDO_PASS", 15) == 0)
	    break;

#ifdef SUDO_FORKPTY
	  read_line (fdpty, buffer, 256);
#else
          read_line (parent_pipe[0], buffer, 256);
#endif

	  if (context->debug)
	    fprintf (stderr, "buffer: -%s-\n", buffer);

	  usleep(1000);
	}

      if (context->debug)
	fprintf (stderr, "brute force GNOME_SUDO_PASS ended...\n");

      if (strncmp(buffer, "GNOME_SUDO_PASS", 15) == 0)
	{
	  gchar *password = NULL;
	  gboolean prompt_grab;

	  if (context->debug)
	    fprintf (stderr, "Yeah, we're in...\n");

	  prompt_grab = gconf_client_get_bool (context->gconf_client, BASE_PATH "prompt",
						   NULL);
	  if (prompt_grab)
	    gksu_prompt_grab (context);

	  password = ask_pass (context, _("Password: "),
			       ask_pass_data, error);
	  if (password == NULL || (*error))
	    {
	      nullify_password (password);
	      return FALSE;
	    }

	  usleep (1000);

#ifdef SUDO_FORKPTY
	  write (fdpty, password, strlen(password) + 1);
	  write (fdpty, "\n", 1);
#else
         fprintf (outfile, "%s\n", password);
         fclose (outfile);
#endif

	  nullify_password (password);

#ifdef SUDO_FORKPTY
	  fcntl(fdpty, F_SETFL, fcntl(fdpty, F_GETFL) & ~O_NONBLOCK);

	  /* ignore the first newline that comes right after sudo receives
	     the password */
	  fgets (buffer, 255, fdfile);
	  /* this is the status we are interested in */
	  fgets (buffer, 255, fdfile);
#else
          fcntl(parent_pipe[0], F_SETFL, fcntl(parent_pipe[0], F_GETFL) & ~O_NONBLOCK);

	  /* ignore the first newline that comes right after sudo receives
	     the password */
	  fgets (buffer, 255, infile);
	  /* this is the status we are interested in */
	  fgets (buffer, 255, infile);
#endif
	}
      else
	{
	  gboolean should_display;
	  if (context->debug)
	    fprintf (stderr, "No password prompt found; we'll assume we don't need a password.\n");

          /* turn NONBLOCK off, also if have no prompt */
#ifdef SUDO_FORKPTY
          fcntl(fdpty, F_SETFL, fcntl(fdpty, F_GETFL) & ~O_NONBLOCK);
#else
          fcntl(parent_pipe[0], F_SETFL, fcntl(parent_pipe[0], F_GETFL) & ~O_NONBLOCK);
#endif

	  should_display = gconf_client_get_bool (context->gconf_client,
						  BASE_PATH "display-no-pass-info", NULL);

	  /* configuration tells us to show this message */
	  if (should_display)
	    {
	      if (context->debug)
		fprintf (stderr, "Calling pass_not_needed window...\n");
	      pass_not_needed (context, pass_not_needed_data);
	      /* make sure it is displayed */
	      while (gtk_events_pending ())
		gtk_main_iteration ();
	    }

	  fprintf (stderr, "%s", buffer);
	}

      if (g_str_has_prefix (buffer, "Sorry, try again."))
	g_set_error (error, gksu_quark, GKSU_ERROR_WRONGPASS,
		     _("Wrong password."));
      else
	{
	  gchar *haystack = buffer;
	  gchar *needle;

	  needle = g_strstr_len (haystack, strlen (haystack), " ");
	  if (needle && (needle + 1))
	    {
	      needle += 1;
	      if (!strncmp (needle, "is not in", 9))
		g_set_error (error, gksu_quark, GKSU_ERROR_NOT_ALLOWED,
			     _("The underlying authorization mechanism (sudo) "
			       "does not allow you to run this program. Contact "
			       "the system administrator."));
	    }
	}

      /* If we have an error, let's just stop sudo right there. */
#ifdef SUDO_FORKPTY
      if (error)
        close(fdpty);
#else
      if (error)
        fclose(infile);
#endif

      cmdline = g_strdup("sudo");
      /* wait for the child process to end or become something other
	 than sudo */
      pid_t pid_exited;
      while ((!(pid_exited = waitpid (pid, &status, WNOHANG))) &&
	     (g_str_has_suffix(cmdline, "sudo")))
	{
	  if (cmdline)
	    g_free (cmdline);
	  cmdline = get_process_name (pid);
	  usleep(100000);
	}

      if (context->sn_context)
	gksu_context_launch_complete (context);

      /* if the process is still active waitpid() on it */
      if (pid_exited != pid)
	waitpid(pid, &status, 0);
      sudo_reset_xauth (context, xauth, xauth_env);

#if SUDO_FORKPTY
      /*
       * Did token acquisition succeed? If so, spawn sudo in
       * non-interactive mode. It should either succeed or die
       * immediately if you're not allowed to run the command.
       */
      if (WEXITSTATUS(status) == 0)
        {
          g_spawn_sync(NULL, cmd, NULL, 0, NULL, NULL,
                       NULL, &child_stderr, &status,
                       error);
        }
#endif

      if (exit_status)
      {
      	if (WIFEXITED(status)) {
      	  *exit_status = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
	  *exit_status = -1;
	}
      }

      if (WEXITSTATUS(status))
	{
          if (g_str_has_prefix(child_stderr, "Sorry, user "))
            {
              g_set_error (error, gksu_quark, GKSU_ERROR_NOT_ALLOWED,
                           _("The underlying authorization mechanism (sudo) "
                             "does not allow you to run this program. Contact "
                             "the system administrator."));
            }
	  if(cmdline)
	    {
	      /* sudo already exec()ed something else, don't report
	       * exit status errors in that case
	       */
	      if (!g_str_has_suffix (cmdline, "sudo"))
		{
		  g_free (cmdline);
		  g_free (child_stderr);
		  return FALSE;
		}
	      g_free (cmdline);
	    }
	  if (error == NULL)
	    g_set_error (error, gksu_quark,
			 GKSU_ERROR_CHILDFAILED,
			 _("sudo terminated with %d status"),
			 WEXITSTATUS(status));
	}
    }

  if (child_stderr != NULL)
    {
      fprintf(stderr, "%s", child_stderr);
      g_free(child_stderr);
    }

  /* if error is set we have found an error condition */
  return (error == NULL);
}

/**
 * gksu_sudo
 * @command_line: the command line that will be executed as other user
 * @error: a #GError to be set with the error condition, if an error
 * happens
 *
 * This function is a wrapper for gksu_sudo_run_full. It will call it
 * without giving the callback functions, which leads to the standard
 * ones being called. A simple #GksuContext is created to hold the
 * user name and the command.
 *
 * Returns: TRUE if all went well, FALSE if an error happend.
 */
gboolean
gksu_sudo (gchar *command_line,
	   GError **error)
{
  GksuContext *context = gksu_context_new ();
  gboolean retval;

  context->command = g_strdup (command_line);
  context->user = g_strdup ("root");
  retval = gksu_sudo_full (context,
			   NULL, NULL,
			   NULL, NULL,
			   error);
  gksu_context_free (context);

  return retval;
}

/**
 * gksu_run_full:
 * @context: a #GksuContext
 * @ask_pass: a #GksuAskPassFunc to be called when the lib determines
 * requesting a password is necessary; it may be NULL, in which case
 * the standard password request dialog will be used
 * @ask_pass_data: a #gpointer with user data to be passed to the
 * #GksuAskPasswordFunc
 * @pass_not_needed: a #GksuPassNotNeededFunc that will be called
 * when the command is being run without the need for requesting
 * a password; it will only be called if the display-no-pass-info
 * gconf key is enabled; NULL will have the standard dialog be shown
 * @pass_not_needed_data: a #gpointer with the user data to be passed to the
 * #GksuPasswordNotNeededFunc
 * @error: a #GError object to be filled with the error code or NULL
 *
 * This is a compatibility shim over gksu_run_fuller, which, for
 * compatibility reasons, lacks the 'exit_status' argument.
 *
 * Returns: TRUE if all went fine, FALSE if failed
 */

gboolean
gksu_run_full (GksuContext *context,
	       GksuAskPassFunc ask_pass,
	       gpointer ask_pass_data,
	       GksuPassNotNeededFunc pass_not_needed,
	       gpointer pass_not_needed_data,
	       GError **error)
{
  return gksu_run_fuller(context,
  			 ask_pass, ask_pass_data,
			 pass_not_needed, pass_not_needed_data,
			 NULL, error);
}

/**
 * gksu_run_fuller:
 * @context: a #GksuContext
 * @ask_pass: a #GksuAskPassFunc to be called when the lib determines
 * requesting a password is necessary; it may be NULL, in which case
 * the standard password request dialog will be used
 * @ask_pass_data: a #gpointer with user data to be passed to the
 * #GksuAskPasswordFunc
 * @pass_not_needed: a #GksuPassNotNeededFunc that will be called
 * when the command is being run without the need for requesting
 * a password; it will only be called if the display-no-pass-info
 * gconf key is enabled; NULL will have the standard dialog be shown
 * @pass_not_needed_data: a #gpointer with the user data to be passed to the
 * #GksuPasswordNotNeededFunc
 * @exit_status: an optional pointer to a #gint8 which will be filled with
 * the exit status of the child process
 * @error: a #GError object to be filled with the error code or NULL
 *
 * This function is a wrapper for gksu_sudo_full/gksu_su_full. It will
 * call one of them, depending on the GConf key that defines whether
 * the default behavior for gksu is su or sudo mode. This is the
 * recommended way of using the library functionality.
 *
 * Returns: TRUE if all went fine, FALSE if failed
 */
gboolean
gksu_run_fuller (GksuContext *context,
	         GksuAskPassFunc ask_pass,
	         gpointer ask_pass_data,
	         GksuPassNotNeededFunc pass_not_needed,
	         gpointer pass_not_needed_data,
		 gint8 *exit_status,
	         GError **error)
{
  GConfClient *gconf_client;
  gboolean sudo_mode;

  gconf_client = gconf_client_get_default ();
  sudo_mode = gconf_client_get_bool (gconf_client, BASE_PATH "sudo-mode",
				     NULL);
  g_object_unref (gconf_client);

  if (sudo_mode)
    return gksu_sudo_fuller (context, ask_pass, ask_pass_data,
			     pass_not_needed, pass_not_needed_data,
			     exit_status, error);

  return gksu_su_fuller (context, ask_pass, ask_pass_data,
		         pass_not_needed, pass_not_needed_data,
		         exit_status, error);
}

/**
 * gksu_run
 * @command_line: the command line that will be executed as other user
 * @error: a #GError to be set with the error condition, if an error
 * happens
 *
 * This function is a wrapper for gksu_sudo/gksu_su. It will call one
 * of them, depending on the GConf key that defines whether the default
 * behavior for gksu is su or sudo mode. This is the recommended way of
 * using the library functionality.
 *
 * Returns: FALSE if all went well, TRUE if an error happend.
 */
gboolean
gksu_run (gchar *command_line,
	  GError **error)
{
  GConfClient *gconf_client;
  gboolean sudo_mode;

  gconf_client = gconf_client_get_default ();
  sudo_mode = gconf_client_get_bool (gconf_client, BASE_PATH "sudo-mode",
				     NULL);
  g_object_unref (gconf_client);

  if (sudo_mode)
    return gksu_sudo (command_line, error);

  return gksu_su (command_line, error);
}

/**
 * gksu_ask_password_full:
 * @context: a #GksuContext
 * @prompt: a prompt different from Password:
 * @error: a #GError object to be filled with the error code or NULL
 *
 * This function uses the gksu infra-structure to request for a
 * password, but instead of passing it to su or sudo to run a command
 * it simply returns the password.
 *
 * Returns: a newly allocated string with the password;
 */
gchar*
gksu_ask_password_full (GksuContext *context, gchar *prompt,
			GError **error)
{
  gchar *ret_value = su_ask_password (context, _(prompt), NULL, error);
  if (context->sn_context)
    gksu_context_launch_complete (context);
  return ret_value;
}

/**
 * gksu_ask_password
 * @error: a #GError to be set with the error condition, if an error
 * happens
 *
 * This function uses the gksu infra-structure to request for a
 * password, but instead of passing it to su or sudo to run a command
 * it simply returns the password. This is just a convenience wrapper
 * for gksu_ask_password_full.
 *
 * Returns: a newly allocated string with the password;
 */
gchar*
gksu_ask_password (GError **error)
{
  GksuContext *context = gksu_context_new ();
  gchar* retval;

  context->user = g_strdup ("root");
  retval = gksu_ask_password_full (context, NULL, error);
  gksu_context_free (context);

  return retval;
}
