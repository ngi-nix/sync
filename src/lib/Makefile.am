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
  testing_api_helpers.c \
  testing_api_trait_account_pub.c \
  testing_api_trait_account_priv.c \
  testing_api_trait_hash.c
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

check_PROGRAMS = \
  test_sync_api

TESTS = \
  $(check_PROGRAMS)

test_sync_api_SOURCES = \
  test_sync_api.c
test_sync_api_LDADD = \
  libsync.la \
  libsynctesting.la \
  -ltalertesting \
  -ltalermerchanttesting \
  -ltalerfakebank \
  -ltalerbank \
  -ltalerexchange \
  -ltalermerchant \
  -ltalerjson \
  -ltalerutil \
  -lgnunetjson \
  -lgnunetcurl \
  -lgnunetutil \
  -ljansson

if HAVE_LIBCURL
libsync_la_LIBADD += -lcurl
else
if HAVE_LIBGNURL
libsync_la_LIBADD += -lgnurl
endif
endif
