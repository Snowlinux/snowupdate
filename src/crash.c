#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libnotify/notify.h>

#include "update-notifier.h"
#include "update.h"
#include "trayappletui.h"

static gboolean
check_system_crashes(void)
{
   int exitcode;

   if(!in_admin_group())
      return FALSE;

   // check for system crashes
   gchar *argv[] = { CRASHREPORT_HELPER, "--system", NULL };
   if(!g_spawn_sync(NULL,
                    argv,
                    NULL,
                    G_SPAWN_STDOUT_TO_DEV_NULL|G_SPAWN_STDERR_TO_DEV_NULL,
                    NULL,
                    NULL,
                    NULL,
                    NULL,
                    &exitcode,
                    NULL)) {
      g_warning("Can not run %s", CRASHREPORT_HELPER);
      return FALSE;
   }

   return exitcode == 0;
}

static gboolean
ask_invoke_apport_with_pkexec(void)
{
   GtkDialog *dialog;
   gchar *msg = _("System program problem detected");
   gchar *descr = _("Do you want to report the problem "
                    "now?");
   dialog = (GtkDialog*)gtk_message_dialog_new (NULL,
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_QUESTION,
                                    GTK_BUTTONS_NONE,
                                    "%s", msg);
   gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                            "%s", descr);
   gtk_dialog_add_button(dialog, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
   gtk_dialog_add_button(dialog, _("Report problem…"), GTK_RESPONSE_ACCEPT);
   int res = gtk_dialog_run(dialog);
   gtk_widget_destroy(GTK_WIDGET(dialog));
   if (res == GTK_RESPONSE_ACCEPT)
      return TRUE;
   return FALSE;
}

static gboolean 
run_apport(TrayApplet *ta)
{
   g_debug("fire up the crashreport tool");
   // be nice and always ask first before firing up pkexec
   if (check_system_crashes() && ask_invoke_apport_with_pkexec()) {
      invoke_with_pkexec(CRASHREPORT_REPORT_APP);
   } else {
      return g_spawn_command_line_async(CRASHREPORT_REPORT_APP, NULL);
   }
   return TRUE;
}

static gboolean
show_notification (TrayApplet *ta)
{
   NotifyNotification *n;

   // check if the update-icon is still visible (in the delay time a 
   // update may already have been performed)
   if(!tray_applet_ui_get_visible(ta))
      return FALSE;

   n = tray_applet_ui_get_data (ta, "notification");
   if (n)
      g_object_unref (n);
   tray_applet_ui_set_data (ta, "notification", NULL);

   // now show a notification handle 
   n = notify_notification_new(
				     _("Crash report detected"),
				     _("An application has crashed on your "
				       "system (now or in the past). "
				       "Click on the notification icon to "
				       "display details. "
				       ),
				     GTK_STOCK_DIALOG_INFO);
   notify_notification_set_timeout (n, 60000);
   notify_notification_show (n, NULL);
   tray_applet_ui_set_data (ta, "notification", n);

   return FALSE;
}

static void 
hide_crash_applet(TrayApplet *ta)
{
   NotifyNotification *n;
   
   /* Hide any notification popup */
   n = tray_applet_ui_get_data (ta, "notification");
   if (n) {
      notify_notification_close (n, NULL);
      g_object_unref (n);
   }
   tray_applet_ui_destroy (ta);
}

static gboolean
button_release_cb (GtkWidget *widget,
		   TrayApplet *ta)
{
   run_apport(ta);
   hide_crash_applet(ta);
   return TRUE;
}

gboolean
crashreport_check (TrayApplet *ta)
{
   int crashreports_found = 0;
   gboolean system_crashes;
   g_debug("crashreport_check");

   // don't do anything if no apport-gtk is installed
   if(!g_file_test(CRASHREPORT_REPORT_APP, G_FILE_TEST_IS_EXECUTABLE))
      return FALSE;

   // Check whether the user doesn't want notifications
   if (!g_settings_get_boolean (ta->un->settings,
                                SETTINGS_KEY_APPORT_NOTIFICATIONS)) {
      g_debug("apport notifications disabled, not displaying crashes");
      return FALSE;
   }

   // check for (new) reports by calling CRASHREPORT_HELPER
   // and checking the return code
   int exitcode;
   gchar *argv[] = { CRASHREPORT_HELPER, NULL };
   if(!g_spawn_sync(NULL,
                    argv,
                    NULL,
                    G_SPAWN_STDOUT_TO_DEV_NULL|G_SPAWN_STDERR_TO_DEV_NULL,
                    NULL,
                    NULL,
                    NULL,
                    NULL,
                    &exitcode,
                    NULL)) {
      g_warning("Can not run %s", CRASHREPORT_HELPER);
      return FALSE;
   }
   // exitcode == 0: reports found, else no reports
   system_crashes = check_system_crashes();
   crashreports_found = !exitcode || system_crashes;

   // in autolaunch mode, just open windows, 
   gboolean autolaunch = g_settings_get_boolean(ta->un->settings,
                                                SETTINGS_KEY_AUTO_LAUNCH); 
   if (autolaunch) {
      if (crashreports_found > 0) {
         g_debug("autolaunch mode, just running apport now");
         crashreports_found=0;
         run_apport(ta);
         return TRUE;
      } else
         return FALSE;
   }

   // non-autolaunch will always use the notification area
   gboolean visible = tray_applet_ui_get_visible(ta);

   // crashreport found 
   if(crashreports_found > 0 && !visible) {
      tray_applet_ui_ensure(ta);
      tray_applet_ui_set_single_action(ta, _("Crash report detected"),
                                       G_CALLBACK(button_release_cb), ta);
      tray_applet_ui_set_visible(ta, TRUE);
      /* Show the notification, after a delay so it doesn't look ugly
       * if we've just logged in */
      g_timeout_add(5000, (GSourceFunc)(show_notification), ta);
   }

   // no crashreports, but visible
   if((crashreports_found == 0) && visible) {
      hide_crash_applet(ta);
   }

   return TRUE;
}

static gboolean
crashreport_check_initially(TrayApplet *ta)
{
   crashreport_check(ta);
   // stop timeout handler
   return FALSE;
}

void
crashreport_tray_icon_init (TrayApplet *ta)
{
	/* Check for crashes for the first time */
	g_timeout_add_seconds(1, (GSourceFunc)crashreport_check_initially, ta);
}
