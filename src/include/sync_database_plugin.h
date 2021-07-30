/*
  This file is part of GNU Taler
  Copyright (C) 2019 Taler Systems SA

  Taler is free software; you can redistribute it and/or modify it under the
  terms of the GNU Lesser General Public License as published by the Free Software
  Foundation; either version 3, or (at your option) any later version.

  Taler is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along with
  Taler; see the file COPYING.GPL.  If not, see <http://www.gnu.org/licenses/>
*/
/**
 * @file include/sync_database_plugin.h
 * @brief database access for Sync
 * @author Christian Grothoff
 */
#ifndef SYNC_DATABASE_PLUGIN_H
#define SYNC_DATABASE_PLUGIN_H

#include <gnunet/gnunet_util_lib.h>
#include <gnunet/gnunet_db_lib.h>
#include "sync_service.h"
#include <jansson.h>
#include <taler/taler_util.h>


/**
 * Possible status codes returned from the SYNC database.
 */
enum SYNC_DB_QueryStatus
{
  /**
   * Client claimed to be updating an existing backup, but we have none.
   */
  SYNC_DB_OLD_BACKUP_MISSING = -5,

  /**
   * Update failed because the old backup hash does not match what we previously had in the DB.
   */
  SYNC_DB_OLD_BACKUP_MISMATCH = -4,

  /**
   * Account is unpaid / does not exist.
   */
  SYNC_DB_PAYMENT_REQUIRED = -3,

  /**
   * Hard database issue, retries futile.
   */
  SYNC_DB_HARD_ERROR = -2,

  /**
   * Soft database error, retrying may help.
   */
  SYNC_DB_SOFT_ERROR = -1,

  /**
   * Database succeeded, but no results.
   */
  SYNC_DB_NO_RESULTS = 0,

  /**
   * Database succeeded, one change or result.
   */
  SYNC_DB_ONE_RESULT = 1
};


/**
 * Function called on all pending payments for an account.
 *
 * @param cls closure
 * @param timestamp for how long have we been waiting
 * @param order_id order id in the backend
 * @param token claim token, or NULL for none
 * @param amount how much is the order for
 */
typedef void
(*SYNC_DB_PaymentPendingIterator)(void *cls,
                                  struct GNUNET_TIME_Absolute timestamp,
                                  const char *order_id,
                                  const struct TALER_ClaimTokenP *token,
                                  const struct TALER_Amount *amount);


/**
 * Handle to interact with the database.
 *
 * Functions ending with "_TR" run their OWN transaction scope
 * and MUST NOT be called from within a transaction setup by the
 * caller.  Functions ending with "_NT" require the caller to
 * setup a transaction scope.  Functions without a suffix are
 * simple, single SQL queries that MAY be used either way.
 */
struct SYNC_DatabasePlugin
{

  /**
   * Closure for all callbacks.
   */
  void *cls;

  /**
   * Name of the library which generated this plugin.  Set by the
   * plugin loader.
   */
  char *library_name;

  /**
   * Drop sync tables. Used for testcases.
   *
   * @param cls closure
   * @return #GNUNET_OK upon success; #GNUNET_SYSERR upon failure
   */
  int
  (*drop_tables) (void *cls);

  /**
   * Function called to perform "garbage collection" on the
   * database, expiring records we no longer require.  Deletes
   * all user records that are not paid up (and by cascade deletes
   * the associated recovery documents). Also deletes expired
   * truth and financial records older than @a fin_expire.
   *
   * @param cls closure
   * @param expire_backups backups older than the given time stamp should be garbage collected
   * @param expire_pending_payments payments still pending from since before
   *            this value should be garbage collected
   * @return transaction status
   */
  enum GNUNET_DB_QueryStatus
  (*gc)(void *cls,
        struct GNUNET_TIME_Absolute expire,
        struct GNUNET_TIME_Absolute expire_pending_payments);


