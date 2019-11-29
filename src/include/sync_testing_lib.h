/*
  This file is part of TALER
  (C) 2018, 2019 Taler Systems SA

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
 * @file include/sync_testing_lib.h
 * @brief API for writing an interpreter to test SYNC components
 * @author Christian Grothoff <christian@grothoff.org>
 */
#ifndef SYNC_TESTING_LIB_H
#define SYNC_TESTING_LIB_H

#include "sync_service.h"
#include <gnunet/gnunet_json_lib.h>
#include <taler/taler_testing_lib.h>
#include <microhttpd.h>

/**
 * Index used in #SYNC_TESTING_get_trait_hash() for the current hash.
 */
#define SYNC_TESTING_TRAIT_HASH_CURRENT 0

/**
 * Index used in #SYNC_TESTING_get_trait_hash() for the previous hash.
 */
#define SYNC_TESTING_TRAIT_HASH_PREVIOUS 1


/**
 * Obtain a hash from @a cmd.
 *
 * @param cmd command to extract the number from.
 * @param index the number's index number, #SYNC_TESTING_TRAIT_HASH_CURRENT or
 *          #SYNC_TESTING_TRAIT_HASH_PREVIOUS
 * @param h[out] set to the hash coming from @a cmd.
 * @return #GNUNET_OK on success.
 */
int
SYNC_TESTING_get_trait_hash (const struct TALER_TESTING_Command *cmd,
                             unsigned int index,
                             const struct GNUNET_HashCode **h);


/**
 * Offer a hash.
 *
 * @param index the number's index number.
 * @param h the hash to offer.
 * @return #GNUNET_OK on success.
 */
struct TALER_TESTING_Trait
TALER_TESTING_make_trait_hash (unsigned int index,
                               const struct GNUNET_HashCode *h);


/**
 * Obtain an account public key from @a cmd.
 *
 * @param cmd command to extract the public key from.
 * @param index usually 0
 * @param pub[out] set to the account public key used in @a cmd.
 * @return #GNUNET_OK on success.
 */
int
SYNC_TESTING_get_trait_account_pub (const struct TALER_TESTING_Command *cmd,
                                    unsigned int index,
                                    const struct SYNC_AccountPublicKeyP **pub);


/**
 * Offer an account public key.
 *
 * @param index usually zero
 * @param h the account_pub to offer.
 * @return #GNUNET_OK on success.
 */
struct TALER_TESTING_Trait
TALER_TESTING_make_trait_account_pub (unsigned int index,
                                      const struct SYNC_AccountPublicKeyP *h);


/**
 * Obtain an account private key from @a cmd.
 *
 * @param cmd command to extract the number from.
 * @param index must be 0
 * @param priv[out] set to the account private key used in @a cmd.
 * @return #GNUNET_OK on success.
 */
int
SYNC_TESTING_get_trait_account_priv (const struct TALER_TESTING_Command *cmd,
                                     unsigned int index,
                                     const struct
                                     SYNC_AccountPrivateKeyP **priv);


/**
 * Offer an account private key.
 *
 * @param index usually zero
 * @param priv the account_priv to offer.
 * @return #GNUNET_OK on success.
 */
struct TALER_TESTING_Trait
TALER_TESTING_make_trait_account_priv (unsigned int index,
                                       const struct
                                       SYNC_AccountPrivateKeyP *priv);


/**
 * Start the sync backend process.  Assume the port
 * is available and the database is clean.  Use the "prepare
 * sync" function to do such tasks.
 *
 * @param config_filename configuration filename.
 *
 * @return the process, or NULL if the process could not
 *         be started.
 */
struct GNUNET_OS_Process *
TALER_TESTING_run_sync (const char *config_filename,
                        const char *sync_url);


/**
 * Prepare the sync execution.  Create tables and check if
 * the port is available.
 *
 * @param config_filename configuration filename.
 * @return the base url, or NULL upon errors.  Must be freed
 *         by the caller.
 */
char *
TALER_TESTING_prepare_sync (const char *config_filename);


/**
 * Make the "backup download" command for a non-existent upload.
 *
 * @param label command label
 * @param sync_url base URL of the sync serving
 *        the policy store request.
 * @return the command
 */
struct TALER_TESTING_Command
SYNC_TESTING_cmd_backup_nx (const char *label,
                            const char *sync_url);


/**
 * Make the "backup download" command.
 *
 * @param label command label
 * @param sync_url base URL of the sync serving
 *        the policy store request.
 * @param http_status expected HTTP status.
 * @param upload_ref reference to upload command
 * @return the command
 */
struct TALER_TESTING_Command
SYNC_TESTING_cmd_backup_download (const char *label,
                                  const char *sync_url,
                                  unsigned int http_status,
                                  const char *upload_ref);


/**
 * Types of options for performing the upload. Used as a bitmask.
 */
enum SYNC_TESTING_UploadOption
{
  /**
   * Do everything by the book.
   */
  SYNC_TESTING_UO_NONE = 0,

  /**
   * Use random hash for previous upload instead of correct
   * previous hash.
   */
  SYNC_TESTING_UO_PREV_HASH_WRONG = 1,

  /**
   * Request payment.
   */
  SYNC_TESTING_UO_REQUEST_PAYMENT = 2,

  /**
   * Reference payment order ID from linked previous upload.
   */
  SYNC_TESTING_UO_REFERENCE_ORDER_ID = 4


};


/**
 * Make the "backup upload" command.
 *
 * @param label command label
 * @param sync_url base URL of the sync serving
 *        the policy store request.
 * @param prev_upload reference to a previous upload we are
 *        supposed to update, NULL for none
 * @param http_status expected HTTP status.
 * @param pub account identifier
 * @param payment_id payment identifier
 * @param policy_data recovery data to post
 *
 * @return the command
 */
struct TALER_TESTING_Command
SYNC_TESTING_cmd_backup_upload (const char *label,
                                const char *sync_url,
                                const char *prev_upload,
                                enum SYNC_TESTING_UploadOption uo,
                                unsigned int http_status,
                                const void *backup_data,
                                size_t backup_data_size);

#endif
