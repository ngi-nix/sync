# This Makefile.am is in the public domain
AM_CPPFLAGS = -I$(top_srcdir)/src/include $(LIBGCRYPT_CFLAGS)

if USE_COVERAGE
  AM_CFLAGS = --coverage -O0
  XLIB = -lgcov
endif


pkgcfgdir = $(prefix)/share/sync/config.d/

bin_SCRIPTS = \
  sync-config

EXTRA_DIST = \
  $(bin_SCRIPTS)

lib_LTLIBRARIES = \
  libsyncutil.la

libsyncutil_la_SOURCES = \
  os_installation.c
libsyncutil_la_LIBADD = \
  -lgnunetutil \
  $(XLIB)
libsyncutil_la_LDFLAGS = \
  -version-info 0:0:0 \
  -export-dynamic -no-undefined
