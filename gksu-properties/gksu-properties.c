#include <locale.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gconf/gconf-client.h>

#include "../config.h"
#include "../libgksu/defines.h"

static GtkBuilder *gui = NULL;
static GtkWidget *main_window;
static GtkWidget *grab_combo;
static GtkWidget *mode_combo;
static GConfClient *gconf_client; /* NULL */

gboolean disable_grab = FALSE;
gboolean force_grab = FALSE;
gboolean prompt = FALSE;
gboolean sudo_mode = FALSE;

void
update_from_gconf ()
{
  disable_grab = gconf_client_get_bool (gconf_client, BASE_PATH "disable-grab",
					NULL);

  force_grab = gconf_client_get_bool (gconf_client, BASE_PATH "force-grab",
				      NULL);

  prompt = gconf_client_get_bool (gconf_client, BASE_PATH "prompt",
				  NULL);

  sudo_mode = gconf_client_get_bool (gconf_client, BASE_PATH "sudo-mode",
				     NULL);
}

const gchar*
get_grab_string ()
{
  if (prompt)
    return "prompt";

  if (force_grab)
    return "force enable";

  if (disable_grab)
    return "disable";

  return "enable";
}

void
update_grab_combo ()
{
  GtkTreeModel *model;
  GtkTreeIter iter;

  const gchar *tmp = NULL;
  gchar *buffer = NULL;
  gboolean found = FALSE;

  model = gtk_combo_box_get_model (GTK_COMBO_BOX (grab_combo));
  gtk_tree_model_get_iter_first (model, &iter);
  tmp = get_grab_string ();
  do
    {
      gtk_tree_model_get (model, &iter, 0, &buffer, -1);
      if (!strcmp (tmp, buffer))
	{
	  g_free (buffer);
	  found = TRUE;
	  break;
	}
      g_free (buffer);
    }  while (gtk_tree_model_iter_next (model, &iter));

  if (found)
    gtk_combo_box_set_active_iter (GTK_COMBO_BOX (grab_combo), &iter);
}

void
update_mode_combo ()
{
  GtkTreeModel *model;
  GtkTreeIter iter;

  const gchar *tmp = NULL;
  gchar *buffer = NULL;
  gboolean found = FALSE;

  model = gtk_combo_box_get_model (GTK_COMBO_BOX (mode_combo));
  gtk_tree_model_get_iter_first (model, &iter);
  if (sudo_mode)
    tmp = "sudo";
  else
    tmp = "su";
  do
    {
      gtk_tree_model_get (model, &iter, 0, &buffer, -1);
      if (!strcmp (tmp, buffer))
	{
	  g_free (buffer);
	  found = TRUE;
	  break;
	}
      g_free (buffer);
    }  while (gtk_tree_model_iter_next (model, &iter));

  if (found)
    gtk_combo_box_set_active_iter (GTK_COMBO_BOX (mode_combo), &iter);
}

void
create_dialog ()
{
  GdkPixbuf *icon;
  main_window = GTK_WIDGET (gtk_builder_get_object (gui, "main_window"));
  icon = gdk_pixbuf_new_from_file (DATA_DIR"/pixmaps/gksu.png", NULL);
  if (icon)
    gtk_window_set_icon (GTK_WINDOW(main_window), icon);
  else
    g_warning ("Error loading window icon %s",
	       DATA_DIR "/pixmaps/gksu.png\n");

  grab_combo = GTK_WIDGET (gtk_builder_get_object (gui, "grab_combo"));
  update_grab_combo ();

  mode_combo = GTK_WIDGET (gtk_builder_get_object (gui, "mode_combo"));
  update_mode_combo ();

  gtk_widget_show_all (main_window);
}

void
gconf_change_cb (GConfClient *gconf_client, guint cnxn_id,
		 GConfEntry *entry, gpointer data)
{
  update_from_gconf ();
  update_mode_combo ();
  update_grab_combo ();
}

