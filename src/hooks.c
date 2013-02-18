#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <libnotify/notify.h>

#include <locale.h>
#include <langinfo.h>

#include "update-notifier.h"
#include "hooks.h"
#include "rfc822.h"
#include "assert.h"
#include "trayappletui.h"

/* relative to the home dir */
#define HOOKS_SEEN_DEPRECATED ".update-notifier/hooks_seen"
/* relative to the XDG_CONFIG_HOME dir */
#define HOOKS_SEEN "update-notifier/hooks_seen"

/* used by e.g. the installer to mark stuff that's already done */
#define GLOBAL_HOOKS_SEEN "/etc/update-notifier/hooks_seen"


// the size of the md5 hash digest for the duplicated note detection
static const int DIGEST_SIZE=16;

static inline void
g_debug_hooks(const char *msg, ...)
{
   va_list va;
   va_start(va, msg);
   g_logv("hooks",G_LOG_LEVEL_DEBUG, msg, va);
   va_end(va);
}

// compare a HookFile with a filename and find the HookFile that matches
static gint
compare_hook_func(gconstpointer a, gconstpointer b)
{
   //g_debug_hooks("compare: %s %s",(char*)(((HookFileSeen*)a)->filename),(char*)b);
   g_assert(a);
   g_assert(b);

   return strcmp(((HookFile*)a)->filename, b);
}

// return the most recent mtime or ctime of the file
static time_t
hook_file_time(const gchar *filename)
{
   struct stat buf;
   char *file = g_strdup_printf("%s/%s",HOOKS_DIR, filename);
   if(g_stat(file, &buf) <0) {
      g_warning("can't stat %s",file);
      g_free(file);
      return 0;
   }
   g_free(file);

   time_t mtime = buf.st_mtime;
   time_t ctime = buf.st_ctime;

   return mtime > ctime ? mtime : ctime;
}

static gboolean
hook_file_md5(const gchar *filename, guint8 *md5)
{
   guchar buf[512];
   FILE *f;
   char *file;
   gsize n;
   GChecksum *checksum;

   file = g_strdup_printf("%s/%s",HOOKS_DIR, filename);
   f = fopen(file,"r");
   if(f == NULL) {
      g_warning("can't read %s",file);
      g_free(file);
      return FALSE;
   }
   checksum = g_checksum_new(G_CHECKSUM_MD5);
   do {
      n = fread(buf, 1, sizeof(buf), f);
      g_checksum_update(checksum, buf, n);
   } while(n > 0);
   
   n=DIGEST_SIZE;
   g_checksum_get_digest(checksum, md5, &n);
   //g_debug_hooks("md5: %s -> '%s'", filename, md5);

   g_free(file);
   g_checksum_free(checksum);
   return TRUE;
}

/* mark a given hook file as seen 
  (actually implemented to write out all the information we have)
*/
static gboolean
hook_file_mark_as_seen(HookTrayAppletPrivate *priv, HookFile *hf)
{
   g_debug_hooks("mark_hook_file_as_seen: %s", hf->filename);

   // copy the md5
   guint8 md5[DIGEST_SIZE];
   hook_file_md5(hf->filename, md5);

   // mark as seen 
   hf->seen = TRUE;

   // update the time (extra paranoia, shouldn't be needed)
   hf->mtime = hook_file_time(hf->filename);

   // write out the list of known files
   gchar *filename = g_strdup_printf("%s/%s",
				     g_get_user_config_dir(),
				     HOOKS_SEEN);
   FILE *f = fopen(filename, "w");
   if(f==NULL) {
      g_warning("Something went wrong writing the users hook file");
      return FALSE;
   }

   // write out all the hooks that are seen 
   //
   // and any hooks that are not yet seen but have the same md5sum 
   // as the one just marked (avoids showing duplicated hooks effecivly). 
   //
   // For hooks with the same md5sum use the mtime of the just displayed hook
   GList *elm = g_list_first(priv->hook_files);
   for(;elm != NULL; elm = g_list_next(elm)) {
      HookFile *e = (HookFile*)elm->data;
      g_debug_hooks("will write out: %s (%s)",e->filename, e->md5);
      if(e->seen == TRUE) {
	 g_debug_hooks("e->seen: %s %li %x", e->filename,e->mtime, (int)(e->cmd_run));
	 fprintf(f,"%s %li %x\n", e->filename, e->mtime, (int)(e->cmd_run));
      } else if(memcmp(e->md5,md5,DIGEST_SIZE) == 0) {
	 e->seen = TRUE;
	 fprintf(f,"%s %li %x\n", e->filename,hf->mtime, (int)(e->cmd_run));
	 g_debug_hooks("same md5: %s %li %x",e->filename,hf->mtime,(int)(e->cmd_run));
      }
   }

   fclose(f);
   g_free(filename);

   return TRUE;
}

