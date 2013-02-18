dnl Process this file with autoconf to produce a configure script.

AC_INIT(src/update-notifier.c)
AM_INIT_AUTOMAKE(update-notifier, 0.41.5)
AM_CONFIG_HEADER(config.h)
AM_MAINTAINER_MODE

AC_ISC_POSIX
AC_PROG_CC
AM_PROG_CC_STDC
AC_HEADER_STDC

GNOME_COMMON_INIT
GLIB_GSETTINGS

pkg_modules="glib-2.0 >= 2.34 gtk+-3.0 libnotify gio-2.0 >= 2.26 x11"

PKG_CHECK_EXISTS(gudev-1.0, [ HAVE_GUDEV=1 ])
if test "x$HAVE_GUDEV" != "x"; then
    pkg_modules="$pkg_modules gudev-1.0"
    AC_DEFINE(HAVE_GUDEV, 1, [Use gudev-1.0])
else
    AC_WARN(No gudev-1.0, disable uevent handling)
fi


PKG_CHECK_MODULES(PACKAGE, [$pkg_modules])

GETTEXT_PACKAGE=update-notifier
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package.])
AC_PROG_INTLTOOL([0.23])

dnl Add the languages which your application supports here.
ALL_LINGUAS=`(cd "$srcdir/po" && ls *.po) | sed 's/\.po$//' | tr '\n' ' '`

AM_GLIB_GNU_GETTEXT

dnl AC_ARG_WITH(pkg-manager, [ --with-pkg-manager=PATH Package manager to use.])
dnl if test "x$with_pkg_manager" != "x"; then
dnl  AC_DEFINE_UNQUOTED(PKG_MANAGER, "$with_pkg_manager", [Package manager to use.])
dnl fi

dnl The following piece will be enabled when we release update-manager
dnl --------------
dnl AC_PATH_PROG(UPDATE_MANAGER, update-manager, no)
dnl if test "x$UPDATE_MANAGER" = "xno"; then
dnl   AC_MSG_ERROR(Couldn't find update-manager. Please install the update-manager package.)
dnl fi
dnl --------------

AC_ARG_ENABLE([system-config-printer],
        AS_HELP_STRING([--enable-system-config-printer], [enable on-demand launching of system-config-printer]),
        [], [enable_debug=no])
if test "x$enable_system_config_printer" = "xyes"; then
        AC_DEFINE(ENABLE_SCP, [1], [On-demand launching of system-config-printer.])
fi

AC_ARG_ENABLE([deprecations],
  [AS_HELP_STRING([--enable-deprecations],
    [allow deprecated API usage @<:@default=yes@:>@])],
  [],
  [enable_deprecations=yes])
AS_IF([test "x$enable_deprecations" = xno],
  [CFLAGS="-DG_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -DGSEAL_ENABLE"]
)

APPINDICATOR_REQUIRED=0.0.7
AC_ARG_ENABLE(appindicator,
                        AS_HELP_STRING([--enable-appindicator[=@<:@no/auto/yes@:>@]],[Build support for application indicators ]),
                        [enable_appindicator=$enableval],
                        [enable_appindicator="auto"])

if test x$enable_appindicator = xauto ; then
        PKG_CHECK_EXISTS([appindicator3-0.1 >= $APPINDICATOR_REQUIRED],
                         enable_appindicator="yes",
                         enable_appindicator="no")
fi

if test x$enable_appindicator = xyes ; then
        PKG_CHECK_EXISTS([appindicator3-0.1 >= $APPINDICATOR_REQUIRED],,
                         AC_MSG_ERROR([appindicator3-0.1 is not installed]))
        PKG_CHECK_MODULES(APP_INDICATOR,
                        appindicator3-0.1 >= $APPINDICATOR_REQUIRED)
        AC_SUBST(APP_INDICATOR_CFLAGS)
        AC_SUBST(APP_INDICATOR_LIBS)
        AC_DEFINE(HAVE_APP_INDICATOR, 1, [Have AppIndicator])
fi
AM_CONDITIONAL(HAVE_APP_INDICATOR, test x"$enable_appindicator" = xyes)



AC_OUTPUT([
Makefile
src/Makefile
data/Makefile
ui/Makefile
pixmaps/Makefile
pixmaps/16x16/Makefile
pixmaps/22x22/Makefile
pixmaps/24x24/Makefile
pixmaps/48x48/Makefile
po/Makefile.in
])


