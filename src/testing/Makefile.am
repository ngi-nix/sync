# This Makefile.am is in the public domain
AM_CPPFLAGS = -I$(top_srcdir)/src/include

if USE_COVERAGE
  AM_CFLAGS = --coverage -O0
  XLIB = -lgcov
endif

lib_LTLIBRARIES = \
  libsynctesting.la

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
  $(top_builddir)/src/lib/libsync.la \
  -ltalermerchant \
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

EXTRA_DIST = \
  test_sync_api.conf \
  test_sync_api_home/.config/taler/exchange/account-2.json \
  test_sync_api_home/.local/share/taler/exchange-offline/master.priv

test_sync_api_SOURCES = \
  test_sync_api.c
test_sync_api_LDADD = \
  $(top_builddir)/src/lib/libsync.la \
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
  -ljansson \
  $(XLIB)
