/*
  This file is part of TALER
  Copyright (C) 2014-2019 Taler Systems SA

  TALER is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 3, or
  (at your option) any later version.

  TALER is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public
  License along with TALER; see the file COPYING.  If not, see
  <http://www.gnu.org/licenses/>
*/
/**
 * @file lib/test_sync_api.c
 * @brief testcase to test sync's HTTP API interface
 * @author Christian Grothoff
 */
#include "platform.h"
#include <taler/taler_util.h>
#include <taler/taler_signatures.h>
#include <taler/taler_exchange_service.h>
#include <taler/taler_json_lib.h>
#include <gnunet/gnunet_util_lib.h>
#include <microhttpd.h>
#include <taler/taler_bank_service.h>
#include <taler/taler_fakebank_lib.h>
#include <taler/taler_testing_lib.h>
#include <taler/taler_merchant_testing_lib.h>
#include <taler/taler_error_codes.h>
#include "sync_service.h"
#include "sync_testing_lib.h"

/**
 * Configuration file we use.  One (big) configuration is used
 * for the various components for this test.
 */
#define CONFIG_FILE "test_sync_api.conf"

/**
 * Exchange base URL.  Could also be taken from config.
 */
#define EXCHANGE_URL "http://localhost:8081/"

/**
 * URL of the fakebank.
 */
static char *fakebank_url;

/**
 * Merchant base URL.
 */
static char *merchant_url;

/**
 * Sync base URL.
 */
static char *sync_url;

/**
 * Merchant process.
 */
static struct GNUNET_OS_Process *merchantd;

/**
 * Sync-httpd process.
 */
static struct GNUNET_OS_Process *syncd;

/**
 * Exchange base URL.
 */
static char *exchange_url;

/**
 * Auditor base URL; only used to fix FTBFS.
 */
static char *auditor_url;

/**
 * Account number of the exchange at the bank.
 */
#define EXCHANGE_ACCOUNT_NO 2

/**
 * Account number of some user.
 */
#define USER_ACCOUNT_NO 62

/**
 * Account number used by the merchant
 */
#define MERCHANT_ACCOUNT_NO 3

/**
 * User name. Never checked by fakebank.
 */
#define USER_LOGIN_NAME "user42"

/**
 * User password. Never checked by fakebank.
 */
#define USER_LOGIN_PASS "pass42"

/**
 * Execute the taler-exchange-wirewatch command with
 * our configuration file.
 *
 * @param label label to use for the command.
 */
#define CMD_EXEC_WIREWATCH(label) \
  TALER_TESTING_cmd_exec_wirewatch (label, CONFIG_FILE)

/**
 * Execute the taler-exchange-aggregator command with
 * our configuration file.
 *
 * @param label label to use for the command.
 */
#define CMD_EXEC_AGGREGATOR(label) \
  TALER_TESTING_cmd_exec_aggregator (label, CONFIG_FILE)

/**
 * Run wire transfer of funds from some user's account to the
 * exchange.
 *
 * @param label label to use for the command.
 * @param amount amount to transfer, i.e. "EUR:1"
 * @param url exchange_url
 */
#define CMD_TRANSFER_TO_EXCHANGE(label,amount) \
  TALER_TESTING_cmd_fakebank_transfer (label, amount, \
                                       fakebank_url, USER_ACCOUNT_NO, \
                                       EXCHANGE_ACCOUNT_NO, \
                                       USER_LOGIN_NAME, USER_LOGIN_PASS, \
                                       EXCHANGE_URL)

/**
 * Run wire transfer of funds from some user's account to the
 * exchange.
 *
 * @param label label to use for the command.
 * @param amount amount to transfer, i.e. "EUR:1"
 */
#define CMD_TRANSFER_TO_EXCHANGE_SUBJECT(label,amount,subject) \
  TALER_TESTING_cmd_fakebank_transfer_with_subject \
    (label, amount, fakebank_url, USER_ACCOUNT_NO, \
    EXCHANGE_ACCOUNT_NO, USER_LOGIN_NAME, USER_LOGIN_PASS, \
    subject)


/**
 * Main function that will tell the interpreter what commands to
 * run.
 *
 * @param cls closure
 */
