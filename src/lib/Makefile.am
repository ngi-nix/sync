# This Makefile.am is in the public domain
AM_CPPFLAGS = -I$(top_srcdir)/src/include

if USE_COVERAGE
  AM_CFLAGS = --coverage -O0
  XLIB = -lgcov
endif

lib_LTLIBRARIES = \
  libsync.la \
  libsynctesting.la

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

libsynctesting_la_LDFLAGS = \
  -version-info 0:0:0 \
  -no-undefined
libsynctesting_la_SOURCES = \
  testing_api_cmd_backup_download.c \
  testing_api_cmd_backup_upload.c \
  testing_api_helpers.c
libsynctesting_la_LIBADD = \
  libsync.la \
  -ltalerexchange \
  -ltalerjson \
  -ltalerutil \
  -lgnunetcurl \
  -lgnunetjson \
  -lgnunetutil \
  -ljansson \
  -ltalertesting \
  $(XLIB)

if HAVE_LIBCURL
libsync_la_LIBADD += -lcurl
else
if HAVE_LIBGNURL
libsync_la_LIBADD += -lgnurl
endif
endif
