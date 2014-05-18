AUTOMAKE_OPTIONS = 1.6

## Process this file with automake to produce Makefile.in

# This is a blank Makefile.am for using gtk-doc.
# Copy this to your project's API docs directory and modify the variables to
# suit your project. See the GTK+ Makefiles in gtk+/docs/reference for examples
# of using the various options.

# The name of the module, e.g. 'glib'.
DOC_MODULE=$(PACKAGE)

# The top-level SGML file. Change it if you want.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code. Relative to $(srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR=../libgksu/

# Extra options to pass to gtkdoc-scanobj or gtkdoc-scangobj.
SCANOBJ_OPTIONS=

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=

# Used for dependencies.
HFILE_GLOB=$(top_srcdir)/libgksu/*.h
CFILE_GLOB=$(top_srcdir)/libgksu/*.c

# Header files to ignore when scanning.
IGNORE_HFILES= glib.h defines.h config.h libintl.h eval-plural.h gettextP.h gmo.h hash-string.h loadinfo.h localcharset.h os2compat.h plural-exp.h printf-args.h printf-parse.h relocatable.h vasnprintf.h vasnwprintf.h wprintf-parse.h xsize.h

# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files =

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = -I../libgksu `pkg-config --cflags gobject-2.0`
GTKDOC_LIBS = ../libgksu/.libs/libgksu-1.2.a `pkg-config --libs gobject-2.0`

# GTKDOC_CC=$(LIBTOOL) --mode=compile $(CC)
# GTKDOC_LD=$(LIBTOOL) --mode=link $(CC)

# If you need to override some of the declarations, place them in the
# $(DOC_MODULE)-overrides.txt file and uncomment the second line here.
DOC_OVERRIDES =
#DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt

man_MANS = gksu-properties.1

include gtk-doc.make

EXTRA_DIST += gksu-properties.1
