#include <glib.h>

#include "update-notifier.h"
#include "trayappletui.h"

void tray_applet_ui_ensure(TrayApplet *ta)
{
   g_debug("tray_applet_ui_ensure: %s", ta->name);
#ifdef HAVE_APP_INDICATOR
   if (ta->indicator)
      return;
   ta->indicator = app_indicator_new (ta->name, ta->name,
                                      APP_INDICATOR_CATEGORY_SYSTEM_SERVICES);
#else
   if (ta->tray_icon)
      return;
   ta->tray_icon = gtk_status_icon_new_from_icon_name (ta->name);
   gtk_status_icon_set_visible (ta->tray_icon, FALSE);
#endif
}

void tray_applet_ui_set_icon(TrayApplet *ta, const char *icon_name)
{
#ifdef HAVE_APP_INDICATOR
   return app_indicator_set_icon(ta->indicator, icon_name);
#else
   return gtk_status_icon_set_from_icon_name(ta->tray_icon, icon_name);
#endif
}

// visible
gboolean tray_applet_ui_get_visible(TrayApplet *ta)
{
#ifdef HAVE_APP_INDICATOR
   if (!ta->indicator)
      return FALSE;
   return app_indicator_get_status(ta->indicator) != APP_INDICATOR_STATUS_PASSIVE;
#else
   if (!ta->tray_icon)
      return FALSE;
   return gtk_status_icon_get_visible(ta->tray_icon);
#endif
}

void tray_applet_ui_set_visible(TrayApplet *ta, gboolean visible)
{
#ifdef HAVE_APP_INDICATOR
   if (visible)
      app_indicator_set_status(ta->indicator, APP_INDICATOR_STATUS_ACTIVE);
   else
      app_indicator_set_status(ta->indicator, APP_INDICATOR_STATUS_PASSIVE);  
#else
   gtk_status_icon_set_visible(ta->tray_icon, visible);
#endif
}

// tooltips are only support for GtkStatusIcon, so with the indicator
// we need to simulate this with a menu
void tray_applet_ui_set_tooltip_text(TrayApplet *ta, const char *text)
{
#ifdef HAVE_APP_INDICATOR
   // there are no tooltips so we show them in the menu as inactive entries
   if (!ta->menu) {
      ta->menu = gtk_menu_new();
   }
   // clear out any previous fake tooltip
   GList *l = gtk_container_get_children(GTK_CONTAINER(ta->menu));
   if (l && l->data && g_object_get_data(G_OBJECT(l->data), "fake-tooltip")) {
      // the tooltip itself
      gtk_container_remove(GTK_CONTAINER(ta->menu), l->data);
      // and the seperator
      if (g_list_next(l))
         gtk_container_remove(GTK_CONTAINER(ta->menu), g_list_next(l)->data);
   }
   g_list_free(l);

   // when we simluate the tooltip we may get really long lines, those
   // need to be split up. in order to do that in a unicode friendly
   // way we ask pango to do the line wrap for us
   GtkWidget *label = gtk_label_new(text);
   g_object_ref_sink(label);
   gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
   PangoLayout *layout = gtk_label_get_layout(GTK_LABEL(label));
   // use 300px max for a line by default
   static const gint MAX_LINE_SIZE_IN_PIXEL = 300;
   pango_layout_set_width(layout, 
                          pango_units_from_double(MAX_LINE_SIZE_IN_PIXEL));

   // now go over the lines that pango identified and manually add \n
   GSList *lines = pango_layout_get_lines_readonly(layout);
   GString *new_text = g_string_new("");
   while (lines != NULL) {
      new_text = g_string_append_len(new_text, 
                                     text+((PangoLayoutLine*)lines->data)->start_index,
                                     ((PangoLayoutLine*)lines->data)->length);
      lines = g_slist_next(lines);
      // avoid final newline
      if (lines != NULL)
         new_text = g_string_append(new_text, "\n");
   }

   // add a new one
   GtkWidget *menu_item = gtk_menu_item_new_with_label(new_text->str);
   gtk_menu_shell_insert(GTK_MENU_SHELL(ta->menu), menu_item, 0);
   g_object_set_data(G_OBJECT(menu_item), "fake-tooltip", "1");
   gtk_widget_set_sensitive(menu_item, FALSE);
   gtk_widget_show(menu_item);
   menu_item = gtk_separator_menu_item_new();
   gtk_menu_shell_insert(GTK_MENU_SHELL(ta->menu), menu_item, 1);
   gtk_widget_show(menu_item);

   // and free
   g_slist_free(lines);
   g_string_free(new_text, TRUE);
   g_object_unref(label);
#else
   gtk_status_icon_set_tooltip_text (ta->tray_icon,  text);
#endif
}

#ifndef HAVE_APP_INDICATOR
// internal helper for the tray_applet_ui_set_menu call
static gboolean
popup_menu_for_gtk_status_icon_cb (GtkStatusIcon *status_icon,
                                   guint          button,
                                   guint          activate_time,
                                   TrayApplet     *ta)
{
   gtk_menu_set_screen (GTK_MENU (ta->menu),
			gtk_status_icon_get_screen(ta->tray_icon));
   gtk_menu_popup (GTK_MENU (ta->menu), NULL, NULL, 
		   gtk_status_icon_position_menu, ta->tray_icon,
		   button, activate_time);
   return TRUE;
}
#endif

// add a menu to the applet, on the indicators this is left-click,
// on the GtkStatusBar its right click
void tray_applet_ui_set_menu(TrayApplet *ta, GtkWidget *menu)
{
   ta->menu = menu;
#ifdef HAVE_APP_INDICATOR
   app_indicator_set_menu(ta->indicator, GTK_MENU(ta->menu));
#else
   g_signal_connect (G_OBJECT(ta->tray_icon),
		     "popup-menu",
		     G_CALLBACK (popup_menu_for_gtk_status_icon_cb),
		     ta);
#endif
   

}


void tray_applet_ui_set_single_action(TrayApplet *ta, 
                                      const char *label,
                                      GCallback callback,
                                      void *data)
{
#ifdef HAVE_APP_INDICATOR
   GtkWidget *menu = gtk_menu_new();
   GtkWidget *menu_item = gtk_menu_item_new_with_label(label);
      g_signal_connect (G_OBJECT(menu_item),
                        "activate",
                        G_CALLBACK(callback),
                        data);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
   gtk_widget_show_all(menu);
   app_indicator_set_menu(ta->indicator, GTK_MENU(menu));
#else
   tray_applet_ui_set_tooltip_text (ta, label);
   g_signal_connect (G_OBJECT(ta->tray_icon),
                     "activate",
                     callback,
                     data);
#endif
}

void tray_applet_ui_set_data(TrayApplet *ta, const char *key, void *data)
{
#ifdef HAVE_APP_INDICATOR
   g_object_set_data(G_OBJECT(ta->indicator), key, data);
#else
   g_object_set_data(G_OBJECT(ta->tray_icon), key, data);
#endif
}

void* tray_applet_ui_get_data(TrayApplet *ta, const char *key)
{
#ifdef HAVE_APP_INDICATOR
   return g_object_get_data(G_OBJECT(ta->indicator), key);
#else
   return g_object_get_data(G_OBJECT(ta->tray_icon), key);
#endif
}

void tray_applet_ui_destroy(TrayApplet *ta)
{
   g_debug("tray_applet_ui_destroy: %s", ta->name);
#ifdef HAVE_APP_INDICATOR
   g_clear_object(&ta->indicator);
#else
   g_clear_object(&ta->tray_icon);
#endif
}
