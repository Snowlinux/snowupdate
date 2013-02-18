#include "update-notifier.h"

// ensure indicator/icon exists
void tray_applet_ui_ensure(TrayApplet *ta);

// icon
void tray_applet_ui_set_icon(TrayApplet *ta, const char *icon_name);

// visible
gboolean tray_applet_ui_get_visible(TrayApplet *ta);
void tray_applet_ui_set_visible(TrayApplet *ta, gboolean visible);

// tooltips are only support for GtkStatusIcon, so with the indicator
// we need to simulate this with a menu
void tray_applet_ui_set_tooltip_text(TrayApplet *ta, const char *text);

// add a menu to the applet, on the indicators this is left-click,
// on the GtkStatusBar its right click
void tray_applet_ui_set_menu(TrayApplet *ta, GtkWidget *menu);


// this will result in a single menu item for the indicator or
// in a menu with a single action in the status icon
void tray_applet_ui_set_single_action(TrayApplet *ta, 
                                      const char *label,
                                      GCallback callback,
                                      void *data);

// set/get data
void tray_applet_ui_set_data(TrayApplet *ta, const char *key, void *data);
void* tray_applet_ui_get_data(TrayApplet *ta, const char *key);

// destroy indicator/icon
void tray_applet_ui_destroy(TrayApplet *ta);
