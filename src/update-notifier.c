/* update-notifier.c
 * Copyright (C) 2004 Lukas Lipka <lukas@pmad.net>
 *           (C) 2004 Michael Vogt <mvo@debian.org>
 *           (C) 2004 Michiel Sikkes <michiel@eyesopened.nl>
 *           (C) 2004-2009 Canonical
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor
 * Boston, MA  02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <signal.h>
#include <grp.h>
#include <pwd.h>
#include <limits.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib-unix.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gio/gio.h>

#include "update-notifier.h"
#include "update.h"
#include "hooks.h"
#include "cdroms.h"
#include "reboot.h"
#include "uevent.h"
#include "crash.h"
#include "avahi.h"
#include "release.h"
#include "trayappletui.h"

/* some prototypes */
extern gboolean up_get_clipboard (void);
gboolean update_timer_finished(gpointer data);

// the time when we check for fam events
#define TIMEOUT_FAM 1000*5 /* 5 sec */

// the timeout (in msec) for apt-get update (changes in 
// /var/lib/apt/lists{/partial})
#define TIMEOUT_APT_GET_UPDATE 1000*30 /* 30 sec */

// the timeout (in sec) when a further activity from dpkg/apt
// causes the applet to "ungray"
#define TIMEOUT_APT_RUN 600 /* 600 sec */

// force startup even if the user is not in the sudo/admin group
static gboolean FORCE_START=FALSE;

// force pkexec for all menu actions
static gboolean FORCE_PKEXEC=FALSE;

// delay on startup (to make session startup faster)
static int STARTUP_DELAY=1;

// make the release upgrader check for a devel release
gboolean DEVEL_RELEASE=FALSE;

// force release check
static gboolean FORCE_RELEASE_CHECK=FALSE;

// global debug options
static gboolean HOOK_DEBUG = FALSE;
static gboolean UPDATE_DEBUG = FALSE;
static gboolean INOTIFY_DEBUG = FALSE;
static gboolean UEVENT_DEBUG = FALSE;
static gboolean RELEASE_DEBUG = FALSE;
static gboolean MISC_DEBUG = FALSE;

// logging stuff
static void debug_log_handler (const gchar   *log_domain,
                              GLogLevelFlags log_level,
                              const gchar   *message,
                              gpointer       user_data)
{
   if (log_domain && strcmp(log_domain, "hooks") == 0) {
      if (HOOK_DEBUG)
         g_log_default_handler (log_domain, log_level, message, user_data);
   }
   else if (log_domain && strcmp(log_domain, "update") == 0) {
      if (UPDATE_DEBUG)
         g_log_default_handler (log_domain, log_level, message, user_data);
   }
   else if (log_domain && strcmp(log_domain, "inotify") == 0) {
      if (INOTIFY_DEBUG) 
          g_log_default_handler (log_domain, log_level, message, user_data);
   }
   else if (log_domain && strcmp(log_domain, "uevent") == 0) {
      if (UEVENT_DEBUG)
         g_log_default_handler (log_domain, log_level, message, user_data);
   }
   else if (log_domain && strcmp(log_domain, "release") == 0) {
      if (RELEASE_DEBUG)
         g_log_default_handler (log_domain, log_level, message, user_data);
   }
   else if (MISC_DEBUG)
      g_log_default_handler (log_domain, log_level, message, user_data);
}

static inline void
g_debug_inotify(const char *msg, ...)
{
   va_list va;
   va_start(va, msg);
   g_logv("inotify",G_LOG_LEVEL_DEBUG, msg, va);
   va_end(va);
}

// launch async scripts one after another
static void start_next_script(GPid pid,
                              gint status,
                              GSList *remaining_scripts_to_run)
{
    if(remaining_scripts_to_run) {
        gboolean     ret = False;
        gchar*       full_path = remaining_scripts_to_run->data;
        gchar       *argv[] = { full_path, NULL };

        g_debug_inotify ("executing: %s", full_path);
        if (g_file_test(full_path, G_FILE_TEST_IS_EXECUTABLE ))
            ret = g_spawn_async("/", argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, NULL);
        remaining_scripts_to_run = g_slist_remove(remaining_scripts_to_run, full_path);
        g_free(full_path);
        if(!ret)
        {
            g_warning("script execution failed or not executable");
            start_next_script(0, 0, remaining_scripts_to_run);
            return;
        }
        g_child_watch_add(pid, (GChildWatchFunc)start_next_script, remaining_scripts_to_run);
    }

}