/* mark a given HookFile as run */
static gboolean
mark_hook_file_as_run(HookTrayAppletPrivate *priv, HookFile *hf)
{
   if(hf == NULL)
      return FALSE;

   g_debug_hooks("mark_hook_file_as_run: %s",hf->filename);
   hf->cmd_run = TRUE;

   return TRUE;
}


/* get the language code in a static allocated buffer
 * short_form: only return the languagecode if true, otherwise
 *             languagecode_countrycode
 */
static char *
get_lang_code(gboolean short_form)
{
   /* make own private copy */
   static gchar locale[51];
   strncpy(locale, setlocale(LC_MESSAGES, NULL), 50);
   
   // FIXME: we need to be more inteligent here
   // _and_ we probably want to look into the "LANGUAGE" enviroment too
   if(short_form) {
      locale[2] = 0;
      return locale;
   } else {
      locale[5] = 0;
      return locale;
   }
}

/*
 * get a i18n field of the rfc822 header
 * Return Value: a pointer that must not be freed (part of the rfc822 struct)
 */
static char *
hook_file_lookup_i18n(struct rfc822_header *rfc822, char *field)
{
   gchar *s, *entry, *text;

   /* first check for langpacks, then i18n fields */
   entry = rfc822_header_lookup(rfc822, "GettextDomain");
   if(entry != NULL) {
      text =  rfc822_header_lookup(rfc822, field);
      s = dgettext(entry, text);
      if(text != s)
	 return s;
   }

   /* try $field-$languagecode_$countrycode.$codeset first */
   s = g_strdup_printf("%s-%s.%s", field, get_lang_code(FALSE), nl_langinfo(CODESET));
   entry = rfc822_header_lookup(rfc822, s);
   //g_debug_hooks("Looking for: %s ; found: %s",s,entry);
   g_free(s);
   if(entry != NULL)
      return entry;

   /* try $field-$languagecode_$countrycode (and assume utf-8) */
   s = g_strdup_printf("%s-%s", field, get_lang_code(FALSE));
   entry = rfc822_header_lookup(rfc822, s);
   //g_debug_hooks("Looking for: %s ; found: %s",s,entry);
   g_free(s);
   if(entry != NULL)
      return entry;

   /* try $field-$languagecode.$codeset next */
   s = g_strdup_printf("%s-%s.%s", field, get_lang_code(TRUE), nl_langinfo(CODESET));
   //g_debug_hooks("Looking for: %s ; found: %s",s,entry);
   entry = rfc822_header_lookup(rfc822, s);
   g_free(s);
   if(entry != NULL)
      return entry;

   /* try $field-$languagecode (and assume utf-8 codeset) */
   s = g_strdup_printf("%s-%s", field, get_lang_code(TRUE));
   //g_debug_hooks("Looking for: %s ; found: %s",s,entry);
   entry = rfc822_header_lookup(rfc822, s);
   g_free(s);
   if(entry != NULL)
      return entry;

   /* now try translating it with gettext */
   entry = rfc822_header_lookup(rfc822, field);
   return entry;
}