void
combo_change_cb (GtkWidget *widget, gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkComboBox *combo_box = GTK_COMBO_BOX (widget);
  gchar *combo_name = (gchar*)data;

  gchar *buffer = NULL;

  model = gtk_combo_box_get_model (combo_box);
  if (gtk_combo_box_get_active_iter (combo_box, &iter))
    {
      gtk_tree_model_get (model, &iter, 0, &buffer, -1);
      if (!strcmp (combo_name, "mode"))
	{
	  if (!strcmp (buffer, "sudo"))
	    gconf_client_set_bool (gconf_client, BASE_PATH "sudo-mode",
				   TRUE, NULL);
	  else
	    gconf_client_set_bool (gconf_client, BASE_PATH "sudo-mode",
				   FALSE, NULL);
	}
      else
	{
	  if (!strcmp (buffer, "enable"))
	    {
	      gconf_client_set_bool (gconf_client, BASE_PATH "prompt",
				     FALSE, NULL);
	      gconf_client_set_bool (gconf_client, BASE_PATH "disable-grab",
				     FALSE, NULL);
	      gconf_client_set_bool (gconf_client, BASE_PATH "force-grab",
				     FALSE, NULL);
	    }
	  else if (!strcmp (buffer, "disable"))
	    {
	      gconf_client_set_bool (gconf_client, BASE_PATH "prompt",
				     FALSE, NULL);
	      gconf_client_set_bool (gconf_client, BASE_PATH "disable-grab",
				     TRUE, NULL);
	      gconf_client_set_bool (gconf_client, BASE_PATH "force-grab",
				     FALSE, NULL);
	    }
	  else if (!strcmp (buffer, "prompt"))
	    {
	      gconf_client_set_bool (gconf_client, BASE_PATH "prompt",
				     TRUE, NULL);
	      gconf_client_set_bool (gconf_client, BASE_PATH "disable-grab",
				     FALSE, NULL);
	      gconf_client_set_bool (gconf_client, BASE_PATH "force-grab",
				     FALSE, NULL);
	    }
	  else if (!strcmp (buffer, "force enable"))
	    {
	      gconf_client_set_bool (gconf_client, BASE_PATH "prompt",
				     FALSE, NULL);
	      gconf_client_set_bool (gconf_client, BASE_PATH "disable-grab",
				     FALSE, NULL);
	      gconf_client_set_bool (gconf_client, BASE_PATH "force-grab",
				     TRUE, NULL);
	    }
	}
    }
}

void
setup_notifications ()
{
  gconf_client_notify_add (gconf_client, GCONF_DIR, gconf_change_cb,
			   NULL, NULL, NULL);

  g_signal_connect (G_OBJECT(mode_combo), "changed",
		    G_CALLBACK(combo_change_cb), "mode");

  g_signal_connect (G_OBJECT(grab_combo), "changed",
		    G_CALLBACK(combo_change_cb), "grab");
}

int
main (int argc, char **argv)
{
  GError* error = NULL;

  bindtextdomain (PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (PACKAGE, "UTF-8");
  textdomain (PACKAGE);

  gtk_init (&argc, &argv);

  if (g_file_test ("gksu-properties.ui", G_FILE_TEST_EXISTS) == TRUE)
    {
      gui = gtk_builder_new ();
      if (!gtk_builder_add_from_file (gui, "gksu-properties.ui", &error))
        {
          g_warning ("Couldn't load builder file: %s", error->message);
          g_error_free (error);
        }
    }
  else if (g_file_test (PREFIX "/share/" PACKAGE "/gksu-properties.ui",
			G_FILE_TEST_EXISTS) == TRUE)
    {
      gui = gtk_builder_new ();
      if (!gtk_builder_add_from_file (gui, PREFIX "/share/" PACKAGE "/gksu-properties.ui", &error))
        {
          g_warning ("Couldn't load builder file: %s", error->message);
          g_error_free (error);
        }
    }

  if (!gui) {
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new (NULL,
				     0,
				     GTK_MESSAGE_ERROR,
				     GTK_BUTTONS_CLOSE,
				     _("Failed to load gtkui file; please check your installation."));

    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);

    return 1;
  }

  gtk_builder_connect_signals (gui, NULL);

  gconf_client = gconf_client_get_default ();

  update_from_gconf ();
  create_dialog ();
  setup_notifications ();

  if (main_window)
    gtk_main ();

  g_object_unref (gconf_client);

  return 0;
}