void invoke(const gchar *cmd, const gchar *desktop, gboolean with_pkexec)
{
   GdkAppLaunchContext *context;
   GAppInfo *appinfo;
   GError *error = NULL;
   static GtkWidget *w = NULL;

   // pkexec
   if(with_pkexec || FORCE_PKEXEC) {
      invoke_with_pkexec(cmd);
      return;
   }

   // fake window to get the current server time *urgh*
   if (!w) {
      w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      gtk_widget_realize (w);
   }

   // normal launch
   context = gdk_display_get_app_launch_context (gdk_display_get_default ());
   guint32 timestamp =  gdk_x11_get_server_time (gtk_widget_get_window (w));
   appinfo = g_app_info_create_from_commandline(cmd, 
						cmd, 
						G_APP_INFO_CREATE_NONE,
						&error);
   gdk_app_launch_context_set_timestamp (context, timestamp);
   if (!g_app_info_launch (appinfo, NULL, (GAppLaunchContext*)context, &error))
      g_warning ("Launching failed: %s", error->message);
   g_object_unref (context);
   g_object_unref (appinfo);

}

void
invoke_with_pkexec(const gchar *cmd)
{
        g_debug("invoke_with_pkexec ()");
        gchar *argv[3];
        argv[0] = "/usr/bin/pkexec";
        argv[1] = (gchar*)cmd;
        argv[2] = NULL;
        g_spawn_async (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, NULL, NULL);
}


static gboolean
trayapplet_create (TrayApplet *ta, UpgradeNotifier *un, char *name)
{
        g_debug("trayapplet_create()");

	ta->un = un;
	ta->name = name;

	return TRUE;
}

/* 
 the following files change:
 on "install":
  - /var/lib/dpkg/lock
  - /var/lib/dpkg/ *
  - /var/lib/update-notifier/dpkg-run-stamp
 on "update":
  - /var/lib/apt/lists/lock
  - /var/lib/apt/lists/ *
  - /var/lib/dpkg/lock
  - /var/lib/apt/periodic/update-success-stamp
*/
static void
monitor_cb(GFileMonitor *handle,
	   GFile *monitor_f,
	   GFile *other_monitor_f,
	   GFileMonitorEvent event_type,
	   gpointer user_data)
{
   UpgradeNotifier *un = (UpgradeNotifier*)user_data;

   gchar *info_uri = g_file_get_path(monitor_f);
#if 0
   g_debug_inotify("info_uri: %s", info_uri);
   g_debug_inotify("event_type: %i",event_type);
#endif

   // we ignore lock file events because we can only get 
   // when a lock was taken, but not when it was removed
   if(g_str_has_suffix(info_uri, "lock"))
      return;

   // look for apt-get install/update
   if(g_str_has_prefix(info_uri,"/var/lib/apt/") 
      || g_str_has_prefix(info_uri,"/var/cache/apt/")
      || strcmp(info_uri,"/var/lib/dpkg/status") == 0) {
      g_debug_inotify("apt_get_running=TRUE");
      un->apt_get_running=TRUE;
   } 
   if(strstr(info_uri, "/var/lib/update-notifier/dpkg-run-stamp") ||
      strstr(info_uri, "/var/lib/apt/periodic/update-success-stamp")) {
      g_debug_inotify("dpkg_was_run=TRUE");
      un->dpkg_was_run = TRUE;
   } 
   if(strstr(info_uri, REBOOT_FILE)) {
      g_debug_inotify("reboot required");
      un->reboot_pending = TRUE;
   }
   if(strstr(info_uri, HOOKS_DIR)) {
      g_debug_inotify("new hook!");
      un->hook_pending = TRUE;
   }
   if(strstr(info_uri, CRASHREPORT_DIR)) {
      g_debug_inotify("crashreport found");
      un->crashreport_pending = TRUE;
   }
   if(strstr(info_uri, UNICAST_LOCAL_AVAHI_FILE)) {
      g_debug_inotify("avahi disabled due to unicast .local domain");
      un->unicast_local_avahi_pending = TRUE;
   }
}

/*
 * We periodically check here what actions happened in this "time-slice".
 * This can be:
 * - dpkg_was_run=TRUE: set when apt wrote the "dpkg-run-stamp" file
 * - apt_get_running: set when apt/dpkg activity is detected (in the
 *                    lists-dir, archive-cache, or /var/lib/dpkg/status)
 * - hook_pending: we have new upgrade hoook information
 * - reboot_pending: we need to reboot
 * - crashreport_pending: we have a new crashreport
 * - unicast_local_avahi_pending: avahi got disabled due to a unicast .local domain
 *
 */
