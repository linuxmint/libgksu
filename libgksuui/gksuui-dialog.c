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

#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>

#include <gconf/gconf-client.h>

#include "defines.h"
#include "../config.h"

#include "gksuui-dialog.h"

enum {
  GKSUUI_DIALOG_SUDO_MODE = 1,
};

static void
gksuui_dialog_class_init (GksuuiDialogClass *klass);

static void
gksuui_dialog_init (GksuuiDialog *gksuui_dialog);

static void
gksuui_dialog_create_gnome_keyring_ui (GksuuiDialog *gksuui_dialog);

GType
gksuui_dialog_get_type (void)
{
  static GType type = 0;

  if (type == 0)
    {
      static const GTypeInfo info =
	{
	  sizeof (GksuuiDialogClass), /* size of class */
	  NULL, /* base_init */
	  NULL, /* base_finalize */
	  (GClassInitFunc) gksuui_dialog_class_init,
	  NULL, /* class_finalize */
	  NULL, /* class_data */
	  sizeof (GksuuiDialog), /* size of object */
	  0, /* n_preallocs */
	  (GInstanceInitFunc) gksuui_dialog_init /* instance_init */
	};
      type = g_type_register_static (gtk_dialog_get_type (),
				     "GksuuiDialogType",
				     &info, 0);
    }

  return type;
}

static void
gksuui_dialog_set_property (GObject *object, guint property_id,
			    const GValue *value, GParamSpec *spec)
{
  GksuuiDialog *self = (GksuuiDialog*) object;

  switch (property_id)
    {
    case GKSUUI_DIALOG_SUDO_MODE:
      self->sudo_mode = g_value_get_boolean (value);
      if (!self->sudo_mode)
	gksuui_dialog_create_gnome_keyring_ui ((GksuuiDialog*) object);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, spec);
      break;
    }
}

static void
gksuui_dialog_get_property (GObject *object, guint property_id,
			    GValue *value, GParamSpec *spec)
{
  GksuuiDialog *self = (GksuuiDialog*) object;

  switch (property_id)
    {
    case GKSUUI_DIALOG_SUDO_MODE:
      g_value_set_boolean (value, self->sudo_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, spec);
      break;
    }
}

static void
gksuui_dialog_class_init (GksuuiDialogClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GParamSpec *spec;

  gobject_class->set_property = gksuui_dialog_set_property;
  gobject_class->get_property = gksuui_dialog_get_property;

  spec = g_param_spec_boolean ("sudo-mode",
			       "Whether we are running in sudo mode",
			       "Set/Get whether we are running in sudo mode",
			       FALSE, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, GKSUUI_DIALOG_SUDO_MODE, spec);
}

/**
 * Helper that can detect if caps lock is pressed
 */
static gboolean
is_capslock_on (void)
{
  XkbStateRec states;
  Display *dsp;

  dsp = GDK_DISPLAY ();
  if (XkbGetState (dsp, XkbUseCoreKbd, &states) != Success)
      return FALSE;

  return (states.locked_mods & LockMask) != 0;
}

static gboolean
verify_capslock_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
   GksuuiDialog *dialog = user_data;

   if (is_capslock_on ())
      gtk_label_set_markup(GTK_LABEL(dialog->label_warn_capslock),
			   _("<b>You have capslock on</b>"));
   else
      gtk_label_set_text(GTK_LABEL(dialog->label_warn_capslock),"");

   return FALSE;
}

void
set_sensitivity_cb (GtkWidget *button, gpointer data)
{
  GtkWidget *widget = (GtkWidget*)data;
  gboolean sensitive;

  sensitive = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(button));
  gtk_widget_set_sensitive (widget, sensitive);
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

  if (!strcmp (key_name, "save-keyring"))
    {
      if (toggled)
	gconf_client_set_string (gconf_client, key, "session", NULL);
      else
	gconf_client_set_string (gconf_client, key, "default", NULL);
    }
  else
    gconf_client_set_bool (gconf_client, key, toggled, NULL);

  g_object_unref (gconf_client);

  g_free (key);
}

