/*
  This file is part of
  (C) 2019 Taler Systems SA

  TALER is free software; you can redistribute it and/or modify it under the
  terms of the GNU Lesser General Public License as published by the Free Software
  Foundation; either version 3, or (at your option) any later version.

  TALER is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along with
  TALER; see the file COPYING.  If not, see <http://www.gnu.org/licenses/>
*/
/**
 * @file sync/test_sync_db.c
 * @brief testcase for sync postgres db plugin
 * @author Christian Grothoff
 */

#include "platform.h"
#include <gnunet/gnunet_util_lib.h>
#include <taler/taler_util.h>
#include "sync_database_plugin.h"
#include "sync_database_lib.h"
#include "sync_error_codes.h"
#include <uuid/uuid.h>


#define FAILIF(cond)                            \
  do {                                          \
    if (! (cond)) { break;}                       \
    GNUNET_break (0);                           \
    goto drop;                                     \
  } while (0)

#define RND_BLK(ptr)                                                    \
  GNUNET_CRYPTO_random_block (GNUNET_CRYPTO_QUALITY_WEAK, ptr, sizeof (*ptr))

/**
 * Global return value for the test.  Initially -1, set to 0 upon
 * completion.   Other values indicate some kind of error.
 */
static int result;

/**
 * Handle to the plugin we are testing.
 */
static struct SYNC_DatabasePlugin *plugin;

/**
 * User public key, set to a random value
 */
static struct SYNC_AccountPubP accountPubP;


/**
 * Main function that will be run by the scheduler.
 *
 * @param cls closure with config
 */
static void
run (void *cls)
{
  struct GNUNET_CONFIGURATION_Handle *cfg = cls;

  if (NULL == (plugin = SYNC_DB_plugin_load (cfg)))
  {
    result = 77;
    return;
  }
  if (GNUNET_OK != plugin->drop_tables (plugin->cls))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Dropping tables failed\n");
    result = 77;
    return;
  }
  SYNC_DB_plugin_unload (plugin);
  if (NULL == (plugin = SYNC_DB_plugin_load (cfg)))
  {
    result = 77;
    return;
  }

  // FIXME: test logic here!

  GNUNET_break (GNUNET_OK ==
                plugin->drop_tables (plugin->cls));
  SYNC_DB_plugin_unload (plugin);
  plugin = NULL;
}


int
main (int argc,
      char *const argv[])
{
  const char *plugin_name;
  char *config_filename;
  char *testname;
  struct GNUNET_CONFIGURATION_Handle *cfg;

  result = -1;
  if (NULL == (plugin_name = strrchr (argv[0], (int) '-')))
  {
    GNUNET_break (0);
    return -1;
  }
  GNUNET_log_setup (argv[0], "DEBUG", NULL);
  plugin_name++;
  (void) GNUNET_asprintf (&testname,
                          "%s",
                          plugin_name);
  (void) GNUNET_asprintf (&config_filename,
                          "test_sync_db_%s.conf",
                          testname);
  cfg = GNUNET_CONFIGURATION_create ();
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_parse (cfg,
                                  config_filename))
  {
    GNUNET_break (0);
    GNUNET_free (config_filename);
    GNUNET_free (testname);
    return 2;
  }
  GNUNET_SCHEDULER_run (&run, cfg);
  GNUNET_CONFIGURATION_destroy (cfg);
  GNUNET_free (config_filename);
  GNUNET_free (testname);
  return result;
}


/* end of test_sync_db.c */
