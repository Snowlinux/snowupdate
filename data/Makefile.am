@INTLTOOL_DESKTOP_RULE@

desktopdir       = $(sysconfdir)/xdg/autostart
desktop_in_files = update-notifier.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@INTLTOOL_XML_NOMERGE_RULE@
@GSETTINGS_RULES@
gsettings_SCHEMAS = com.ubuntu.update-notifier.gschema.xml

convertdir = $(datadir)/GConf/gsettings
dist_convert_DATA = update-notifier.convert

helperdir = $(libdir)/update-notifier
helper_SCRIPTS = apt_check.py apt-cdrom-check cddistupgrader update-motd-reboot-required update-motd-updates-available update-motd-fsck-at-reboot backend_helper.py package-data-downloader

notifydir = $(datadir)/update-notifier
notify_SCRIPTS = notify-reboot-required
notify_in_files = package-data-downloads-failed.in package-data-downloads-failed-permanently.in
notify_DATA = $(notify_in_files:.in=)

EXTRA_DIST= $(helper_SCRIPTS) \
	    $(desktop_in_files) \
	    $(notify_in_files) \
	    $(gsettings_SCHEMAS:.xml=.xml.in)

CLEANFILES = \
	    $(gsettings_SCHEMAS) \
	    package-data-downloads-failed \
	    package-data-downloads-failed-permanently

package-data-downloads-failed: package-data-downloads-failed.in
	po2debconf --podir=$(top_srcdir)/po -o $@ $<

package-data-downloads-failed-permanently: package-data-downloads-failed-permanently.in
	po2debconf --podir=$(top_srcdir)/po -o $@ $<