static void
gksuui_dialog_create_gnome_keyring_ui (GksuuiDialog *dialog)
{
  /* gnome-keyring stuff */
  GtkWidget *vbox;
  GtkWidget *check_button;
  GtkWidget *alignment;
  GtkWidget *radio_vbox;
  GtkWidget *radio_session, *radio_default;

  GConfClient *gconf_client;
  gboolean remember_password;
  gchar *tmp = NULL;

  /* gnome-keyring stuff */
  gconf_client = gconf_client_get_default ();

  vbox = gtk_vbox_new (2, TRUE);
  gtk_box_pack_start (GTK_BOX(GKSUUI_DIALOG(dialog)->entry_vbox), vbox, TRUE, TRUE, 0);
  gtk_widget_show (vbox);

  check_button = gtk_check_button_new_with_label (_("Remember password"));
  g_signal_connect (G_OBJECT(check_button), "toggled", G_CALLBACK(cb_toggled_cb), "save-to-keyring");

  remember_password = gconf_client_get_bool (gconf_client, BASE_PATH"save-to-keyring", NULL);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(check_button), remember_password);
  gtk_box_pack_start (GTK_BOX(vbox), check_button, TRUE, TRUE, 0);
  gtk_widget_show (check_button);

  alignment = gtk_alignment_new (0.5, 0.5, 0.6, 1);
  gtk_box_pack_start (GTK_BOX(vbox), alignment, TRUE, TRUE, 2);
  gtk_widget_show (alignment);

  radio_vbox = gtk_vbox_new (2, TRUE);
  gtk_container_add (GTK_CONTAINER(alignment), radio_vbox);
  gtk_widget_set_sensitive (radio_vbox, remember_password);
  gtk_widget_show (radio_vbox);

  radio_session = gtk_radio_button_new_with_label (NULL, _("Save for this session"));
  g_signal_connect (G_OBJECT(radio_session), "toggled", G_CALLBACK(cb_toggled_cb), "save-keyring");
  gtk_box_pack_start (GTK_BOX(radio_vbox), radio_session, TRUE, TRUE, 0);
  gtk_widget_show (radio_session);

  radio_default = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON(radio_session), _("Save in the keyring"));
  gtk_box_pack_start (GTK_BOX(radio_vbox), radio_default, TRUE, TRUE, 0);
  gtk_widget_show (radio_default);

  g_signal_connect (G_OBJECT(check_button), "toggled", G_CALLBACK(set_sensitivity_cb), radio_vbox);

  tmp = gconf_client_get_string (gconf_client, BASE_PATH"save-keyring", NULL);
  if (tmp && (!strcmp (tmp, "default")))
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(radio_default), TRUE);
  g_free (tmp);

  g_object_unref (gconf_client);
}

static gboolean
focus_out_cb (GtkWidget *widget, GdkEventFocus *event, gpointer user_data)
{
  gtk_window_present (GTK_WINDOW(widget));
  return TRUE;
}

