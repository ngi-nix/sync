# This Makefile.am is in the public domain
AM_CPPFLAGS = -I$(top_srcdir)/src/include

pkgcfgdir = $(prefix)/share/taler/config.d/

pkgcfg_DATA = \
  sync.conf

bin_PROGRAMS = \
  sync-httpd

sync_httpd_SOURCES = \
  sync-httpd.c sync-httpd.h \
  sync-httpd_parsing.c sync-httpd_parsing.h \
  sync-httpd_responses.c sync-httpd_responses.h \
  sync-httpd_mhd.c sync-httpd_mhd.h \
  sync-httpd_policy.c sync-httpd_policy.h

sync_httpd_LDADD = \
  $(top_builddir)/src/stasis/libsyncdb.la \
  -lmicrohttpd \
  -ljansson \
  -lgnunetcurl \
  -lgnunetjson \
  -lgnunetutil

EXTRA_DIST = \
  $(pkgcfg_DATA)