static char *
hook_description_get_summary(struct rfc822_header *rfc822)
{
   char *summary = hook_file_lookup_i18n(rfc822, "Name");
   return summary;
}

static char *
hook_description_get_description(struct rfc822_header *rfc822)
{
   char *description = hook_file_lookup_i18n(rfc822, "Description");
   return description;
}

/*
 * show the given hook file
 */
static gboolean
show_next_hook(TrayApplet *ta, GList *hooks)
{
   //g_debug_hooks("show_next_hook()");

   HookTrayAppletPrivate *priv = (HookTrayAppletPrivate *)ta->user_data;

   // init some vars
   HookFile *hf = NULL;
   char *hook_file = NULL;

   // find the next unseen hook
   GList *elm = g_list_first(hooks);
   for(;elm != NULL; elm = g_list_next(elm)) {
      hf = (HookFile*)elm->data;
      if(hf->seen == FALSE) {
	 hook_file = hf->filename;
	 //g_debug_hooks("next_hook is: %s",hook_file);
	 break;
      }
   }

   if(hook_file == NULL) {
      g_debug_hooks("no unseen hookfile found in hook list of len (%i)",
	      g_list_length(hooks));
      return FALSE;
   }

   /* if there's another unseen hook, show the "next" button */
   gboolean show_next_button = FALSE;
   if(elm) {
      for(elm = g_list_next(elm); elm != NULL; elm = g_list_next(elm)) {
	 if(((HookFile*)elm->data)->seen == FALSE) {
	    show_next_button = TRUE;
	    break;
	 }
      }
   }
   if(show_next_button)
      gtk_widget_show(priv->button_next);
   else
      gtk_widget_hide(priv->button_next);

   char *filename = g_strdup_printf("%s%s",HOOKS_DIR,hook_file);

   /* setup the message */
   GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(priv->textview_hook));
   FILE *f = fopen(filename, "r");
   if(f == NULL) {
      g_warning("can't open %s", filename);
      g_free(filename);
      return FALSE;
   }
   struct rfc822_header *rfc822 = rfc822_parse_stanza(f);

   char *cmd = rfc822_header_lookup(rfc822, "Command");
   char *term = rfc822_header_lookup(rfc822, "Terminal");
   if(term)
      term = g_strstrip(term);
   g_object_set_data(G_OBJECT(priv->button_run),"cmd", g_strdup(cmd));
   g_object_set_data(G_OBJECT(priv->button_run),"term", g_strdup(term));
   g_object_set_data(G_OBJECT(priv->button_run),"hook_file", hf);
   if(cmd != NULL) {
      gtk_widget_show(priv->button_run);
   } else {
      gtk_widget_hide(priv->button_run);
   }
   char *title_str = hook_file_lookup_i18n(rfc822, "Title");
   if(title_str != NULL) {
      gchar *s = g_strdup_printf("<span weight=\"bold\" size=\"larger\">%s</span>", title_str);
      gtk_label_set_markup(GTK_LABEL(priv->label_title), s);
      g_free(s);
   }
   char *summary = hook_description_get_summary(rfc822);
   char *descr = hook_description_get_description(rfc822);
   char *s;
   if(summary) {
      s = g_strdup_printf("%s\n\n%s\n",gettext(summary), gettext(descr));
      gtk_text_buffer_set_text(buf, s, -1);
      // set the name to bold
      GtkTextIter start, end;
      gtk_text_buffer_get_iter_at_offset(buf, &start, 0);
      gtk_text_buffer_get_iter_at_offset(buf, &end, g_utf8_strlen(gettext(summary),-1));
      gtk_text_buffer_apply_tag_by_name(buf, "bold_tag", &start, &end);

   } else {
      s = g_strdup_printf("%s\n",gettext(descr));
      gtk_text_buffer_set_text(buf, s, -1);
   }

   // set button name (if needed)
   char *b = hook_file_lookup_i18n(rfc822, "ButtonText");
   if(b)
      gtk_button_set_label(GTK_BUTTON (priv->button_run), b);
   else
      gtk_button_set_label(GTK_BUTTON (priv->button_run), _("_Run this action now"));

   // clean up
   fclose(f);
   g_free(filename);
   g_free(s);
   rfc822_header_free_all(rfc822);

   /* mark the current hook file as seen */
   g_object_set_data(G_OBJECT(priv->button_next), "HookFile", hf);

   return TRUE;
}