static gboolean
file_monitor_periodic_check(gpointer data)
{
   UpgradeNotifier *un = (UpgradeNotifier *)data;

   // we are not ready yet, wait for the next timeslice
   if((un->reboot == NULL) || (un->crashreport == NULL))
      return TRUE;

   // DPkg::Post-Invoke has written a stamp file, that means an install/remove
   // operation finished, we can show hooks/reboot notifications then
   if(un->dpkg_was_run) {

      // check updates
      update_check(un->update);

      // run cache-changed plugin scripts
      GDir *dir = g_dir_open(CACHE_CHANGED_PLUGIN_DIR, 0, NULL);
      const gchar *fname, *full_path;
      GSList *cache_changed_scripts = NULL;
      while ( (fname = g_dir_read_name(dir)) != NULL ) {
        full_path = g_build_filename(CACHE_CHANGED_PLUGIN_DIR, fname, NULL);
        cache_changed_scripts = g_slist_insert_sorted(cache_changed_scripts,
                                 (gpointer)full_path, (GCompareFunc)g_strcmp0);
      }
      g_dir_close(dir);
      // launch first script and queue others
      start_next_script(0, 0, cache_changed_scripts);

      // any apt-get update must be finished, otherwise
      // apt-get install wouldn't be finished
      update_apt_is_running(un->update, FALSE);
      if(un->update_finished_timer > 0) 
	 g_source_remove(un->update_finished_timer);
      
      if(un->reboot_pending) {
	 //g_print("checking reboot now\n");
	 reboot_check (un->reboot);
	 un->reboot_pending = FALSE;
      }

      // apt must be finished when a new stamp file was written, so we
      // reset the apt_get_running time-slice field because its not
      // important anymore (it finished running)
      //
      // This may leave a 5s race condition when a apt-get install finished
      // and something new (apt-get) was started
      un->apt_get_running = FALSE;
      un->last_apt_action = 0;
   }

   // show pending hooks
   if(un->hook_pending) {
      //g_print("checking hooks now\n");
      check_update_hooks(un->hook);
      un->hook_pending = FALSE;
   }

   // apt-get update/install or dpkg is running (and updates files in
   // its list/cache dir) or in /var/lib/dpkg/status
   if(un->apt_get_running) 
      update_apt_is_running(un->update, TRUE);

   // update time information for apt/dpkg
   time_t now = time(NULL);
   if(un->apt_get_running) 
      un->last_apt_action = now;

   // no apt operation for a long time
   if(un->last_apt_action > 0 &&
      (now - un->last_apt_action) >  TIMEOUT_APT_RUN) {
      update_apt_is_running(un->update, FALSE);
      update_check(un->update);
      un->last_apt_action = 0;
   }

   if(un->crashreport_pending) {
      g_debug("checking for valid crashreport now");
      crashreport_check (un->crashreport);
      un->crashreport_pending = FALSE;
   }

   if(un->unicast_local_avahi_pending) {
      g_debug("checking for disabled avahi due to unicast .local domain now");
      avahi_disabled_check ();
      un->unicast_local_avahi_pending = FALSE;
   }

   // reset the bitfields (for the next "time-slice")
   un->dpkg_was_run = FALSE;
   un->apt_get_running = FALSE;

   return TRUE;
}




// FIXME: get the apt directories with apt-config or something
static gboolean 
monitor_init(UpgradeNotifier *un)
{
   int i;
   GFileMonitor *monitor_handle;

   // monitor these dirs
   static const char *monitor_dirs[] = { 
      "/var/lib/apt/lists/", "/var/lib/apt/lists/partial/", 
      "/var/cache/apt/archives/", "/var/cache/apt/archives/partial/", 
      HOOKS_DIR, 
      CRASHREPORT_DIR,
      NULL};
   for(i=0;monitor_dirs[i] != NULL;i++) {
      GError *error = NULL;
      GFile *gf = g_file_new_for_path(monitor_dirs[i]);
      monitor_handle = g_file_monitor_directory(gf, 0, NULL, &error);
      if(monitor_handle)
	 g_signal_connect(monitor_handle, "changed", (GCallback)monitor_cb, un);
      else
	 g_warning("can not add '%s'", monitor_dirs[i]);
   }

   // and those files
   static const char *monitor_files[] = { 
      "/var/lib/dpkg/status", 
      "/var/lib/update-notifier/dpkg-run-stamp", 
      "/var/lib/apt/periodic/update-success-stamp",
      REBOOT_FILE,
      UNICAST_LOCAL_AVAHI_FILE,
      NULL};
   for(i=0;monitor_files[i] != NULL;i++) {
      GError *error = NULL;
      GFile *gf = g_file_new_for_path(monitor_files[i]);
      monitor_handle = g_file_monitor_file(gf, 0, NULL, &error);
      if(monitor_handle)
	 g_signal_connect(monitor_handle, "changed", (GCallback)monitor_cb, un);
      else
	 g_warning("can not add '%s'", monitor_dirs[i]);
   }

   g_timeout_add (TIMEOUT_FAM, (GSourceFunc)file_monitor_periodic_check, un);


   return TRUE;
}