static void
gksuui_dialog_init (GksuuiDialog *gksuui_dialog)
{
  GtkDialog *dialog;
  GtkWidget *hbox; /* aditional hbox for 'password: entry' label */

  /*
     make sure we're using UTF-8 and getting our locale files
     from the right place
  */
  bindtextdomain(PACKAGE_NAME, LOCALEDIR);
  bind_textdomain_codeset (PACKAGE_NAME, "UTF-8");

  gtk_widget_push_composite_child ();

  /* dialog window */
  dialog = GTK_DIALOG(gksuui_dialog);

  /* make sure that our window will always have the focus */
  g_signal_connect (G_OBJECT(dialog), "focus-out-event",
		    G_CALLBACK(focus_out_cb), NULL);

  gksuui_dialog->main_vbox = dialog->vbox;

  gtk_window_set_title (GTK_WINDOW(gksuui_dialog), "");
  gtk_dialog_set_has_separator (GTK_DIALOG(gksuui_dialog), FALSE);
  gtk_container_set_border_width (GTK_CONTAINER(gksuui_dialog), 6);
  gtk_box_set_spacing (GTK_BOX(gksuui_dialog->main_vbox), 12);
  gtk_window_set_resizable (GTK_WINDOW(gksuui_dialog), FALSE);

  /* skip taskbar and  pager hint */
  gtk_window_set_skip_pager_hint (GTK_WINDOW(gksuui_dialog), TRUE);
  gtk_window_set_skip_taskbar_hint (GTK_WINDOW(gksuui_dialog), TRUE);


  /* center window */
  gtk_window_set_position (GTK_WINDOW(gksuui_dialog), GTK_WIN_POS_CENTER);

  /* the action buttons */
  /*  the cancel button  */
  gksuui_dialog->cancel_button = gtk_dialog_add_button (dialog,
						      GTK_STOCK_CANCEL,
						      GTK_RESPONSE_CANCEL);
  /*  the ok button  */
  gksuui_dialog->ok_button = gtk_dialog_add_button (dialog,
						  GTK_STOCK_OK,
						  GTK_RESPONSE_OK);
  gtk_widget_grab_default (gksuui_dialog->ok_button);


  /* hbox */
  gksuui_dialog->hbox = gtk_hbox_new (FALSE, 12);
  gtk_container_set_border_width (GTK_CONTAINER(gksuui_dialog->hbox), 6);
  gtk_box_pack_start (GTK_BOX(gksuui_dialog->main_vbox),
		      gksuui_dialog->hbox, TRUE, TRUE, 0);
  gtk_widget_show (gksuui_dialog->hbox);

  /* image */
  gksuui_dialog->image =
    gtk_image_new_from_stock (GTK_STOCK_DIALOG_AUTHENTICATION,
			      GTK_ICON_SIZE_DIALOG);
  gtk_misc_set_alignment (GTK_MISC(gksuui_dialog->image), 0.5, 0);
  gtk_box_pack_start (GTK_BOX(gksuui_dialog->hbox), gksuui_dialog->image,
		      FALSE, FALSE, 0);
  gtk_widget_show (gksuui_dialog->image);

  /* vbox for label and entry */
  gksuui_dialog->entry_vbox = gtk_vbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX(gksuui_dialog->hbox), gksuui_dialog->entry_vbox,
		      TRUE, TRUE, 0);
  gtk_widget_show (gksuui_dialog->entry_vbox);

  /* label */
  gksuui_dialog->label = gtk_label_new (_("<span weight=\"bold\" size=\"larger\">"
					  "Type the root password.</span>\n"));
  gtk_label_set_use_markup (GTK_LABEL(gksuui_dialog->label), TRUE);
  gtk_label_set_line_wrap (GTK_LABEL(gksuui_dialog->label), TRUE);
  gtk_misc_set_alignment (GTK_MISC(gksuui_dialog->label), 0.0, 0);
  gtk_box_pack_start (GTK_BOX(gksuui_dialog->entry_vbox),
		      gksuui_dialog->label, TRUE, TRUE, 0);
  gtk_widget_show (gksuui_dialog->label);

  /* alert */
  gksuui_dialog->alert = gtk_label_new (NULL);
  gtk_box_pack_start (GTK_BOX(gksuui_dialog->entry_vbox),
		      gksuui_dialog->alert, TRUE, TRUE, 0);

  /* hbox for entry and label */
  hbox = gtk_hbox_new (FALSE, 6);
  gtk_box_pack_start (GTK_BOX (gksuui_dialog->entry_vbox), hbox,
		      TRUE, TRUE, 0);
  gtk_widget_show (hbox);

  /* entry label */
  gksuui_dialog->prompt_label = gtk_label_new (_("Password:"));
  gtk_box_pack_start (GTK_BOX(hbox), gksuui_dialog->prompt_label,
		      FALSE, FALSE, 0);
  gtk_widget_show (gksuui_dialog->prompt_label);

  /* entry */
  gksuui_dialog->entry = gtk_entry_new();
  g_signal_connect (G_OBJECT(gksuui_dialog->entry), "key-press-event",
		    G_CALLBACK(verify_capslock_cb), gksuui_dialog);
  g_signal_connect_swapped (G_OBJECT(gksuui_dialog->entry), "activate",
			    G_CALLBACK(gtk_button_clicked),
			    gksuui_dialog->ok_button);
  gtk_entry_set_visibility(GTK_ENTRY(gksuui_dialog->entry), FALSE);
  if ('*' == gtk_entry_get_invisible_char(GTK_ENTRY(gksuui_dialog->entry)))
    gtk_entry_set_invisible_char(GTK_ENTRY(gksuui_dialog->entry), 0x25cf);
  gtk_box_pack_start (GTK_BOX (hbox), gksuui_dialog->entry,
		      TRUE, TRUE, 0);
  gtk_widget_show (gksuui_dialog->entry);
  gtk_widget_grab_focus(gksuui_dialog->entry);

  /* label capslock warning */
  gksuui_dialog->label_warn_capslock = gtk_label_new ("");
  gtk_widget_show (gksuui_dialog->label_warn_capslock);
  gtk_label_set_justify (GTK_LABEL(gksuui_dialog->label_warn_capslock),
			 GTK_JUSTIFY_CENTER);
  gtk_label_set_use_markup (GTK_LABEL(gksuui_dialog->label_warn_capslock), TRUE);
  gtk_box_pack_start (GTK_BOX(gksuui_dialog->entry_vbox),
		      gksuui_dialog->label_warn_capslock, TRUE, TRUE, 0);

  /* expose event */
  g_signal_connect (G_OBJECT(gksuui_dialog), "focus-in-event",
		    G_CALLBACK(verify_capslock_cb), gksuui_dialog);


  gtk_widget_pop_composite_child ();
}

/**
 * gksuui_dialog_new:
 *
 * Creates a new #GksuuiDialog.
 *
 * Returns: the new #GksuuiDialog
 */
