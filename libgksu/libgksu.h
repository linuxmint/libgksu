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

#ifndef __LIBGKSU_H__
#define __LIBGKSU_H__

#include <glib.h>
#include <glib-object.h>

#define SN_API_NOT_YET_FROZEN
#include <libsn/sn.h>

#include <gtk/gtk.h>

#include <gconf/gconf-client.h>

G_BEGIN_DECLS

typedef struct _GksuContext GksuContext;
struct _GksuContext
{
  /*
   * Protected
   */

  /* Xauth stuff */
  gchar *xauth;
  gchar *dir;
  gchar *display;

  gboolean sudo_mode;

  GConfClient *gconf_client;

  /* what to run, with whose powers */
  gchar *user;
  gchar *command;

  gboolean login_shell;
  gboolean keep_env;

  /* UI options */
  gchar *description;
  gchar *message;
  gchar *alert;
  gboolean grab;
  gboolean always_ask_password;

  /* startup notification */
  SnLauncherContext *sn_context;
  gchar *sn_id;
  
  /* ref counting */
  gint ref_count;

  gboolean debug;
};

#define GKSU_TYPE_CONTEXT gksu_context_get_type()
GType                gksu_context_get_type  (void);
GksuContext*         gksu_context_new       (void);
GksuContext*         gksu_context_ref       (GksuContext *context);
void                 gksu_context_unref     (GksuContext *context);

GType gksu_error_get_type (void);
#define GKSU_TYPE_ERROR (gksu_error_get_type ())

typedef enum
{
  GKSU_ERROR_XAUTH,
  GKSU_ERROR_HELPER,
  GKSU_ERROR_NOCOMMAND,
  GKSU_ERROR_NOPASSWORD,
  GKSU_ERROR_FORK,
  GKSU_ERROR_EXEC,
  GKSU_ERROR_PIPE,
  GKSU_ERROR_PIPEREAD,
  GKSU_ERROR_WRONGPASS,
  GKSU_ERROR_CHILDFAILED,
  GKSU_ERROR_NOT_ALLOWED,
  GKSU_ERROR_CANCELED,
  GKSU_ERROR_WRONGAUTOPASS
} GksuError;

typedef
gchar*
(*GksuAskPassFunc) (GksuContext *context, gchar *prompt,
		    gpointer user_data, GError**);

typedef
void
(*GksuPassNotNeededFunc) (GksuContext *context, gpointer user_data);

/*
   getters and setters for the configuration
   options
*/
void
gksu_context_set_user (GksuContext *context, gchar *username);

const gchar*
gksu_context_get_user (GksuContext *context);

void
gksu_context_set_command (GksuContext *context, gchar *command);

const gchar*
gksu_context_get_command (GksuContext *context);

void
gksu_context_set_login_shell (GksuContext *context, gboolean value);

gboolean
gksu_context_get_login_shell (GksuContext *context);

void
gksu_context_set_keep_env (GksuContext *context, gboolean value);

gboolean
gksu_context_get_keep_env (GksuContext *context);

void
gksu_context_set_description (GksuContext *context, gchar *description);

gchar*
gksu_context_get_description (GksuContext *context);

void
gksu_context_set_message (GksuContext *context, gchar *message);

gchar*
gksu_context_get_message (GksuContext *context);

void
gksu_context_set_alert (GksuContext *context, gchar *alert);

gchar*
gksu_context_get_alert (GksuContext *context);

void
gksu_context_set_grab (GksuContext *context, gboolean value);

gboolean
gksu_context_get_grab (GksuContext *context);

void
gksu_context_set_always_ask_password (GksuContext *context, gboolean value);

gboolean
gksu_context_get_always_ask_password (GksuContext *context);

void
gksu_context_set_launcher_context (GksuContext *context, SnLauncherContext *sn_context);

SnLauncherContext*
gksu_context_get_launcher_context (GksuContext *context);

void
gksu_context_set_debug (GksuContext *context, gboolean value);

gboolean
gksu_context_get_debug (GksuContext *context);

void
gksu_context_free (GksuContext *context);

gboolean
gksu_su_fuller (GksuContext *context,
	        GksuAskPassFunc ask_pass,
	        gpointer ask_pass_data,
	        GksuPassNotNeededFunc pass_not_needed,
	        gpointer pass_not_needed_data,
	        gint8 *exit_status,
	        GError **error);

gboolean
gksu_su_full (GksuContext *context,
	      GksuAskPassFunc ask_pass,
	      gpointer ask_pass_data,
	      GksuPassNotNeededFunc pass_not_needed,
	      gpointer pass_not_needed_data,
	      GError **error);

gboolean
gksu_su (gchar *command_line,
	 GError **error);

gboolean
gksu_sudo_fuller (GksuContext *context,
		  GksuAskPassFunc ask_pass,
		  gpointer ask_pass_data,
		  GksuPassNotNeededFunc pass_not_needed,
		  gpointer pass_not_needed_data,
		  gint8 *exit_status,
		  GError **error);

gboolean
gksu_sudo_full (GksuContext *context,
		GksuAskPassFunc ask_pass,
		gpointer ask_pass_data,
		GksuPassNotNeededFunc pass_not_needed,
		gpointer pass_not_needed_data,
		GError **error);

gboolean
gksu_sudo (gchar *command_line,
	   GError **error);

gboolean
gksu_run_fuller (GksuContext *context,
	         GksuAskPassFunc ask_pass,
	         gpointer ask_pass_data,
	         GksuPassNotNeededFunc pass_not_needed,
	         gpointer pass_not_needed_data,
	         gint8 *exit_status,
	         GError **error);

gboolean
gksu_run_full (GksuContext *context,
	       GksuAskPassFunc ask_pass,
	       gpointer ask_pass_data,
	       GksuPassNotNeededFunc pass_not_needed,
	       gpointer pass_not_needed_data,
	       GError **error);

gboolean
gksu_run (gchar *command_line,
	  GError **error);

gchar*
gksu_ask_password_full (GksuContext *context,
			gchar *prompt,
			GError **error);

gchar*
gksu_ask_password (GError **error);

G_END_DECLS

#endif