static gboolean
tray_icons_init(UpgradeNotifier *un)
{
   //g_debug_inotify("tray_icons_init");

   /* new updates tray icon */
   un->update = g_new0 (TrayApplet, 1);

   // check if the updates icon should be displayed
   if (in_admin_group() || FORCE_START) {
      trayapplet_create(un->update, un, "software-update-available");
      update_tray_icon_init(un->update);
   } else 
      un->update = NULL;

   /* update hook icon*/
   un->hook = g_new0 (TrayApplet, 1);
   trayapplet_create(un->hook, un, "hook-notifier");
   hook_tray_icon_init(un->hook);

   /* reboot required icon */
   un->reboot = g_new0 (TrayApplet, 1);
   trayapplet_create(un->reboot, un, "un-reboot");
   reboot_tray_icon_init(un->reboot);

   /* crashreport detected icon */
   un->crashreport = g_new0 (TrayApplet, 1);
   trayapplet_create(un->crashreport, un, "apport");
   crashreport_tray_icon_init(un->crashreport);

   return FALSE; // for the tray_destroyed_cb
}

static gboolean
system_user(UpgradeNotifier *un)
{
   /* do not start for system users, e.g. the guest session 
    * (see /usr/share/gdm/guest-session/guest-session-setup.sh)
    */
   int end_system_uid = 500;
   int i = g_settings_get_int(un->settings, SETTINGS_KEY_END_SYSTEM_UIDS);
   if (i>0)
      end_system_uid = i;

   uid_t uid = getuid();
   if (uid < end_system_uid)
      return TRUE;
   return FALSE;
}

// this function checks if the user is in the given group; return 1 if user is
// a member, 0 if not; return 2 if group does not exist.
static int
user_in_group(const char* grpname)
{
   int i, ng = 0;
   gid_t *groups = NULL;

   struct group *grp = getgrnam(grpname);
   if(grp == NULL) 
      return 2;

   ng = getgroups (0, NULL);
   groups = (gid_t *) malloc (ng * sizeof (gid_t));

   i = getgroups (ng, groups);
   if (i != ng) {
     free (groups);
     return 1;
   }
   
   for(i=0;i<ng;i++) {
      if(groups[i] == grp->gr_gid) {
        free(groups);
        return 1;
      }
   }
   
   if(groups != NULL)
      free(groups);

   return 0;
}

gboolean
in_admin_group(void)
{
   int is_admin, is_sudo;

   // we consider the user an admin if the user is in the "sudo" or "admin"
   // group or neither of these groups exist
   is_admin = user_in_group("admin");
   is_sudo = user_in_group("sudo");
   return is_admin == 1 || is_sudo == 1 || (is_admin + is_sudo == 4);
}


static gboolean
sigint_cb (gpointer user_data __attribute__ ((__unused__)))
{
	gtk_main_quit ();
	return FALSE;
}


