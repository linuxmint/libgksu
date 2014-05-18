AM_CFLAGS = -g -O2 -Wall
INCLUDES = `pkg-config --cflags gtk+-2.0 gconf-2.0`
AM_CPPFLAGS = -DLOCALEDIR=\"$(datadir)/locale\" -DDATA_DIR=\"$(datadir)\" -DPREFIX=\"$(prefix)\"

noinst_LTLIBRARIES = libgksuui1.0.la
libgksuui1_0_la_SOURCES = gksuui-dialog.c
libgksuui1_0_la_LDFLAGS = -Wl,-O1 `pkg-config --libs gtk+-2.0 gconf-2.0`

noinst_HEADERS = defines.h gksuui.h gksuui-dialog.h
includedir = ${prefix}/include/$(PACKAGE)

noinst_PROGRAMS = test-gksuui
test_gksuui_SOURCES = test-gksuui.c
test_gksuui_LDADD = libgksuui1.0.la
test_gksuui_LDFLAGS = `pkg-config --libs glib-2.0`
