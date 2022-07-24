# This Makefile.am is in the public domain
AM_CPPFLAGS = -I$(top_srcdir)/src/include

pkgcfgdir = $(prefix)/share/sync/config.d/

pkgcfg_DATA = \
  sync_db_postgres.conf

plugindir = $(libdir)/sync

if HAVE_POSTGRESQL
if HAVE_GNUNETPQ
plugin_LTLIBRARIES = \
  libsync_plugin_db_postgres.la
endif
endif

if USE_COVERAGE
  AM_CFLAGS = --coverage -O0
  XLIB = -lgcov
endif

sqldir = $(prefix)/share/sync/sql/

sql_DATA = \
  versioning.sql \
  sync-0001.sql \
  drop.sql

bin_PROGRAMS = \
  sync-dbinit

sync_dbinit_SOURCES = \
  sync-dbinit.c
sync_dbinit_LDADD = \
  $(LIBGCRYPT_LIBS) \
  $(top_builddir)/src/util/libsyncutil.la \
  libsyncdb.la \
  -ltalerutil \
  -lgnunetutil \
  $(XLIB)

lib_LTLIBRARIES = \
  libsyncdb.la
libsyncdb_la_SOURCES = \
  sync_db_plugin.c
libsyncdb_la_LIBADD = \
  $(top_builddir)/src/util/libsyncutil.la \
  -lgnunetpq \
  -lpq \
  -lgnunetutil \
  -lltdl \
  $(XLIB)
libsyncdb_la_LDFLAGS = \
   $(POSTGRESQL_LDFLAGS) \
   -version-info 0:0:0 \
   -no-undefined

libsync_plugin_db_postgres_la_SOURCES = \
  plugin_syncdb_postgres.c
libsync_plugin_db_postgres_la_LIBADD = \
  $(LTLIBINTL)
libsync_plugin_db_postgres_la_LDFLAGS = \
  $(SYNC_PLUGIN_LDFLAGS) \
  -lgnunetpq \
  -lpq \
  -ltalerpq \
  -lgnunetutil \
  $(XLIB)

check_PROGRAMS = \
 $(TESTS)

test_sync_db_postgres_SOURCES = \
  test_sync_db.c
test_sync_db_postgres_LDFLAGS = \
  $(top_builddir)/src/util/libsyncutil.la \
  libsyncdb.la \
  -lgnunetutil \
  -lgnunetpq \
  -ltalerutil \
  $(XLIB)

AM_TESTS_ENVIRONMENT=export SYNC_PREFIX=$${SYNC_PREFIX:-@libdir@};export PATH=$${SYNC_PREFIX:-@prefix@}/bin:$$PATH;
TESTS = \
  test_sync_db-postgres

EXTRA_DIST = \
  $(pkgcfg_DATA) \
  test_sync_db_postgres.conf
