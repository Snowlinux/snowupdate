
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <glib.h>
#include <glib-object.h>

#include <sys/types.h>
#include <sys/wait.h>

#include "update-notifier.h"
#include "cdroms.h"

#define CDROM_CHECKER PACKAGE_LIB_DIR"/update-notifier/apt-cdrom-check"

/* reposonses for the dialog */
enum {
   RES_START_PM=1,
   RES_DIST_UPGRADER=2,
   RES_APTONCD=3
};

/* Returnvalues from apt-cdrom-check:
    # 0 - no ubuntu CD
    # 1 - CD with packages
    # 2 - dist-upgrader CD
    # 3 - aptoncd media
*/
enum {
   NO_CD,
   CD_WITH_PACKAGES,
   CD_WITH_DISTUPGRADER,
   CD_WITH_APTONCD
};

static void
distro_cd_detected(UpgradeNotifier *un,
                   int cdtype,
                   const char *mount_point)
{
   GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
					      GTK_MESSAGE_QUESTION, 
					      GTK_BUTTONS_NONE,
					      NULL );
   gchar *title, *markup;
   switch(cdtype) {
   case CD_WITH_PACKAGES:
      title = _("Software Packages Volume Detected");
      markup = _("<span weight=\"bold\" size=\"larger\">"
	    "A volume with software packages has "
	    "been detected.</span>\n\n"
	    "Would you like to open it with the "
	    "package manager?");
      gtk_dialog_add_buttons(GTK_DIALOG(dialog), 
			     GTK_STOCK_CANCEL,
			     GTK_RESPONSE_REJECT,
			     _("Start Package Manager"), 
			     RES_START_PM,
			     NULL);
      gtk_dialog_set_default_response (GTK_DIALOG(dialog), RES_START_PM);
      break;
   case CD_WITH_DISTUPGRADER:
      title = _("Upgrade volume detected");
      markup = _("<span weight=\"bold\" size=\"larger\">"
	    "A distribution volume with software packages has "
	    "been detected.</span>\n\n"
	    "Would you like to try to upgrade from it automatically? ");
      gtk_dialog_add_buttons(GTK_DIALOG(dialog), 
			     GTK_STOCK_CANCEL,
			     GTK_RESPONSE_REJECT,
			     _("Run upgrade"), 
			     RES_DIST_UPGRADER,
			     NULL);
      gtk_dialog_set_default_response (GTK_DIALOG(dialog), RES_DIST_UPGRADER);
      break;
#if 0  //  we don't have aptoncd support currently, g-a-i is not
       // in the archive anymore
   case CD_WITH_APTONCD:
      title = _("APTonCD volume detected");
      markup = _("<span weight=\"bold\" size=\"larger\">"
	    "A volume with unofficial software packages has "
	    "been detected.</span>\n\n"
	    "Would you like to open it with the "
	    "package manager?");
      gtk_dialog_add_buttons(GTK_DIALOG(dialog), 
			     GTK_STOCK_CANCEL,
			     GTK_RESPONSE_REJECT,
			     _("Start package manager"), 
			     RES_START_PM,
			     NULL);
      gtk_dialog_set_default_response (GTK_DIALOG(dialog), RES_START_PM);
      break;      
#endif
   default:
      g_assert_not_reached();
   }

   gtk_window_set_title(GTK_WINDOW(dialog), title);
   gtk_window_set_skip_taskbar_hint (GTK_WINDOW(dialog), FALSE);
   gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), markup);

   int res = gtk_dialog_run (GTK_DIALOG (dialog));
   char *cmd = NULL;
   switch(res) {
   gchar *argv[5];
   case RES_START_PM:
      argv[0] = "/usr/lib/update-notifier/backend_helper.py";
      argv[1] = "add_cdrom";
      argv[2] = (gchar *)mount_point;
      argv[3] = NULL;
      g_spawn_async (NULL, argv, NULL, 0, NULL, NULL, NULL, NULL);
      break;
   case RES_DIST_UPGRADER:
      argv[0] = "/usr/lib/update-notifier/cddistupgrader";
      argv[1] = (gchar *)mount_point;
      argv[2] = NULL;
      g_spawn_async (NULL, argv, NULL, 0, NULL, NULL, NULL, NULL);
      break;
   default:
      /* do nothing */
      break;
   }
   g_free(cmd);
   gtk_widget_destroy (dialog);
}

static void 
check_mount_point_for_packages (const char *mount_point, gpointer data)
{
   if (!mount_point)
      return;
   //g_print("checking mount point %s\n", p);

   char *ubuntu_dir = g_strdup_printf("%s/ubuntu",mount_point);
   char *cdromupgrade = g_strdup_printf("%s/cdromupgrade",mount_point);
   char *aptoncd_file = g_strdup_printf("%s/aptoncd.info",mount_point);
   if(! (g_file_test (ubuntu_dir, G_FILE_TEST_IS_SYMLINK) ||
	 g_file_test (cdromupgrade, G_FILE_TEST_EXISTS) ||
	 g_file_test (aptoncd_file, G_FILE_TEST_IS_REGULAR) )) {
      g_free(ubuntu_dir);
      g_free(cdromupgrade);
      g_free(aptoncd_file);
      return;
   }
   g_free(ubuntu_dir);
   g_free(cdromupgrade);
   g_free(aptoncd_file);

   /* this looks like a ubuntu CD, run the checker script to verify
    * this. We expect the following return codes:
    # 0 - no ubuntu CD
    # 1 - CD with packages 
    # 2 - dist-upgrader CD
    # 3 - aptoncd media
    * (see data/apt-cdrom-check)
    */
   //g_print("this looks like a ubuntu-cdrom\n");
   char *cmd = g_strdup_printf(CDROM_CHECKER" '%s'",mount_point);
   int retval=-1;
   g_spawn_command_line_sync(cmd, NULL, NULL,  &retval, NULL);
   
   //g_print("retval: %i \n", WEXITSTATUS(retval));
   int cdtype = WEXITSTATUS(retval);
   if(cdtype > 0) {
      distro_cd_detected(data, cdtype, mount_point);
   }

   g_free(cmd);
}

static void
check_one_mount (GVolumeMonitor *monitor, GMount *mount, gpointer data)
{
    if (!g_mount_can_eject(mount))
         return;

    GFile *root = g_mount_get_root(mount);
    gchar *p = g_file_get_path(root);
    check_mount_point_for_packages(p, data);
    g_free(p);
    g_object_unref(root);
}

static void
check_all_mounts (GVolumeMonitor *monitor, gpointer data)
{
    GList *mounts = g_volume_monitor_get_mounts(monitor);
    GList *iter;

    for (iter = mounts; iter; iter = iter->next) {
        check_one_mount(monitor, mounts->data, data);
        g_object_unref(mounts->data);
    }
    g_list_free(mounts);
}

gboolean
cdroms_init (UpgradeNotifier *un)
{
    static GVolumeMonitor *monitor = NULL;
    if (monitor != NULL)
        return TRUE;

    monitor = g_volume_monitor_get();
    if (monitor == NULL)
        return FALSE;

    g_signal_connect(monitor, "mount-added", (GCallback)check_one_mount, un);
    g_signal_connect(monitor, "mount-changed", (GCallback)check_one_mount, un);
    check_all_mounts(monitor, un);

    return TRUE;
}