void on_button_run_clicked(GtkWidget *self, gpointer *data)
{
   //g_debug_hooks("cb_button_run()");
   gchar *cmdline;

   TrayApplet *ta = (TrayApplet *)data;
   HookTrayAppletPrivate *priv = (HookTrayAppletPrivate *)ta->user_data;

   /* mark the current hook file as run */
   HookFile *hf = g_object_get_data(G_OBJECT(self), "hook_file");
   mark_hook_file_as_run(priv, hf);

   gchar *cmd = g_object_get_data(G_OBJECT(self), "cmd");
   gchar *term = g_object_get_data(G_OBJECT(self), "term");

   if(cmd == NULL) {
      g_warning("cmd is NULL");
      return;
   }

   if(term != NULL && !g_ascii_strncasecmp(term, "true",-1)) {
      cmdline = g_strdup_printf("x-terminal-emulator -e %s",cmd);
   } else 
      cmdline = g_strdup(cmd);

   g_spawn_command_line_async(cmdline, NULL);
   g_free(cmdline);
}

void on_button_next_clicked(GtkWidget *self, gpointer *data)
{
   g_debug_hooks("cb_button_next()");
   TrayApplet *ta = (TrayApplet *)data;
   HookTrayAppletPrivate *priv = (HookTrayAppletPrivate *)ta->user_data;

   HookFile *hf;
   hf = (HookFile*)g_object_get_data(G_OBJECT(self),"HookFile");
   if(hf == NULL) {
      g_warning("button_next called without HookFile");
      return;
   }
   
   // mark as seen 
   hook_file_mark_as_seen(priv, hf);

   if(priv->hook_files != NULL)
      show_next_hook(ta, priv->hook_files);
}

static void
on_insert_text(GtkTextBuffer *buffer,
	       GtkTextIter   *iter_end,
	       gchar         *text,
	       gint           arg3,
	       gpointer       user_data)
{
   GtkTextIter iter, match_start, match_end, match_tmp;

   // get where we start
   gtk_text_buffer_get_iter_at_offset(buffer, &iter,
				      gtk_text_iter_get_offset(iter_end) - g_utf8_strlen(text,-1));
   // search for http:// uris
   while(gtk_text_iter_forward_search(&iter, 
				      "http://",  GTK_TEXT_SEARCH_VISIBLE_ONLY, 
				      &match_start, &match_end, iter_end))  {
      match_tmp = match_end;
      // we found a uri, iterate it until the end is found
      while(gtk_text_iter_forward_char(&match_tmp)) {
	 gchar *s = gtk_text_iter_get_text(&match_end,&match_tmp);
	 if(strlen(s) < 1)
	    break;
	 char c = s[strlen(s)-1];
	 if(c == ' ' || c == ')' || c == ']' || c == '\n' || c == '\t' || c == '>')
	    break;
	 match_end = match_tmp;
      }
      gchar *url = gtk_text_iter_get_text(&match_start, &match_end);
      //g_print("url: '%s'\n",url);
      GtkTextTag *tag;
      tag = gtk_text_buffer_create_tag(buffer, NULL,
				       "foreground","blue",
				       "underline", PANGO_UNDERLINE_SINGLE,
				       NULL);
      g_object_set_data(G_OBJECT(tag), "url", url);
      gtk_text_buffer_apply_tag(buffer, tag, &match_start, &match_end);
      iter = match_end;
   }
}

