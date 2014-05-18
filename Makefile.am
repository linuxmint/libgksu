AUTOMAKE_OPTIONS = 1.7

DISTCLEANFILES = *~ intltool-extract intltool-merge intltool-update gksu.schemas

SUBDIRS = po libgksuui libgksu gksu-properties docs
DIST_SUBDIRS = $(SUBDIRS)

schemasdir       = $(GCONF_SCHEMA_FILE_DIR)
schemas_in_files = gksu.schemas.in
schemas_DATA     = $(schemas_in_files:.schemas.in=.schemas)
@INTLTOOL_SCHEMAS_RULE@

pixmapdir = $(datadir)/pixmaps
dist_pixmap_DATA = gksu.png

EXTRA_DIST = config.rpath mkinstalldirs intltool-extract.in intltool-merge.in intltool-update.in gksu.schemas.in

if GCONF_SCHEMAS_INSTALL
install-data-local:
        if test -z "$(DESTDIR)" ; then \
          for p in $(schemas_DATA) ; do \
            GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(srcdir)/$$p ; \
          done \
        fi
else
install-data-local:
endif
