#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)

AC_INIT(libgksu, 2.0.12, kov@debian.org)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AC_CONFIG_SRCDIR(libgksu/libgksu.c)
AM_CONFIG_HEADER(config.h)

MAJOR_VERSION=`echo $VERSION | cut -d '.' -f 1`
MINOR_VERSION=`echo $VERSION | cut -d '.' -f 2`
MICRO_VERSION=`echo $VERSION | cut -d '.' -f 3`

# gettext domain
AC_DEFINE(DOMAIN, ["libgksu"], [gettext domain])

AC_SUBST(MAJOR_VERSION)
AC_SUBST(MINOR_VERSION)
AC_SUBST(MICRO_VERSION)

AM_MAINTAINER_MODE

AC_DEFINE(PACKAGE, DOMAIN, [package name])
AC_DEFINE(VERSION, AC_PACKAGE_VERSION, [package version])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LIBTOOL

##################################################
# Check for linker --version-script flag
##################################################
lt_simple_link_test_code='int main(){return(0);}'
AC_LIBTOOL_LINKER_OPTION([whether the linker accepts the --version-script flag],
		   use_version_script, [-Wl,--version-script=libgksu/libgksu.ver])
AM_CONDITIONAL(USE_VERSION_SCRIPT, test x$use_version_script = xyes)

AC_PATH_PROG(GCONFTOOL, gconftool-2)
AM_GCONF_SOURCE_2

PKG_CHECK_MODULES(GLIB, [glib-2.0])
PKG_CHECK_MODULES(GTK, [gtk+-2.0])
PKG_CHECK_MODULES(LIBGKSU, [gtk+-2.0 >= 2.4.0, gconf-2.0, libstartup-notification-1.0, gnome-keyring-1, libgtop-2.0])
PKG_CHECK_MODULES(GKSU_PROPERTIES, [gtk+-2.0 >= 2.12, gconf-2.0])
PKG_CHECK_MODULES(LIBGKSUUI, [gtk+-2.0, gconf-2.0, x11])
PKG_CHECK_MODULES(TEST_GKSUUI, [glib-2.0, x11])

# Checks for library functions.
ALL_LINGUAS="ca cs da de es eu fr hu it ko lt pl pt_BR ro ru sk sv nb nl th zh_CN"

AC_DEFINE(GETTEXT_PACKAGE, AC_PACKAGE_NAME, [gettext package])
GETTEXT_PACKAGE=AC_PACKAGE_NAME
AC_SUBST(GETTEXT_PACKAGE)

IT_PROG_INTLTOOL
AM_GLIB_GNU_GETTEXT

AC_ARG_ENABLE(sudo-forkpty, [  --enable-sudo-forkpty  Use forkpty when dealing with sudo; this is required if you have requiretty as an option in your sudoers file [default=no]], sudo_forkpty="$enableval", sudo_forkpty=no)
AM_CONDITIONAL(SUDO_FORKPTY, test x$sudo_forkpty = xyes)

##################################################
# Check for gtk-doc.
##################################################

AC_ARG_WITH(html-dir, [  --with-html-dir=PATH path to installed docs ])

if test "x$with_html_dir" = "x" ; then
  HTML_DIR='${datadir}/gtk-doc/html'
else
  HTML_DIR=$with_html_dir
fi

AC_SUBST(HTML_DIR)

gtk_doc_min_version=1.0
AC_MSG_CHECKING([gtk-doc version >= $gtk_doc_min_version])
if pkg-config --atleast-version=$gtk_doc_min_version gtk-doc; then
  AC_MSG_RESULT(yes)
  GTKDOC=true
else
  AC_MSG_RESULT(no)
  GTKDOC=false
fi

dnl Let people disable the gtk-doc stuff.
AC_ARG_ENABLE(gtk-doc, [  --enable-gtk-doc  Use gtk-doc to build documentation [default=yes]], enable_gtk_doc="$enableval", enable_gtk_doc=yes)

AM_CONDITIONAL(ENABLE_GTK_DOC, test x$enable_gtk_doc = xyes)
AM_CONDITIONAL(GTK_DOC_USE_LIBTOOL, test x$use_libtool = xyes)
# end of gtk-doc check

AC_CONFIG_FILES([
	Makefile
	po/Makefile.in
	libgksu/Makefile
	libgksu/libgksu2.pc
	libgksuui/Makefile
	gksu-properties/Makefile
	docs/Makefile
	])
AC_OUTPUT
