#ifndef _HOOKS_H_
#define _HOOKS_H_



gboolean check_update_hooks(TrayApplet *ta);
void hook_tray_icon_init(TrayApplet *ta);



typedef struct _HookFile HookFile;
struct _HookFile
{
   char *filename;
   time_t mtime;
   guint8 md5[16];
   gboolean cmd_run;
   gboolean seen;
};


typedef struct _HookTrayAppletPrivate HookTrayAppletPrivate;
struct _HookTrayAppletPrivate
{
   GtkWidget *dialog_hooks;
   GtkWidget *label_title;
   GtkWidget *textview_hook;
   GtkWidget *button_next;
   GtkWidget *button_run;

   // the list of all hook files
   GList *hook_files;   

   // the notification
   NotifyNotification *active_notification;
};

#endif
