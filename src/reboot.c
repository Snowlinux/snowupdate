#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libnotify/notify.h>
#include <gio/gio.h>

#include "update-notifier.h"
#include "update.h"
#include "trayappletui.h"

static gboolean
show_notification (TrayApplet *ta)
{
	NotifyNotification *n;

	// only show once the icon is really available
	if(!tray_applet_ui_get_visible(ta))
	   return TRUE;

	n = tray_applet_ui_get_data (ta, "notification");
	if (n)
		g_object_unref (n);
	tray_applet_ui_set_data (ta, "notification", NULL);

	/* Create and show the notification */
	n = notify_notification_new(
				     _("System restart required"),
				     _("To finish updating your system, "
				       "please restart it.\n\n"
				       "Click on the notification icon for "
				       "details."),
				     GTK_STOCK_DIALOG_WARNING);
	notify_notification_set_timeout (n, 60000);
	notify_notification_show (n, NULL);
	tray_applet_ui_set_data (ta, "notification", n);

	return FALSE;
}

static gboolean
gdm_action_reboot(void)
{
  GVariant *answer;
  GDBusProxy *proxy;

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL, /* GDBusInterfaceInfo */
                                         "org.gnome.SessionManager",
                                         "/org/gnome/SessionManager",
                                         "org.gnome.SessionManager",
                                         NULL, /* GCancellable */
                                         NULL /* GError */);
  if (proxy == NULL)
     return FALSE;

  answer = g_dbus_proxy_call_sync (proxy, "RequestReboot", NULL,
                                   G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
  g_object_unref (proxy);

  if (answer == NULL)
    return FALSE;

  g_variant_unref (answer);
  return TRUE;
}

static gboolean
ck_action_reboot(void)
{
  GVariant *answer;
  GDBusProxy *proxy;

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL, /* GDBusInterfaceInfo */
                                         "org.freedesktop.ConsoleKit",
                                         "/org/freedesktop/ConsoleKit/Manager",
                                         "org.freedesktop.ConsoleKit.Manager",
                                         NULL, /* GCancellable */
                                         NULL /* GError */);
  if (proxy == NULL)
     return FALSE;

  answer = g_dbus_proxy_call_sync (proxy, "Restart", NULL,
                                   G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
  g_object_unref (proxy);

  if (answer == NULL)
    return FALSE;

  g_variant_unref (answer);
  return TRUE;

}

static void
request_reboot (void)
{
   if(!gdm_action_reboot() && !ck_action_reboot()) {
      const char *fmt, *msg, *details;
      fmt = "<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s\n";
      msg = _("Reboot failed");
      details = _("Failed to request reboot, please shutdown manually");
      GtkWidget *dlg = gtk_message_dialog_new_with_markup(NULL, 0,
							  GTK_MESSAGE_ERROR,
							  GTK_BUTTONS_CLOSE,
							  fmt, msg, details);
      gtk_dialog_run(GTK_DIALOG(dlg));
      gtk_widget_destroy(dlg);
   }
}

static void
ask_reboot_required(TrayApplet *ta, gboolean focus_on_map)
{
   GtkBuilder *builder;
   GError *error = NULL;
   GtkWidget *image, *dia;

   builder = gtk_builder_new ();
   if (!gtk_builder_add_from_file (builder, UIDIR"reboot-dialog.ui", &error)) {
      g_warning ("Couldn't load builder file: %s", error->message);
      g_error_free (error);
   }

   image = GTK_WIDGET (gtk_builder_get_object (builder, "image"));
   gtk_image_set_from_icon_name(GTK_IMAGE (image), "un-reboot", GTK_ICON_SIZE_DIALOG);

   dia = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_reboot"));

   g_object_unref (builder);

   gtk_window_set_focus_on_map(GTK_WINDOW(dia), focus_on_map);
   if (gtk_dialog_run (GTK_DIALOG(dia)) == GTK_RESPONSE_OK)
      request_reboot ();
   gtk_widget_destroy (dia);
}