static void
on_event_after(GtkWidget *widget,
	       GdkEventButton  *event,
	       gpointer   user_data)      
{
   if(event->type != GDK_BUTTON_RELEASE)
      return;
   if(event->button != 1)
      return;
   gint x,y;
   gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(widget),
					 GTK_TEXT_WINDOW_WIDGET,
					 event->x, event->y,
					 &x, &y);
   GtkTextIter iter;
   gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(widget), &iter, x, y);
   GSList *tags = gtk_text_iter_get_tags(&iter);
   for( ; tags != NULL ; tags = tags->next) {
      gchar *url = g_object_get_data(G_OBJECT(tags->data), "url");
      if(url != NULL) {
	 //g_print("click: '%s'\n",url);
	 char *argv[] = { "/usr/bin/gnome-open", url, NULL };
	 g_spawn_async(NULL, argv, NULL, 0, NULL, NULL, NULL, NULL);
	 break;
      }
   }
}

static void
show_hooks(TrayApplet *ta, gboolean focus_on_map)
{
   HookTrayAppletPrivate *priv = (HookTrayAppletPrivate *)ta->user_data;
   GtkBuilder *builder;
   GError *error = NULL;

   g_debug_hooks("show_hooks()");

   builder = gtk_builder_new ();
   assert (builder);

   if (!gtk_builder_add_from_file (builder, UIDIR"hooks-dialog.ui", &error)) {
      g_warning ("Couldn't load builder file: %s", error->message);
      g_error_free (error);
   }

   gtk_builder_connect_signals (builder, (gpointer)ta);

   priv->dialog_hooks = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_hooks"));
   gtk_window_set_title (GTK_WINDOW (priv->dialog_hooks), _("Information available"));
   assert (priv->dialog_hooks);
   priv->label_title = GTK_WIDGET (gtk_builder_get_object (builder, "label_title"));
   assert (priv->label_title);
   priv->textview_hook = GTK_WIDGET (gtk_builder_get_object (builder, "textview_hook"));
   assert (priv->textview_hook);
   priv->button_run = GTK_WIDGET (gtk_builder_get_object (builder, "button_run"));
   assert (priv->button_run);
   priv->button_next = GTK_WIDGET (gtk_builder_get_object (builder, "button_next"));
   assert (priv->button_next);

   g_object_unref (G_OBJECT (builder));

   // create a bold tag
   GtkTextBuffer *buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (priv->textview_hook));
   gtk_text_buffer_create_tag (buf, "bold_tag",
			       "scale", PANGO_SCALE_LARGE, 
			       "weight", PANGO_WEIGHT_BOLD,
			       NULL);  
   g_signal_connect_after (G_OBJECT (buf), "insert-text",
			   G_CALLBACK (on_insert_text), NULL);
   g_signal_connect_after (G_OBJECT (priv->textview_hook), "event-after",
			   G_CALLBACK (on_event_after), NULL);

   gtk_window_set_focus_on_map (GTK_WINDOW (priv->dialog_hooks), focus_on_map);

   // if show_next_hook() fails for some reason don't do anything
   if(!show_next_hook(ta, priv->hook_files))
      goto out;

   int res = gtk_dialog_run (GTK_DIALOG (priv->dialog_hooks));
   if(res == GTK_RESPONSE_CLOSE) {
      // mark the currently current hookfile as seen
      HookFile *hf;
      hf = (HookFile*)g_object_get_data (G_OBJECT (priv->button_next), "HookFile");
      if(hf == NULL) {
	 g_warning("show_hooks called without HookFile");
	 goto out;
      }
   
      // mark as seen 
      hook_file_mark_as_seen(priv, hf);
      check_update_hooks(ta);
   }

out:
   g_clear_pointer (&priv->dialog_hooks, gtk_widget_destroy);
   priv->label_title = NULL;
   priv->textview_hook = NULL;
   priv->button_run = NULL;
   priv->button_next = NULL;
}

