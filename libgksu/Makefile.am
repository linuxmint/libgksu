AM_CFLAGS = -g -O2 -Wall
INCLUDES = ${LIBGKSU_CFLAGS}
AM_CPPFLAGS = -DLOCALEDIR=\"$(datadir)/locale\" -DDATA_DIR=\"$(datadir)\" -DPREFIX=\"$(prefix)\"

if SUDO_FORKPTY
AM_CPPFLAGS += -DSUDO_FORKPTY
endif

lib_LTLIBRARIES = libgksu2.la
libgksu2_la_SOURCES = libgksu.c libgksu.h
# 0.0.0 -> major.minor.micro
# major -> breaks backward compatibility (changes to existing ABI)
# minor -> keeps compatibility (additions to the API)
# micro -> no change to the API/ABI
libgksu2_la_LIBADD = ../libgksuui/libgksuui1.0.la -lutil ${LIBGKSU_LIBS}
libgksu2_la_LDFLAGS = -version-info 0:2:0 -Wl,-O1
if USE_VERSION_SCRIPT
libgksu2_la_LDFLAGS += -Wl,--version-script=libgksu.ver
endif

noinst_HEADERS = defines.h

include_HEADERS = gksu.h libgksu.h
includedir = ${prefix}/include/${PACKAGE}

pkgconfigdir = ${libdir}/pkgconfig
pkgconfig_DATA = libgksu2.pc

pkglibexecdir = ${libdir}/${PACKAGE}
pkglibexec_PROGRAMS = gksu-run-helper
gksu_run_helper_LDFLAGS =
gksu_run_helper_LDADD = $(GLIB_LIBS)
gksu_run_helper_SOURCES = gksu-run-helper.c

noinst_PROGRAMS = test-gksu
test_gksu_SOURCES = test-gksu.c
test_gksu_LDADD = libgksu2.la $(GTK_LIBS)
test_gksu_LDFLAGS =

EXTRA_DIST = libgksu.ver