static gboolean
button_release_cb (GtkWidget *widget,
		   TrayApplet *ta)
{
   ask_reboot_required(ta, TRUE);

   return TRUE;
}

static gboolean
aptdaemon_pending_transactions (void)
{
  GError *error;
  GVariant *answer;
  GDBusProxy *proxy;
  char *owner = NULL;
  const char *current = NULL;
  char **pending = NULL;

  error = NULL;
  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                         NULL, /* GDBusInterfaceInfo */
                                         "org.debian.apt",
                                         "/org/debian/apt",
                                         "org.debian.apt",
                                         NULL, /* GCancellable */
                                         &error);
  if (proxy == NULL) {
    g_debug ("Failed to open connection to bus: %s", error->message);
    g_error_free (error);
    return FALSE;
  }

  owner = g_dbus_proxy_get_name_owner (proxy);
  g_debug("aptdaemon on bus: %i", (owner != NULL));
  if (owner == NULL) {
    g_object_unref (proxy);
    g_free (owner);
    return FALSE;
  }
  g_free (owner);

  error = NULL;
  answer = g_dbus_proxy_call_sync (proxy, "GetActiveTransactions", NULL,
                                   G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  g_object_unref (proxy);

  if (answer == NULL) {
    g_debug ("error during dbus call: %s", error->message);
    g_error_free (error);
    return FALSE;
  }

  if (g_strcmp0 (g_variant_get_type_string (answer), "(sas)") != 0) {
    g_debug ("aptd answer in unexpected format: %s",
             g_variant_get_type_string (answer));
    g_variant_unref (answer);
    return FALSE;
  }

  g_variant_get (answer, "(&s^a&s)", &current, &pending);

  gboolean has_pending = FALSE;
  if ((current && strcmp(current,"") != 0) || g_strv_length(pending) > 0)
     has_pending = TRUE;

  g_free (pending);
  g_variant_unref (answer);

  return has_pending;
}

static void
do_reboot_check (TrayApplet *ta)
{
	struct stat statbuf;

	// if we are not supposed to show the reboot notification
	// just skip it 
	if(g_settings_get_boolean(ta->un->settings, SETTINGS_KEY_HIDE_REBOOT))
	   return;
	// no auto-open of this dialog 
	if(g_settings_get_boolean(ta->un->settings,
	                          SETTINGS_KEY_AUTO_LAUNCH)) {
	   g_debug ("Skipping reboot required");
	   return;
	}

	/* If the file doesn't exist, we don't need to reboot */
	if (stat (REBOOT_FILE, &statbuf)) {
		NotifyNotification *n;

		/* Hide any notification popup */
		n = tray_applet_ui_get_data (ta, "notification");
		if (n) {
			notify_notification_close (n, NULL);
			g_object_unref (n);
		}
		tray_applet_ui_destroy (ta);

		return;
	}

	/* Skip the rest if the icon is already visible */
	if (tray_applet_ui_get_visible (ta))
	   return;
	tray_applet_ui_ensure (ta);
	tray_applet_ui_set_icon(ta, "un-reboot");
	tray_applet_ui_set_single_action(ta, _("System restart required"),
					 G_CALLBACK (button_release_cb), ta);
	tray_applet_ui_set_visible (ta, TRUE);

	/* Check whether the user doesn't like notifications */
	if (g_settings_get_boolean (ta->un->settings,
	                            SETTINGS_KEY_NO_UPDATE_NOTIFICATIONS))
		return;

	/* Show the notification, after a delay so it doesn't look ugly
	 * if we've just logged in */
	g_timeout_add(5000, (GSourceFunc)(show_notification), ta);

}

gboolean
reboot_check (TrayApplet *ta)
{
   if (aptdaemon_pending_transactions())
      g_timeout_add_seconds (5, (GSourceFunc)reboot_check, ta);
   else
      do_reboot_check(ta);
   return FALSE;
}


void
reboot_tray_icon_init (TrayApplet *ta)
{
	/* Check for updates for the first time */
	reboot_check (ta);
}