static gboolean
button_release_cb (GtkWidget *widget, 
		   TrayApplet *ta)
{
   HookTrayAppletPrivate *priv = (HookTrayAppletPrivate*)ta->user_data;

   if(priv->active_notification != NULL) {
      notify_notification_close(priv->active_notification, NULL);
      g_clear_object(&priv->active_notification);
   }
   
   //g_debug_hooks("left click on hook applet");
   show_hooks(ta, TRUE);
   return TRUE;
}

static gboolean
is_hook_relevant(const gchar *hook_file)
{
   g_debug_hooks("is_hook_relevant(): %s", hook_file);
   gboolean res = TRUE;

   char *filename = g_strdup_printf("%s%s",HOOKS_DIR,hook_file);
   FILE *f = fopen(filename, "r");
   if(f == NULL) {
      // can't open, can't be relevant
      return FALSE; 
   }
   struct rfc822_header *rfc822 = rfc822_parse_stanza(f);

   // check if its a note that is relevant only for admin users
   // (this is the default)
   gchar *b = rfc822_header_lookup(rfc822, "OnlyAdminUsers");
   if(b == NULL || g_ascii_strncasecmp(b, "true",-1) == 0) {
      if (!in_admin_group())
	 return FALSE;
   }

   // check the DontShowAfterReboot flag
   b = rfc822_header_lookup(rfc822, "DontShowAfterReboot");
   if(b != NULL && g_ascii_strncasecmp(b, "true",-1) == 0) {
      g_debug_hooks("found DontShowAfterReboot");

      // read the uptime information
      double uptime=0, idle=0;
      char buf[1024];
      int fd = open("/proc/uptime", 0);
      int local_n = read(fd, buf, sizeof(buf)-1);
      buf[local_n] = '\0';
      char *savelocale = setlocale(LC_NUMERIC, NULL);
      setlocale(LC_NUMERIC,"C");
      sscanf(buf, "%lf %lf", &uptime,&idle);
      close(fd);
      setlocale(LC_NUMERIC,savelocale);

      time_t mtime = hook_file_time(hook_file);
      time_t now = time(NULL);

      g_debug_hooks("now: %li mtime: %li uptime: %f",now,mtime,uptime);
      g_debug_hooks("diff: %li  uptime: %f",now-mtime,uptime);
      if((int)uptime > 0 && (now - mtime) > (int)uptime) {
	 g_debug_hooks("not relevant because of reboot: %s",hook_file);
	 res = FALSE;
      }  else
	 g_debug_hooks("hook is relevant");
   }
   fclose(f);

   // check for DisplayIf test
   b = rfc822_header_lookup(rfc822, "DisplayIf");
   if(b != NULL) {
      g_debug_hooks("found DisplayIf command: '%s'", b);
      int exitc = system(b);
      g_debug_hooks("'%s' returned: %i", b, exitc);
      res = (exitc == 0);
   }

   g_free(filename);
   rfc822_header_free_all(rfc822);
   return res;
}

static gboolean show_notification(void *data)
{
   TrayApplet *ta = (TrayApplet*)data;
   HookTrayAppletPrivate *priv = (HookTrayAppletPrivate*)ta->user_data;

   if(!tray_applet_ui_get_visible(ta))
      return FALSE;

   if((priv->dialog_hooks && gtk_widget_get_visible(priv->dialog_hooks)) ||
      priv->active_notification != NULL)
      return FALSE;

   NotifyNotification *n;
   GdkPixbuf* pix;
   n = notify_notification_new(
			       _("Information available"),
			       _("Click on the notification icon"
				 " to show the available information.\n"),
			       NULL);
   
   pix = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), 
				  GTK_STOCK_DIALOG_INFO, 48,0,NULL);
   notify_notification_set_icon_from_pixbuf (n, pix);
   g_object_unref(pix);
   notify_notification_set_timeout (n, 60*1000);
   notify_notification_show(n, NULL);
   if (priv->active_notification)
      g_object_unref(priv->active_notification);
   priv->active_notification = n;

   return FALSE;
}

