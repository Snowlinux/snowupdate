#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <glib-object.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#include "update-notifier.h"

extern gboolean DEVEL_RELEASE;

static inline void
g_debug_release(const char *msg, ...)
{
   va_list va;
   va_start(va, msg);
   g_logv("release",G_LOG_LEVEL_DEBUG, msg, va);
   va_end(va);
}

// actually show the notification
static gboolean
check_new_release(gpointer user_data)
{
   g_debug_release ("check_new_release");

   GSettings *settings = (GSettings*)user_data;

   time_t now = time(NULL);
   time_t last_check = (time_t)g_settings_get_uint(settings,
                                                   SETTINGS_KEY_LAST_RELEASE_CHECK);

   // test if we need to run
   if ( (last_check + RELEASE_UPGRADE_CHECK_WAIT) > now ) {
      g_debug_release ("release upgrade check not needed (%i > %i)", last_check + RELEASE_UPGRADE_CHECK_WAIT, now);
      return TRUE;
   }

   // run the checker
   gchar *argv[10] = { RELEASE_UPGRADE_CHECKER, };
   if (DEVEL_RELEASE) {
      g_debug_release ("running the release upgrade checker %s in devel mode", RELEASE_UPGRADE_CHECKER);
      argv[1] = "--devel-release";
      argv[2] = NULL;
   } else {
      g_debug_release ("running the release upgrade checker %s", RELEASE_UPGRADE_CHECKER);
      argv[1] = NULL;
   }
   g_spawn_async ("/", argv, NULL, 0, NULL, NULL, NULL, NULL);

   // and update the timestamp so we don't check again too soon
   g_settings_set_uint(settings, SETTINGS_KEY_LAST_RELEASE_CHECK, (guint)now);

   return TRUE;
}

gboolean
release_checker_init (UpgradeNotifier *un)
{
   g_debug_release ("release_checker_init");

   // check once at startup
   check_new_release (un->settings);
   // release upgrades happen not that frequently, we use two timers
   // check every 10 min if 48h are reached and then run 
   // "check-release-upgrade-gtk" again
   g_timeout_add_seconds (60*10, check_new_release, un->settings);

   return TRUE;
}