static GOptionEntry entries[] = 
{
   { "debug-hooks", 0, 0, G_OPTION_ARG_NONE, &HOOK_DEBUG, "Enable hooks debugging"},
   { "debug-updates", 0, 0, G_OPTION_ARG_NONE, &UPDATE_DEBUG, "Enable updates/autolaunch debugging"},
   { "debug-inotify", 0, 0, G_OPTION_ARG_NONE, &INOTIFY_DEBUG, "Enable inotify debugging"},
   { "debug-uevent", 0, 0, G_OPTION_ARG_NONE, &UEVENT_DEBUG, "Enable uevent debugging"},
   { "debug-new-release-check", 0, 0, G_OPTION_ARG_NONE, &RELEASE_DEBUG, "Enable new release check debugging"},
   { "debug-misc", 0, 0, G_OPTION_ARG_NONE, &MISC_DEBUG, "Enable uncategorized debug prints"},
   { "force", 0, 0, G_OPTION_ARG_NONE, &FORCE_START, "Force start even if the user is not in the admin group"},
   { "force-use-pkexec", 0, 0, G_OPTION_ARG_NONE, &FORCE_PKEXEC, "Force running all commands (update-manager, synaptic) with pkexec" },
   { "startup-delay", 0, 0, G_OPTION_ARG_INT, &STARTUP_DELAY, "Delay startup by given amount of seconds" },
   { "devel-release", 0, 0, G_OPTION_ARG_NONE, &DEVEL_RELEASE, "Make the release checker check for a new development release" },
   { "force-release-check", 0, 0, G_OPTION_ARG_NONE, &FORCE_RELEASE_CHECK, "Force release check" },
   { NULL }
};

int 
main (int argc, char **argv)
{
	UpgradeNotifier *un;
	GError *error = NULL;

	// init
	if(!gtk_init_with_args (&argc, &argv, 
				_("- inform about updates"), entries, 
				"update-notifier", &error) ) {
	   fprintf(stderr, _("Failed to init the UI: %s\n"), 
		   error ? error->message : _("unknown error"));
	   exit(1);
	}

	notify_init("update-notifier");
        bindtextdomain(PACKAGE, PACKAGE_LOCALE_DIR);
        bind_textdomain_codeset(PACKAGE, "UTF-8");
        textdomain(PACKAGE);

	if (HOOK_DEBUG || UPDATE_DEBUG || INOTIFY_DEBUG ||
	    UEVENT_DEBUG || RELEASE_DEBUG || MISC_DEBUG)
		g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	// setup a custom debug log handler
	g_log_set_handler ("inotify", G_LOG_LEVEL_DEBUG,
			   debug_log_handler, NULL);
	g_log_set_handler ("hooks", G_LOG_LEVEL_DEBUG,
			   debug_log_handler, NULL);
	g_log_set_handler ("update", G_LOG_LEVEL_DEBUG,
			   debug_log_handler, NULL);
	g_log_set_handler ("uevent", G_LOG_LEVEL_DEBUG,
			   debug_log_handler, NULL);
	g_log_set_handler ("release", G_LOG_LEVEL_DEBUG,
			   debug_log_handler, NULL);
	g_log_set_handler (NULL, G_LOG_LEVEL_DEBUG,
			   debug_log_handler, NULL);

	g_set_application_name (_("update-notifier"));
	gtk_window_set_default_icon_name ("update-notifier");

	//g_print("starting update-notifier\n");

	/* Create the UpgradeNotifier object */
	un = g_new0 (UpgradeNotifier, 1);
	un->settings = g_settings_new(SETTINGS_SCHEMA);

	// do not run as system user (e.g. guest user)
	if (system_user(un) && !FORCE_START) {
	   g_warning("not starting for system user");
	   exit(0);
	}

	// do not run as system user (e.g. guest user)
	if (FORCE_RELEASE_CHECK)
	   g_settings_reset(un->settings, SETTINGS_KEY_LAST_RELEASE_CHECK);

	// check if it is running already
	if (!up_get_clipboard ()) {
	   g_warning ("already running?");
	   return 1;
	}

	// check for update-notifier dir and create if needed
	gchar *dirname = g_strdup_printf("%s/update-notifier",
					 g_get_user_config_dir());
	if(!g_file_test(dirname, G_FILE_TEST_IS_DIR))
	   g_mkdir(dirname, 0700);
	g_free(dirname);

	// delay icon creation for 30s so that the desktop 
	// login is not slowed down by update-notifier
	g_timeout_add_seconds(STARTUP_DELAY, 
			      (GSourceFunc)(tray_icons_init), un);

        // initial check for avahi
        avahi_disabled_check();

	/* setup inserted-cdroms checker */
	if(!cdroms_init(un)) {
	   g_warning("initializing cdroms failed");
	}

	// init release checker
	release_checker_init(un);

	// init uevent monitoring (missing firmware, etc.)
	uevent_init();

	// init gio file monitoring
	monitor_init (un);

	/* Start the main gtk loop */
	g_unix_signal_add_full (G_PRIORITY_DEFAULT, SIGINT, sigint_cb,
				NULL, NULL);
	gtk_main ();

	return 0;
}
