## Process this file with automake to produce Makefile.in

INCLUDES = \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" \
	-DUIDIR=\""$(datadir)/update-notifier/ui/"\" \
	$(PACKAGE_CFLAGS) $(APP_INDICATOR_CFLAGS)

bin_PROGRAMS = update-notifier



update_notifier_SOURCES = update-notifier.c \
                           update-notifier.h \
			   clipboard.c \
			   rfc822.h\
			   rfc822.c\
			   hooks.h\
			   hooks.c\
			   reboot.c\
			   reboot.h\
			   crash.c\
			   crash.h\
			   avahi.h\
			   avahi.c\
			   update.c\
			   update.h\
			   release.c\
			   release.h\
			   uevent.c\
			   uevent.h\
			   cdroms.c\
			   cdroms.h \
			   trayappletui.h\
			   trayappletui.c

update_notifier_LDADD = $(PACKAGE_LIBS) $(INTLLIBS) $(APP_INDICATOR_LIBS)
update_notifier_LDFLAGS = -export-dynamic