gboolean check_update_hooks(TrayApplet *ta)
{
   GList *elm;
   HookFile *hf;
   GDir* dir;
   const gchar *hook_file;

   g_debug_hooks("check_update_hooks()");
   HookTrayAppletPrivate *priv = (HookTrayAppletPrivate*)ta->user_data;

   dir=g_dir_open(HOOKS_DIR, 0, NULL);
   if(dir == NULL) {
      g_warning("can't read %s directory",HOOKS_DIR);
      return FALSE;
   }

   g_debug_hooks("reading '%s' dir", HOOKS_DIR);
   int unseen_count = 0;
   while((hook_file=g_dir_read_name(dir)) != NULL) {
      g_debug_hooks("investigating file '%s'", hook_file); 

      // check if the hook still applies (for e.g. DontShowAfterReboot)
      if(!is_hook_relevant(hook_file)) {
	 g_debug_hooks("not relevant: '%s'",hook_file);
	 continue;
      }
      // see if we already know about this hook filename
      elm = g_list_find_custom(priv->hook_files,hook_file,
				      compare_hook_func);

      // not seen before, add to the list
      if(elm == NULL) {
	 g_debug_hooks("never seen before: %s",hook_file);
	 HookFile *t = g_new0(HookFile, 1);
	 t->filename = strdup(hook_file);
	 t->mtime = hook_file_time(hook_file);
	 hook_file_md5(hook_file, t->md5);
	 t->cmd_run = FALSE;
	 t->seen = FALSE;
	 priv->hook_files = g_list_append(priv->hook_files, (gpointer)t);
	 // init elm with the just added record (will be needed below)
	 elm = g_list_find_custom(priv->hook_files,hook_file,
				  compare_hook_func);
	 assert(elm != NULL);
      }
      
      // this is the hook file information we have (either because it was
      // availabe already or because we added it)
      hf = (HookFile*)elm->data;

      // file has changed since we last saw it
      time_t new_mtime = hook_file_time(hook_file);
      if(new_mtime > hf->mtime) {
	 g_debug_hooks("newer mtime: %s (%li > %li))",hook_file, new_mtime, hf->mtime);
	 hf->seen = FALSE;
      }

      // we have not seen it yet (because e.g. it was just added)
      if(hf->seen == FALSE) {
	 g_debug_hooks("the file '%s' was NOT SEEN yet",hook_file);

	 // update mtime (because we haven't seen the old one and there
	 // is a new one now)
	 hf->mtime = new_mtime;

	 // check if there is already another notification that is 
	 // a) not this one
	 // b) not seen yet
	 // c) the same
	 // if so, we don't increase the unseen count as all identical
	 // ones will maked as unseen at onces
	 gboolean md5match = FALSE;
	 GList *x = g_list_first(priv->hook_files);
	 while(x!=NULL) {
	    HookFile *e = (HookFile*)x->data;
	    if((elm != x)  &&
	       (e->seen == FALSE) &&
	       (memcmp(hf->md5,e->md5,DIGEST_SIZE)==0) )
	      {
		 g_debug_hooks("%s (%s) was seen also in %s (%s)",
			 hf->filename, hf->md5, e->filename, e->md5);
		 md5match = TRUE;
	      }
	    x = g_list_next(x);
	 }
	 if(!md5match) {
	    g_debug_hooks("%s increases unseen_count",hf->filename);
	    unseen_count++;
	 }
      } else 
	 g_debug_hooks("already seen: '%s'",hook_file);
   }
   g_dir_close(dir);

   //g_debug_hooks("hooks: %i (new: %i)", g_list_length(hook_files), unseen_count);

   if (unseen_count > 0) {
      // check if we do a notification or a dialog
      if(g_settings_get_boolean(ta->un->settings, SETTINGS_KEY_AUTO_LAUNCH)) {
	 g_debug_hooks("showing hooks with focus on map == FALSE");
	 show_hooks(ta, FALSE);
	 return TRUE;
      }

      // no dialog, do the notification
      if(!tray_applet_ui_get_visible (ta))
	 g_timeout_add(5000, show_notification, ta);

      tray_applet_ui_ensure (ta);
      tray_applet_ui_set_single_action(ta, _("Information available"),
				       G_CALLBACK (button_release_cb), ta);
      tray_applet_ui_set_visible (ta, TRUE);
   } else
      tray_applet_ui_destroy (ta);

   return TRUE;
}

