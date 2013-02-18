#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef HAVE_GUDEV
#include <sys/wait.h>

#include <ctype.h>

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>
#include <sys/utsname.h>

/* search path for firmare; second and fourth element are dynamically constructed
 * in uevent_init(). */
const gchar* firmware_search_path[] = {
    "/lib/firmware", NULL,
    "/lib/firmware/updates", NULL,
    NULL };

gchar *hplip_helper[] = { "/usr/bin/hp-plugin-ubuntu", NULL };

static inline void
g_debug_uevent(const char *msg, ...)
{
   va_list va;
   va_start(va, msg);
   g_logv("uevent",G_LOG_LEVEL_DEBUG, msg, va);
   va_end(va);
}

static gboolean
deal_with_hplip_firmware(GUdevDevice *device)
{
    const gchar *id_vendor, *id_product, *id_model;
    GError *error = NULL;
    gint ret = 0;

    id_vendor = g_udev_device_get_sysfs_attr (device, "idVendor");
    id_product = g_udev_device_get_sysfs_attr (device, "idProduct");
    id_model = g_udev_device_get_property (device, "ID_MODEL");
    g_debug_uevent ("uevent.c id_vendor=%s, id_product=%s", id_vendor, id_product);

    // only idVendor=03f0, idProduct="??{17,2a}" requires firmware
    if (g_strcmp0 (id_vendor, "03f0") != 0 || 
        id_product == NULL || 
        g_utf8_strlen(id_product, -1) != 4)
       return FALSE;
    if (! ( ((id_product[2] == '1') && (id_product[3] == '7')) ||
            ((id_product[2] == '2') && (tolower(id_product[3]) == 'a')) ))
       return FALSE;

    // firmware is only required if "hp-mkuri -c" returns 2 or 5
    const gchar *cmd = "/usr/bin/hp-mkuri -c";
    g_setenv("hp_model", id_model, TRUE);
    if (!g_spawn_command_line_sync (cmd, NULL, NULL, &ret, &error))
    {
       g_warning("error calling hp-mkuri");
       return FALSE;
    }

    // check return codes, 2 & 5 indicate that it has the firmware already
    if (WEXITSTATUS(ret) != 2 && WEXITSTATUS(ret) != 5) 
    {
       g_debug_uevent ("hp-mkuri indicates no firmware needed");
       return TRUE;
    }

    if (!g_spawn_async("/", hplip_helper, NULL, 0, NULL, NULL, NULL, NULL))
    {
       g_warning("error calling hplip_helper");
       return FALSE;
    }
    return TRUE;
}

#ifdef ENABLE_SCP
static gboolean scp_checked = FALSE;

static gboolean
deal_with_scp(GUdevDevice *device)
{
    GError *error = NULL;

    /* only do this once */
    if (scp_checked)
	return FALSE;

    /* check if we just added a printer */
    if ((g_strcmp0(g_udev_device_get_sysfs_attr(device, "bInterfaceClass"), "07") != 0 ||
		g_strcmp0(g_udev_device_get_sysfs_attr(device, "bInterfaceSubClass"), "01") != 0) &&
	    !g_str_has_prefix(g_udev_device_get_name (device), "lp")) {
	g_debug_uevent ("deal_with_scp: devpath=%s: not a printer", g_udev_device_get_sysfs_path(device));
	return FALSE;
    }

    g_debug_uevent ("deal_with_scp: devpath=%s: printer identified", g_udev_device_get_sysfs_path(device));

    scp_checked = TRUE;

    gchar* ps_argv[] = {"ps", "h", "-ocommand", "-Cpython", NULL};
    gchar* ps_out;
    if (!g_spawn_sync (NULL, ps_argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
		&ps_out, NULL, NULL, &error)) {
	g_warning ("deal_with_scp: error calling ps: %s", error->message);
	g_error_free (error);
	return TRUE;
    }
    g_debug_uevent ("deal_with_scp: running python processes:%s", ps_out);
    if (strstr (ps_out, "system-config-printer") != NULL) {
	g_debug_uevent ("deal_with_scp: system-config-printer already running");
	return TRUE;
    }

    g_debug_uevent ("deal_with_scp: launching system-config-printer");
    gchar* scp_argv[] = {"system-config-printer-applet", NULL};
    error = NULL;
    if (!g_spawn_async(NULL, scp_argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
	g_warning("%s could not be called: %s", scp_argv[0], error->message);
	g_error_free (error);
    }
    return TRUE;
}
#endif

static void
on_uevent (GUdevClient *client,
           gchar *action,
           GUdevDevice *device,
           gpointer user_data)
{
    g_debug_uevent ("uevent.c on_uevent: action=%s, devpath=%s", action, g_udev_device_get_sysfs_path(device));

    if (g_strcmp0 (action, "add") != 0 && g_strcmp0 (action, "change") != 0)
	return;

    /* handle firmware */
    if (deal_with_hplip_firmware(device))
       return;

#ifdef ENABLE_SCP
    if (deal_with_scp(device))
	return;
#endif
}

void
uevent_init(void)
{
    const gchar* subsytems[] = {"firmware", "usb", NULL};

    /* build firmware search path */
    struct utsname u;
    if (uname (&u) != 0) {
	g_warning("uname() failed, not monitoring firmware");
	return;
    }
    firmware_search_path[1] = g_strdup_printf("/lib/firmware/%s", u.release);
    firmware_search_path[3] = g_strdup_printf("/lib/firmware/updates/%s", u.release);

    GUdevClient* gudev = g_udev_client_new (subsytems);
    g_signal_connect (gudev, "uevent", G_CALLBACK (on_uevent), NULL);

    /* cold plug HPLIP firmware */
    GList *usb_devices, *elem;
    usb_devices = g_udev_client_query_by_subsystem (gudev, "usb");
    for (elem = usb_devices; elem != NULL; elem = g_list_next(elem)) {
       deal_with_hplip_firmware(elem->data);
#ifdef ENABLE_SCP
       deal_with_scp(elem->data);
#endif
       g_object_unref(elem->data);
    }
    g_list_free(usb_devices);
}
#else
#include <glib.h>
void
uevent_init(void)
{
    g_warning("Installation of firmware disabled.");
}
#endif // HAVE_GUDEV