GtkWidget*
gksuui_dialog_new (gboolean sudo_mode)
{
  return GTK_WIDGET (g_object_new (GKSUUI_TYPE_DIALOG,
				   "sudo_mode", sudo_mode,
				   NULL));
}

/**
 * gksuui_dialog_set_message:
 * @dialog: the dialog on which to set the message
 * @message: the message to be set on the dialog
 *
 * Sets the message that is displayed to the user when
 * requesting a password. You can use Pango markup to
 * modify font attributes.
 *
 */
void
gksuui_dialog_set_message (GksuuiDialog *dialog, gchar *message)
{
  GtkWidget *label = dialog->label;

  gtk_label_set_markup (GTK_LABEL(label), message);
}

/**
 * gksuui_dialog_get_message:
 * @dialog: the dialog from which to get the message
 *
 * Gets the current message that the dialog will use
 * when run.
 *
 * Returns: a pointer to the string containing the
 * message. You need to make a copy of the string to
 * keep it.
 */
const gchar*
gksuui_dialog_get_message (GksuuiDialog *dialog)
{
  GtkWidget *label = dialog->label;

  return gtk_label_get_text (GTK_LABEL(label));
}

/**
 * gksuui_dialog_set_alert:
 * @dialog: the dialog on which to set the alert
 * @alert: the alert to be set on the dialog
 *
 * Sets the alert that is displayed to the user when
 * requesting a password. You can use Pango markup to
 * modify font attributes. This alert should be used
 * to display messages like 'wrong password' on password
 * retry, for example.
 *
 */
void
gksuui_dialog_set_alert (GksuuiDialog *dialog, gchar *alert)
{
  GtkWidget *label = dialog->alert;

  gtk_label_set_markup (GTK_LABEL(label), alert);
}

/**
 * gksuui_dialog_get_alert:
 * @dialog: the dialog from which to get the alert
 *
 * Gets the current alert that the dialog will use
 * when run.
 *
 * Returns: a pointer to the string containing the
 * alert. You need to make a copy of the string to
 * keep it.
 */
const gchar*
gksuui_dialog_get_alert (GksuuiDialog *dialog)
{
  GtkWidget *label = dialog->alert;

  return gtk_label_get_text (GTK_LABEL(label));
}

/**
 * gksuui_dialog_set_icon:
 * @dialog: the dialog on which the icon will be set
 * @icon: a #GdkPixbuf from which to set the image
 *
 * Sets the icon that will be shown on the dialog. Should
 * probably not be used, as the default icon is the default
 * authorization icon.
 */
void
gksuui_dialog_set_icon (GksuuiDialog *dialog, GdkPixbuf *icon)
{
  GtkWidget *image = dialog->image;

  gtk_image_set_from_pixbuf (GTK_IMAGE(image), icon);
}

/**
 * gksuui_dialog_get_icon:
 * @dialog: the dialog from which the icon should be
 * got
 *
 * Gets the #GtkImage which is currently defined as the
 * icon for the authorization dialog.
 *
 * Returns: a #GtkWidget which is the #GtkImage
 */
GtkWidget*
gksuui_dialog_get_icon (GksuuiDialog *dialog)
{
  return dialog->image;
}

/**
 * gksuui_dialog_get_password:
 * @dialog: the dialog from which to get the message
 *
 * Gets the password typed by the user on the dialog.
 * This is a convenience function to grab the password
 * easily from the dialog after calling gtk_dialog_run ()
 *
 * Returns: a newly allocated string containing the password
 */
gchar*
gksuui_dialog_get_password (GksuuiDialog *dialog)
{
  GtkEditable *entry = GTK_EDITABLE(dialog->entry);

  return gtk_editable_get_chars (entry, 0, -1);
}

/**
 * gksuui_dialog_set_prompt:
 * @dialog: the dialog on which to set the prompt
 * @prompt: the prompt to be set on the dialog
 *
 * Sets the prompt that is displayed to the user when
 * requesting a password. You can use Pango markup to
 * modify font attributes.
 *
 */
void
gksuui_dialog_set_prompt (GksuuiDialog *dialog, gchar *prompt)
{
  GtkWidget *label = dialog->prompt_label;

  gtk_label_set_markup (GTK_LABEL(label), prompt);
}

/**
 * gksuui_dialog_get_prompt:
 * @dialog: the dialog from which to get the prompt
 *
 * Gets the current prompt that the dialog will use
 * when run.
 *
 * Returns: a pointer to the string containing the
 * prompt. You need to make a copy of the string to
 * keep it.
 */
const gchar*
gksuui_dialog_get_prompt (GksuuiDialog *dialog)
{
  GtkWidget *label = dialog->prompt_label;

  return gtk_label_get_text (GTK_LABEL(label));
}
