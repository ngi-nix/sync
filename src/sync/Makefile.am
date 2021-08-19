# This Makefile.am is in the public domain
AM_CPPFLAGS = -I$(top_srcdir)/src/include

if USE_COVERAGE
  AM_CFLAGS = --coverage -O0
  XLIB = -lgcov
endif

pkgcfgdir = $(prefix)/share/sync/config.d/

pkgcfg_DATA = \
  sync.conf

bin_PROGRAMS = \
  sync-httpd

sync_httpd_SOURCES = \
  sync-httpd.c sync-httpd.h \
  sync-httpd_backup.c sync-httpd_backup.h \
  sync-httpd_backup_post.c \
  sync-httpd_config.c sync-httpd_config.h \
  sync-httpd_mhd.c sync-httpd_mhd.h
sync_httpd_LDADD = \
  $(top_builddir)/src/util/libsyncutil.la \
  $(top_builddir)/src/syncdb/libsyncdb.la \
  -lmicrohttpd \
  -ljansson \
  -ltalermerchant \
  -ltalermhd \
  -ltalerjson \
  -ltalerutil \
  -lgnunetcurl \
  -lgnunetjson \
  -lgnunetutil \
  $(XLIB)

EXTRA_DIST = \
  $(pkgcfg_DATA)
