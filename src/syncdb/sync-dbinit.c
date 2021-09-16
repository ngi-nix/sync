/*
  This file is part of TALER
  Copyright (C) 2019 Taler Systems SA

  TALER is free software; you can redistribute it and/or modify it under the
  terms of the GNU General Public License as published by the Free Software
  Foundation; either version 3, or (at your option) any later version.

  TALER is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along with
  TALER; see the file COPYING.  If not, see <http://www.gnu.org/licenses/>
*/
/**
 * @file util/sync-dbinit.c
 * @brief Create tables for the sync database.
 * @author Christian Grothoff
 */
#include "platform.h"
#include <gnunet/gnunet_util_lib.h>
#include "sync_util.h"
#include "sync_database_lib.h"


/**
 * Return value from main().
 */
static int global_ret;

/**
 * -r option: do full DB reset
 */
static int reset_db;

/**
 * -g option: do GC reset
 */
static int gc_db;


/**
 * Main function that will be run.
 *
 * @param cls closure
 * @param args remaining command-line arguments
 * @param cfgfile name of the configuration file used (for saving, can be NULL!)
 * @param cfg configuration
 */
static void
run (void *cls,
     char *const *args,
     const char *cfgfile,
     const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  struct SYNC_DatabasePlugin *plugin;

  if (NULL ==
      (plugin = SYNC_DB_plugin_load (cfg)))
  {
    fprintf (stderr,
             "Failed to initialize database plugin.\n");
    global_ret = EXIT_NOTINSTALLED;
    return;
  }
  if (reset_db)
  {
    if (GNUNET_OK != plugin->drop_tables (plugin->cls))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "Could not drop tables as requested. Either database was not yet initialized, or permission denied. Consult the logs. Will still try to create new tables.\n");
    }
  }
  if (GNUNET_OK !=
      plugin->create_tables (plugin->cls))
  {
    global_ret = EXIT_FAILURE;
    SYNC_DB_plugin_unload (plugin);
    return;
  }
  if (gc_db)
  {
    struct GNUNET_TIME_Absolute now;
    struct GNUNET_TIME_Absolute ancient;

    now = GNUNET_TIME_absolute_get ();
    ancient = GNUNET_TIME_absolute_subtract (now,
                                             GNUNET_TIME_relative_multiply (
                                               GNUNET_TIME_UNIT_YEARS,
                                               6));
    if (0 >
        plugin->gc (plugin->cls,
                    now,
                    ancient))
    {
      fprintf (stderr,
               "Garbage collection failed!\n");
      global_ret = EXIT_FAILURE;
    }
  }
  SYNC_DB_plugin_unload (plugin);
}


/**
 * The main function of the database initialization tool.
 * Used to initialize the Sync' database.
 *
 * @param argc number of arguments from the command line
 * @param argv command line arguments
 * @return 0 ok, non-zero on error
 */
int
main (int argc,
      char *const *argv)
{
  struct GNUNET_GETOPT_CommandLineOption options[] = {
    GNUNET_GETOPT_option_flag ('r',
                               "reset",
                               "reset database (DANGEROUS: all existing data is lost!)",
                               &reset_db),
    GNUNET_GETOPT_option_flag ('g',
                               "garbagecollect",
                               "remove state data from database",
                               &gc_db),
    GNUNET_GETOPT_OPTION_END
  };
  enum GNUNET_GenericReturnValue ret;

  /* FIRST get the libtalerutil initialization out
     of the way. Then throw that one away, and force
     the SYNC defaults to be used! */
  (void) TALER_project_data_default ();
  GNUNET_OS_init (SYNC_project_data_default ());
  ret = GNUNET_PROGRAM_run (argc, argv,
                            "sync-dbinit",
                            "Initialize sync database",
                            options,
                            &run, NULL);
  if (GNUNET_SYSERR == ret)
    return EXIT_INVALIDARGUMENT;
  if (GNUNET_NO == ret)
    return EXIT_SUCCESS;
  return global_ret;
}


/* end of sync-dbinit.c */
