# This Makefile.am is in the public domain
AM_CPPFLAGS = -I$(top_srcdir)/src/include

if USE_COVERAGE
  AM_CFLAGS = --coverage -O0
  XLIB = -lgcov
endif

lib_LTLIBRARIES = \
  libsync.la 

libsync_la_LDFLAGS = \
  -version-info 0:0:0 \
  -no-undefined
libsync_la_SOURCES = \
  sync_api_curl_defaults.c sync_api_curl_defaults.h \
  sync_api_download.c \
  sync_api_upload.c
libsync_la_LIBADD = \
  -lgnunetcurl \
  -lgnunetjson \
  -ltalerjson \
  -ltalerutil \
  -lgnunetutil \
  -ljansson \
  $(XLIB)

