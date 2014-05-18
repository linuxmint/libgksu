/*
 * libgksuui -- Gtk+ widget and convenience functions for requesting passwords
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

#ifndef __GKSUUI_DIALOG_H__
#define __GKSUUI_DIALOG_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GKSUUI_TYPE_DIALOG (gksuui_dialog_get_type ())
#define GKSUUI_DIALOG(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GKSUUI_TYPE_DIALOG, GksuuiDialog))
#define GKSUUI_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GKSUUI_TYPE_DIALOG, GksuuiDialogClass))
#define GKSUUI_IS_DIALOG(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GKSUUI_TYPE_DIALOG))
#define GKSUUI_IS_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GKSUUI_TYPE_CONTEXT))
#define GKSUUI_DIALOG_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GKSUUI_TYPE_DIALOG, GksuuiDialogClass))

typedef struct _GksuuiDialogClass GksuuiDialogClass;
typedef struct _GksuuiDialog GksuuiDialog;

struct _GksuuiDialogClass
{
  GtkDialogClass parent_class;
};

/**
 * GksuuiDialog:
 * @dialog: parent widget
 * @main_vbox: GtkDialog's vbox
 * @hbox: box to separate the image of the right-side widgets
 * @image: the authorization image, left-side widget
 * @entry_vbox: right-side widgets container
 * @label: message describing what is required from the user,
 * right-side widget
 * @entry: place to type the password in, right-side widget
 * @ok_button: OK button of the dialog
 * @cancel_button: Cancel button of the dialog
 *
 * Convenience widget based on #GtkDialog to request a password.
 */
struct _GksuuiDialog
{
  GtkDialog dialog;

  GtkWidget *main_vbox;
  GtkWidget *hbox;
  GtkWidget *image;
  GtkWidget *entry_vbox;
  GtkWidget *alert;
  GtkWidget *label;
  GtkWidget *label_warn_capslock;
  GtkWidget *entry;
  GtkWidget *ok_button;
  GtkWidget *cancel_button;
  GtkWidget *prompt_label;

  /* private */
  gboolean sudo_mode;
};

GType
gksuui_dialog_get_type (void);

GtkWidget*
gksuui_dialog_new (gboolean sudo_mode);

void
gksuui_dialog_set_message (GksuuiDialog *dialog, gchar *message);

const gchar*
gksuui_dialog_get_message (GksuuiDialog *dialog);

void
gksuui_dialog_set_alert (GksuuiDialog *dialog, gchar *alert);

const gchar*
gksuui_dialog_get_alert (GksuuiDialog *dialog);

void
gksuui_dialog_set_icon (GksuuiDialog *dialog, GdkPixbuf *icon);

GtkWidget*
gksuui_dialog_get_icon (GksuuiDialog *dialog);

gchar*
gksuui_dialog_get_password (GksuuiDialog *dialog);

void
gksuui_dialog_set_prompt (GksuuiDialog *dialog, gchar *prompt);

const gchar*
gksuui_dialog_get_prompt (GksuuiDialog *dialog);

G_END_DECLS

#endif
