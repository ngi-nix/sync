# This Makefile.am is in the public domain
AM_CPPFLAGS = -I$(top_srcdir)/src/include

pkgcfgdir = $(prefix)/share/sync/config.d/

pkgcfg_DATA = \
  sync.conf

bin_PROGRAMS = \
  sync-httpd

sync_httpd_SOURCES = \
  sync-httpd.c sync-httpd.h \
  sync-httpd_backup.c sync-httpd_backup.h \
  sync-httpd_backup_post.c \
  sync-httpd_mhd.c sync-httpd_mhd.h \
  sync-httpd_terms.c sync-httpd_terms.h
sync_httpd_LDADD = \
  $(top_builddir)/src/syncdb/libsyncdb.la \
  -lmicrohttpd \
  -ljansson \
  -ltalermerchant \
  -ltalermhd \
  -ltalerjson \
  -ltalerutil \
  -lgnunetcurl \
  -lgnunetjson \
  -lgnunetutil

EXTRA_DIST = \
  $(pkgcfg_DATA)
