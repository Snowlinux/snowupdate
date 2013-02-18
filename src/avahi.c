#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libnotify/notify.h>

#include "update-notifier.h"

static gint
show_notification (void* data)
{
	NotifyNotification *n;

	/* Create and show the notification */
	n = notify_notification_new (_("Network service discovery disabled"),
                                     _("Your current network has a .local "
                                       "domain, which is not recommended "
                                       "and incompatible with the Avahi "
                                       "network service discovery. The service "
                                       "has been disabled."
				       ),
				     GTK_STOCK_DIALOG_INFO);
	notify_notification_set_timeout (n, 60000);
	notify_notification_show (n, NULL);
	g_object_unref (n);

	return FALSE;
}

gboolean
avahi_disabled_check(void)
{
        if (g_file_test (UNICAST_LOCAL_AVAHI_FILE, G_FILE_TEST_EXISTS)) {
                /* Show the notification, after a delay so it doesn't look ugly
                 * if we've just logged in */
                g_timeout_add(5000, (GSourceFunc)(show_notification), NULL);
        }

        return TRUE;
}
