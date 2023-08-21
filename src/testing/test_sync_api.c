/*
  This file is part of TALER
  Copyright (C) 2014-2023 Taler Systems SA

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
 * Account number of the exchange at the bank.
 */
#define EXCHANGE_ACCOUNT_NAME "2"

/**
 * Account number of some user.
 */
#define USER_ACCOUNT_NAME "62"

/**
 * Account number used by the merchant
 */
#define MERCHANT_ACCOUNT_NAME "3"

/**
 * Payto URI of the customer (payer).
 */
static char *payer_payto;

/**
 * Payto URI of the exchange (escrow account).
 */
static char *exchange_payto;

/**
 * Payto URI of the merchant (receiver).
 */
static char *merchant_payto;

/**
 * Configuration of the bank.
 */
static struct TALER_TESTING_Credentials cred;

/**
 * Merchant base URL.
 */
static const char *merchant_url = "http://localhost:8080/";

/**
 * Sync base URL.
 */
static const char *sync_url = "http://localhost:8084/";


/**
 * Execute the taler-exchange-wirewatch command with
 * our configuration file.
 *
 * @param label label to use for the command.
 */
static struct TALER_TESTING_Command
cmd_exec_wirewatch (char *label)
{
  return TALER_TESTING_cmd_exec_wirewatch (label,
                                           CONFIG_FILE);
}


/**
 * Run wire transfer of funds from some user's account to the
 * exchange.
 *
 * @param label label to use for the command.
 * @param amount amount to transfer, i.e. "EUR:1"
 * @param url exchange_url
 */
static struct TALER_TESTING_Command
cmd_transfer_to_exchange (const char *label,
                          const char *amount)
{
  return TALER_TESTING_cmd_admin_add_incoming (label,
                                               amount,
                                               &cred.ba,
                                               payer_payto);
}


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
    TALER_TESTING_cmd_run_fakebank ("run-fakebank",
                                    cred.cfg,
                                    "exchange-account-exchange"),
    TALER_TESTING_cmd_system_start ("start-taler",
                                    CONFIG_FILE,
                                    "-emb",
                                    "-u", "exchange-account-exchange",
                                    NULL),
    TALER_TESTING_cmd_get_exchange ("get-exchange",
                                    cred.cfg,
                                    NULL,
                                    true,
                                    true),
    TALER_TESTING_cmd_merchant_post_instances ("instance-create-default",
                                               merchant_url,
                                               "default",
                                               merchant_payto,
                                               MHD_HTTP_NO_CONTENT),

    /**
     * Move money to the exchange's bank account.
     */
    cmd_transfer_to_exchange ("create-reserve-1",
                              "EUR:10.02"),
    /**
     * Make a reserve exist, according to the previous
     * transfer.
     */
    cmd_exec_wirewatch ("wirewatch-1"),
    TALER_TESTING_cmd_withdraw_amount ("withdraw-coin-1",
                                       "create-reserve-1",
                                       "EUR:5",
                                       0,
                                       MHD_HTTP_OK),
    TALER_TESTING_cmd_withdraw_amount ("withdraw-coin-2",
                                       "create-reserve-1",
                                       "EUR:5",
                                       0,
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
    TALER_TESTING_cmd_merchant_claim_order ("fetch-proposal",
                                            merchant_url,
                                            MHD_HTTP_OK,
                                            "backup-upload-1",
                                            NULL),
    /* make the payment */
    TALER_TESTING_cmd_merchant_pay_order ("pay-account",
                                          merchant_url,
                                          MHD_HTTP_OK,
                                          "fetch-proposal",
                                          "withdraw-coin-1",
                                          "EUR:5",
                                          "EUR:4.99", /* must match ANNUAL_FEE in config! */
                                          "session-id"),
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
    SYNC_TESTING_cmd_backup_download ("download-3b",
                                      sync_url,
                                      MHD_HTTP_OK,
                                      "backup-upload-3"),

    TALER_TESTING_cmd_end ()
  };

  TALER_TESTING_run (is,
                     commands);
}


int
main (int argc,
      char *const *argv)
{
  (void) argc;
  payer_payto =
    "payto://x-taler-bank/localhost/" USER_ACCOUNT_NAME "?receiver-name=user";
  exchange_payto =
    "payto://x-taler-bank/localhost/" EXCHANGE_ACCOUNT_NAME
    "?receiver-name=exchange";
  merchant_payto =
    "payto://x-taler-bank/localhost/" MERCHANT_ACCOUNT_NAME
    "?receiver-name=merchant";
  return TALER_TESTING_main (argv,
                             "DEBUG",
                             CONFIG_FILE,
                             "exchange-account-exchange",
                             TALER_TESTING_BS_FAKEBANK,
                             &cred,
                             &run,
                             NULL);
}


/* end of test_sync_api.c */