static void
run (void *cls,
     struct TALER_TESTING_Interpreter *is)
{
  struct TALER_TESTING_Command commands[] = {
    /**
     * Move money to the exchange's bank account.
     */
    CMD_TRANSFER_TO_EXCHANGE ("create-reserve-1",
                              "EUR:10.02"),
    /**
     * Make a reserve exist, according to the previous
     * transfer.
     */
    CMD_EXEC_WIREWATCH ("wirewatch-1"),
    TALER_TESTING_cmd_withdraw_amount
      ("withdraw-coin-1",
      "create-reserve-1",
      "EUR:5",
      MHD_HTTP_OK),
    TALER_TESTING_cmd_withdraw_amount
      ("withdraw-coin-2",
      "create-reserve-1",
      "EUR:5",
      MHD_HTTP_OK),
    /* Failed download: no backup exists */
    SYNC_TESTING_cmd_backup_nx ("backup-download-nx",
                                sync_url),
    /* Failed upload: need to pay */
    SYNC_TESTING_cmd_backup_upload ("backup-upload-1",
                                    sync_url,
                                    NULL /* prev upload */,
                                    NULL /* last upload */,
                                    SYNC_TESTING_UO_NONE,
                                    MHD_HTTP_PAYMENT_REQUIRED,
                                    "Test-1",
                                    strlen ("Test-1")),
    /* what would we have to pay? */
    TALER_TESTING_cmd_proposal_lookup ("fetch-proposal",
                                       merchant_url,
                                       MHD_HTTP_OK,
                                       "backup-upload-1",
                                       NULL),
    /* make the payment */
    TALER_TESTING_cmd_pay ("pay-account",
                           merchant_url,
                           MHD_HTTP_OK,
                           "fetch-proposal",
                           "withdraw-coin-1",
                           "EUR:5",
                           "EUR:4.99", /* must match ANNUAL_FEE in config! */
                           "EUR:0.01"),
    /* now upload should succeed */
    SYNC_TESTING_cmd_backup_upload ("backup-upload-2",
                                    sync_url,
                                    "backup-upload-1",
                                    NULL,
                                    SYNC_TESTING_UO_NONE,
                                    MHD_HTTP_NO_CONTENT,
                                    "Test-1",
                                    strlen ("Test-1")),
    /* now updated upload should succeed */
    SYNC_TESTING_cmd_backup_upload ("backup-upload-3",
                                    sync_url,
                                    "backup-upload-2",
                                    NULL,
                                    SYNC_TESTING_UO_NONE,
                                    MHD_HTTP_NO_CONTENT,
                                    "Test-3",
                                    strlen ("Test-3")),
    /* Test download: succeeds! */
    SYNC_TESTING_cmd_backup_download ("download-3",
                                      sync_url,
                                      MHD_HTTP_OK,
                                      "backup-upload-3"),
    /* now updated upload should fail (conflict) */
    SYNC_TESTING_cmd_backup_upload ("backup-upload-3b",
                                    sync_url,
                                    "backup-upload-2",
                                    "backup-upload-3",
                                    SYNC_TESTING_UO_NONE,
                                    MHD_HTTP_CONFLICT,
                                    "Test-3b",
                                    strlen ("Test-3b")),
    /* now updated upload should fail (payment requested) */
    SYNC_TESTING_cmd_backup_upload ("backup-upload-4",
                                    sync_url,
                                    "backup-upload-3",
                                    "backup-upload-3",
                                    SYNC_TESTING_UO_REQUEST_PAYMENT,
                                    MHD_HTTP_PAYMENT_REQUIRED,
                                    "Test-4",
                                    strlen ("Test-4")),
    /* Test download: previous did NOT change the data on the server! */
    SYNC_TESTING_cmd_backup_download ("download-3",
                                      sync_url,
                                      MHD_HTTP_OK,
                                      "backup-upload-3"),

    TALER_TESTING_cmd_end ()
  };

  TALER_TESTING_run_with_fakebank (is,
                                   commands,
                                   fakebank_url);
}


int
main (int argc,
      char *const *argv)
{
  unsigned int ret;
  /* These environment variables get in the way... */
  unsetenv ("XDG_DATA_HOME");
  unsetenv ("XDG_CONFIG_HOME");

  GNUNET_log_setup ("test-sync-api",
                    "DEBUG",
                    NULL);
  if (NULL ==
      (fakebank_url = TALER_TESTING_prepare_fakebank
                        (CONFIG_FILE,
                        "account-exchange")))
    return 77;
  if (NULL ==
      (merchant_url = TALER_TESTING_prepare_merchant (CONFIG_FILE)))
    return 77;

  if (NULL ==
      (sync_url = SYNC_TESTING_prepare_sync (CONFIG_FILE)))
    return 77;

  TALER_TESTING_cleanup_files (CONFIG_FILE);

  switch (TALER_TESTING_prepare_exchange (CONFIG_FILE,
                                          &auditor_url,
                                          &exchange_url))
  {
  case GNUNET_SYSERR:
    GNUNET_break (0);
    return 1;
  case GNUNET_NO:
    return 77;

  case GNUNET_OK:

    if (NULL == (merchantd =
                   TALER_TESTING_run_merchant (CONFIG_FILE, merchant_url)))
      return 1;

    if (NULL == (syncd =
                   SYNC_TESTING_run_sync (CONFIG_FILE, sync_url)))
      return 1;

    ret = TALER_TESTING_setup_with_exchange (&run,
                                             NULL,
                                             CONFIG_FILE);

    GNUNET_OS_process_kill (merchantd, SIGTERM);
    GNUNET_OS_process_kill (syncd, SIGTERM);
    GNUNET_OS_process_wait (merchantd);
    GNUNET_OS_process_wait (syncd);
    GNUNET_OS_process_destroy (merchantd);
    GNUNET_OS_process_destroy (syncd);
    GNUNET_free (merchant_url);
    GNUNET_free (sync_url);

    if (GNUNET_OK != ret)
      return 1;
    break;
  default:
    GNUNET_break (0);
    return 1;
  }
  return 0;
}


/* end of test_sync_api.c */
