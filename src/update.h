#include <gtk/gtk.h>
#include <libnotify/notify.h>


gboolean update_check(TrayApplet *ta);
void update_tray_icon_init(TrayApplet *ta);

void update_apt_is_running(TrayApplet *ta, gboolean is_running);

typedef struct _UpdateTrayAppletPrivate UpdateTrayAppletPrivate;
struct _UpdateTrayAppletPrivate 
{
   // this is a permanent marker if apt is running currently
   // (the difference to the one in update-notifier.h is that
   //  the one in here is "global" and not "per-timeslice" information
   gboolean apt_is_running;
   NotifyNotification *active_notification;
   
   int num_upgrades;
   int num_security;
};
