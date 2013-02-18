/* update-notifier.h
 * Copyright (C) 2004 Michiel Sikkes <michiel@eyesopened.nl>
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

#ifndef __UPGRADE_NOTIFIER_H__
#define __UPGRADE_NOTIFIER_H__

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "config.h"

#ifdef HAVE_APP_INDICATOR
#include <libappindicator/app-indicator.h>
#endif

#define CLIPBOARD_NAME 	"UPGRADE_NOTIFIER_SELECTION"

#define SETTINGS_SCHEMA "com.ubuntu.update-notifier"
#define SETTINGS_KEY_NO_UPDATE_NOTIFICATIONS "no-show-notifications"
#define SETTINGS_KEY_APPORT_NOTIFICATIONS "show-apport-crashes"
#define SETTINGS_KEY_END_SYSTEM_UIDS "end-system-uids"
#define SETTINGS_KEY_AUTO_LAUNCH "auto-launch"
#define SETTINGS_KEY_AUTO_LAUNCH_INTERVAL "regular-auto-launch-interval"
#define SETTINGS_KEY_HIDE_REBOOT "hide-reboot-notification"
#define SETTINGS_KEY_LAST_RELEASE_CHECK "release-check-time"

#define SETTINGS_UM_SCHEMA "com.ubuntu.update-manager"
#define SETTINGS_UM_KEY_LAST_LAUNCH "launch-time"

#define HOOKS_DIR "/var/lib/update-notifier/user.d/"
#define CRASHREPORT_HELPER "/usr/share/apport/apport-checkreports"
#define CRASHREPORT_REPORT_APP "/usr/share/apport/apport-gtk"
#define CRASHREPORT_DIR "/var/crash/"
#define REBOOT_FILE "/var/run/reboot-required"
#define UNICAST_LOCAL_AVAHI_FILE "/var/run/avahi-daemon/disabled-for-unicast-local"

// security update autolaunch minimal time (12h)
#define AUTOLAUNCH_MINIMAL_SECURITY_INTERVAL 12*60*60

// this is the age that we tolerate for updates (7 days)
#define OUTDATED_NAG_AGE 60*60*24*7

// this is the time we wait when we found outdated information for 
// anacron(and friends) to update the system (2h)
#define OUTDATED_NAG_WAIT 60*60*2

// this is the script and the time to run the release-upgrades checker
#define RELEASE_UPGRADE_CHECKER "/usr/lib/ubuntu-release-upgrader/check-new-release-gtk"
#define RELEASE_UPGRADE_CHECK_WAIT 60*60*24*14

// cache-changed plugin dir
#define CACHE_CHANGED_PLUGIN_DIR "/usr/share/update-notifier/plugins/cache-changed"

#if 0
// testing
#define OUTDATED_NAG_AGE 60*60
#define OUTDATED_NAG_WAIT 6
#endif

void invoke_with_pkexec(const gchar *cmd);
void invoke(const gchar *cmd, const gchar *desktop, gboolean with_pkexec);
gboolean in_admin_group(void);

typedef struct _TrayApplet TrayApplet;
typedef struct _UpgradeNotifier UpgradeNotifier;

struct _TrayApplet 
{
   GObject me;
   UpgradeNotifier *un;
#ifdef HAVE_APP_INDICATOR
   AppIndicator *indicator;
#else
   GtkStatusIcon *tray_icon;
#endif
   GtkWidget *menu;
   char *name;
   void *user_data;
};

struct _UpgradeNotifier
{
   GSettings *settings;

   TrayApplet *update;
   TrayApplet *reboot;
   TrayApplet *hook;
   TrayApplet *crashreport;

   guint update_finished_timer; 

   
   // some states for the file montioring (these field
   // are the state for the current "time-slice")
   gboolean dpkg_was_run;
   gboolean apt_get_running;

   // these field are "global" (time-wise)
   gboolean reboot_pending;
   gboolean hook_pending;
   gboolean crashreport_pending;
   gboolean unicast_local_avahi_pending;
   time_t last_apt_action;

};

#endif /* __UPGRADE_NOTIFIER_H__ */