  /**
   * Store backup. Only applicable for the FIRST backup under
   * an @a account_pub. Use @e update_backup_TR to update an
   * existing backup.
   *
   * @param cls closure
   * @param account_pub account to store @a backup under
   * @param account_sig signature affirming storage request
   * @param backup_hash hash of @a backup
   * @param backup_size number of bytes in @a backup
   * @param backup raw data to backup
   * @return transaction status
   */
  enum SYNC_DB_QueryStatus
  (*store_backup_TR)(void *cls,
                     const struct SYNC_AccountPublicKeyP *account_pub,
                     const struct SYNC_AccountSignatureP *account_sig,
                     const struct GNUNET_HashCode *backup_hash,
                     size_t backup_size,
                     const void *backup);


  /**
   * Store payment. Used to begin a payment, not indicative
   * that the payment actually was made. (That is done
   * when we increment the account's lifetime.)
   *
   * @param cls closure
   * @param account_pub account to store @a backup under
   * @param order_id order we created
   * @param token claim token, or NULL for none
   * @param amount how much we asked for
   * @return transaction status
   */
  enum SYNC_DB_QueryStatus
  (*store_payment_TR)(void *cls,
                      const struct SYNC_AccountPublicKeyP *account_pub,
                      const char *order_id,
                      const struct TALER_ClaimTokenP *token,
                      const struct TALER_Amount *amount);


  /**
   * Lookup pending payments by account.
   *
   * @param cls closure
   * @param account_pub account to look for pending payments under
   * @param it iterator to call on all pending payments
   * @param it_cls closure for @a it
   * @return transaction status
   */
  enum GNUNET_DB_QueryStatus
  (*lookup_pending_payments_by_account_TR)(void *cls,
                                           const struct
                                           SYNC_AccountPublicKeyP *account_pub,
                                           SYNC_DB_PaymentPendingIterator it,
                                           void *it_cls);

  /**
   * Update backup.
   *
   * @param cls closure
   * @param account_pub account to store @a backup under
   * @param account_sig signature affirming storage request
   * @param old_backup_hash hash of the previous backup (must match)
   * @param backup_hash hash of @a backup
   * @param backup_size number of bytes in @a backup
   * @param backup raw data to backup
   * @return transaction status
   */
  enum SYNC_DB_QueryStatus
  (*update_backup_TR)(void *cls,
                      const struct SYNC_AccountPublicKeyP *account_pub,
                      const struct GNUNET_HashCode *old_backup_hash,
                      const struct SYNC_AccountSignatureP *account_sig,
                      const struct GNUNET_HashCode *backup_hash,
                      size_t backup_size,
                      const void *backup);


  /**
   * Lookup an account and associated backup meta data.
   *
   * @param cls closure
   * @param account_pub account to store @a backup under
   * @param backup_hash[OUT] set to hash of @a backup
   * @return transaction status
   */
  enum SYNC_DB_QueryStatus
  (*lookup_account_TR)(void *cls,
                       const struct SYNC_AccountPublicKeyP *account_pub,
                       struct GNUNET_HashCode *backup_hash);


  /**
   * Obtain backup.
   *
   * @param cls closure
   * @param account_pub account to store @a backup under
   * @param account_sig[OUT] set to signature affirming storage request
   * @param prev_hash[OUT] set to hash of the previous @a backup (all zeros if none)
   * @param backup_hash[OUT] set to hash of @a backup
   * @param backup_size[OUT] set to number of bytes in @a backup
   * @param backup[OUT] set to raw data to backup, caller MUST FREE
   */
  enum SYNC_DB_QueryStatus
  (*lookup_backup_TR)(void *cls,
                      const struct SYNC_AccountPublicKeyP *account_pub,
                      struct SYNC_AccountSignatureP *account_sig,
                      struct GNUNET_HashCode *prev_hash,
                      struct GNUNET_HashCode *backup_hash,
                      size_t *backup_size,
                      void **backup);

  /**
   * Increment account lifetime and mark the associated payment
   * as successful.
   *
   * @param cls closure
   * @param account_pub which account received a payment
   * @param order_id order which was paid, must be unique and match pending payment
   * @param lifetime for how long is the account now paid (increment)
   * @return transaction status
   */
  enum SYNC_DB_QueryStatus
  (*increment_lifetime_TR)(void *cls,
                           const struct SYNC_AccountPublicKeyP *account_pub,
                           const char *order_id,
                           struct GNUNET_TIME_Relative lifetime);

};
#endif
