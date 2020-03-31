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
#include "sync_service.h"
#include "sync_database_plugin.h"
#include "sync_database_lib.h"


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
 * Function called on all pending payments for an account.
 *
 * @param cls closure
 * @param timestamp for how long have we been waiting
 * @param order_id order id in the backend
 * @param amount how much is the order for
 */
static void
payment_it (void *cls,
            struct GNUNET_TIME_Absolute timestamp,
            const char *order_id,
            const struct TALER_Amount *amount)
{
  GNUNET_assert (NULL == cls);
  GNUNET_assert (0 == strcmp (order_id,
                              "fake-order-2"));
}


/**
 * Main function that will be run by the scheduler.
 *
 * @param cls closure with config
 */
static void
run (void *cls)
{
  struct GNUNET_CONFIGURATION_Handle *cfg = cls;
  struct TALER_Amount amount;
  struct SYNC_AccountPublicKeyP account_pub;
  struct SYNC_AccountSignatureP account_sig;
  struct SYNC_AccountSignatureP account_sig2;
  struct GNUNET_HashCode h;
  struct GNUNET_HashCode h2;
  struct GNUNET_HashCode h3;
  struct GNUNET_HashCode r;
  struct GNUNET_HashCode r2;
  struct GNUNET_TIME_Absolute ts;
  size_t bs;
  void *b = NULL;

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
  memset (&account_pub, 1, sizeof (account_pub));
  memset (&account_sig, 2, sizeof (account_sig));
  GNUNET_CRYPTO_hash ("data", 4, &h);
  GNUNET_CRYPTO_hash ("DATA", 4, &h2);
  GNUNET_CRYPTO_hash ("ATAD", 4, &h3);
  GNUNET_assert (GNUNET_OK ==
                 TALER_string_to_amount ("EUR:1",
                                         &amount));
  FAILIF (SYNC_DB_ONE_RESULT !=
          plugin->store_payment_TR (plugin->cls,
                                    &account_pub,
                                    "fake-order",
                                    &amount));
  FAILIF (SYNC_DB_ONE_RESULT !=
          plugin->increment_lifetime_TR (plugin->cls,
                                         &account_pub,
                                         "fake-order",
                                         GNUNET_TIME_UNIT_MINUTES));
  FAILIF (SYNC_DB_ONE_RESULT !=
          plugin->store_backup_TR (plugin->cls,
                                   &account_pub,
                                   &account_sig,
                                   &h,
                                   4,
                                   "data"));
  FAILIF (SYNC_DB_NO_RESULTS !=
          plugin->store_backup_TR (plugin->cls,
                                   &account_pub,
                                   &account_sig,
                                   &h,
                                   4,
                                   "data"));
  FAILIF (SYNC_DB_ONE_RESULT !=
          plugin->update_backup_TR (plugin->cls,
                                    &account_pub,
                                    &h,
                                    &account_sig,
                                    &h2,
                                    4,
                                    "DATA"));
  FAILIF (SYNC_DB_OLD_BACKUP_MISMATCH !=
          plugin->update_backup_TR (plugin->cls,
                                    &account_pub,
                                    &h,
                                    &account_sig,
                                    &h3,
                                    4,
                                    "ATAD"));
  FAILIF (SYNC_DB_NO_RESULTS !=
          plugin->update_backup_TR (plugin->cls,
                                    &account_pub,
                                    &h,
                                    &account_sig,
                                    &h2,
                                    4,
                                    "DATA"));
  FAILIF (SYNC_DB_ONE_RESULT !=
          plugin->lookup_account_TR (plugin->cls,
                                     &account_pub,
                                     &r));
  FAILIF (0 != GNUNET_memcmp (&r,
                              &h2));
  FAILIF (SYNC_DB_ONE_RESULT !=
          plugin->lookup_backup_TR (plugin->cls,
                                    &account_pub,
                                    &account_sig2,
                                    &r,
                                    &r2,
                                    &bs,
                                    &b));
  FAILIF (0 != GNUNET_memcmp (&r,
                              &h));
  FAILIF (0 != GNUNET_memcmp (&r2,
                              &h2));
  FAILIF (0 != GNUNET_memcmp (&account_sig2,
                              &account_sig));
  FAILIF (bs != 4);
  FAILIF (0 != memcmp (b,
                       "DATA",
                       4));
  GNUNET_free (b);
  b = NULL;
  FAILIF (0 !=
          plugin->lookup_pending_payments_by_account_TR (plugin->cls,
                                                         &account_pub,
                                                         &payment_it,
                                                         NULL));
  memset (&account_pub, 2, sizeof (account_pub));
  FAILIF (SYNC_DB_ONE_RESULT !=
          plugin->store_payment_TR (plugin->cls,
                                    &account_pub,
                                    "fake-order-2",
                                    &amount));
  FAILIF (1 !=
          plugin->lookup_pending_payments_by_account_TR (plugin->cls,
                                                         &account_pub,
                                                         &payment_it,
                                                         NULL));
  FAILIF (SYNC_DB_PAYMENT_REQUIRED !=
          plugin->store_backup_TR (plugin->cls,
                                   &account_pub,
                                   &account_sig,
                                   &h,
                                   4,
                                   "data"));
  FAILIF (SYNC_DB_ONE_RESULT !=
          plugin->increment_lifetime_TR (plugin->cls,
                                         &account_pub,
                                         "fake-order-2",
                                         GNUNET_TIME_UNIT_MINUTES));
  FAILIF (SYNC_DB_OLD_BACKUP_MISSING !=
          plugin->update_backup_TR (plugin->cls,
                                    &account_pub,
                                    &h,
                                    &account_sig,
                                    &h2,
                                    4,
                                    "DATA"));
  ts = GNUNET_TIME_relative_to_absolute (GNUNET_TIME_UNIT_YEARS);
  (void) GNUNET_TIME_round_abs (&ts);
  FAILIF (0 >
          plugin->gc (plugin->cls,
                      ts,
                      ts));
  memset (&account_pub, 1, sizeof (account_pub));
  FAILIF (SYNC_DB_NO_RESULTS !=
          plugin->lookup_backup_TR (plugin->cls,
                                    &account_pub,
                                    &account_sig2,
                                    &r,
                                    &r2,
                                    &bs,
                                    &b));

  result = 0;
drop:
  GNUNET_free_non_null (b);
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