static void
hook_read_seen_file(HookTrayAppletPrivate *priv, const char* filename)
{
   HookFile *t;
   char buf[512];
   int time, was_run;
   FILE *f = fopen(filename, "r");
   if(f==NULL)
      return;

   g_debug_hooks("reading hook_file: %s ", filename);

   while(fscanf(f, "%s %i %i",buf,&time,&was_run) == 3) {

      // first check if the file actually exists, if not skip it
      // (may already be delete when e.g. a package was removed)
      char *filename = g_strdup_printf("%s%s",HOOKS_DIR,buf);
      gboolean res = g_file_test(filename, G_FILE_TEST_EXISTS);
      g_free(filename);
      if(!res)
	 continue;

      // now check if we already have that filename in the list
      GList *elm = g_list_find_custom(priv->hook_files,buf,
				      compare_hook_func);
      if(elm != NULL) {
	 g_debug_hooks("hookfile: %s already in the list", buf);

	 // we have that file already in the list with a newer mtime,
	 // than the file in the config file. ignore the config file
	 HookFile *exisiting = (HookFile *)elm->data;
	 // and it's more current
	 if(exisiting->mtime > time) {
	    g_debug_hooks("existing is newer, ignoring the read one");
	    continue;
	 }

	 // we have it in the list, but the the file in the list is older
	 // than the one in the config file. use this config-file instead
	 // and update the values in the list
	 exisiting->cmd_run = was_run;
	 exisiting->seen = TRUE;
	 exisiting->mtime = time;
	 hook_file_md5(exisiting->filename,exisiting->md5);

      } else {
	 // not in the list yet
	 // add the just read hook file to the list
	 g_debug_hooks("got: %s %i %i ",buf,time,was_run);
	 t = g_new0(HookFile, 1);
	 t->filename = strdup(buf);
	 t->mtime = time;
	 t->cmd_run = was_run;
	 t->seen = TRUE;
	 hook_file_md5(t->filename,t->md5);

	 priv->hook_files = g_list_append(priv->hook_files, (gpointer)t);
      }
   }
   fclose(f);
}

static gboolean 
init_already_seen_hooks(TrayApplet *ta)
{
   g_debug_hooks("init_already_seen_hooks");

   HookTrayAppletPrivate* priv = (HookTrayAppletPrivate*)ta->user_data;
   char *filename, *old_filename;

   // init with default value
   priv->hook_files = NULL;  

   // read global hook file
   hook_read_seen_file(priv, GLOBAL_HOOKS_SEEN);
   
   filename = g_strdup_printf("%s/%s", 
			      g_get_user_config_dir(),
			      HOOKS_SEEN);
   // compat with the previous version
   old_filename = g_strdup_printf("%s/%s", 
				  g_get_home_dir(),
				  HOOKS_SEEN_DEPRECATED);
   if (g_file_test(old_filename, G_FILE_TEST_IS_REGULAR)) {
     g_rename(old_filename, filename);
   }
   // read user hook file
   hook_read_seen_file(priv,filename);
   g_free(filename);
   g_free(old_filename);

   return TRUE;
}

void hook_tray_icon_init(TrayApplet *ta)
{
   HookTrayAppletPrivate *priv = g_new0(HookTrayAppletPrivate, 1);
   ta->user_data = priv;

   /* read already seen hooks */
   init_already_seen_hooks(ta);
   
   /* Check for hooks */
   check_update_hooks(ta);
}
