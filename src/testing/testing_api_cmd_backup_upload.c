/*
  This file is part of SYNC
  Copyright (C) 2014-2019 Taler Systems SA

  SYNC is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 3, or
  (at your option) any later version.

  SYNC is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public
  License along with SYNC; see the file COPYING.  If not, see
  <http://www.gnu.org/licenses/>
*/
/**
 * @file lib/testing_api_cmd_backup_upload.c
 * @brief command to upload data to the sync backend service.
 * @author Christian Grothoff
 */
#include "platform.h"
#include "sync_service.h"
#include "sync_testing_lib.h"
#include <taler/taler_util.h>
#include <taler/taler_testing_lib.h>
#include <taler/taler_merchant_service.h>
#include "sync_testing_lib.h"

/**
 * State for a "backup upload" CMD.
 */
struct BackupUploadState
{

  /**
   * Eddsa private key.
   */
  struct SYNC_AccountPrivateKeyP sync_priv;

  /**
   * Eddsa public key.
   */
  struct SYNC_AccountPublicKeyP sync_pub;

  /**
   * Hash of the previous upload (maybe bogus if
   * #SYNC_TESTING_UO_PREV_HASH_WRONG is set in @e uo).
   * Maybe all zeros if there was no previous upload.
   */
  struct GNUNET_HashCode prev_hash;

  /**
   * Hash of the current upload.
   */
  struct GNUNET_HashCode curr_hash;

  /**
   * The /backups POST operation handle.
   */
  struct SYNC_UploadOperation *uo;

  /**
   * URL of the sync backend.
   */
  const char *sync_url;

  /**
   * Previous upload, or NULL for none. Used to calculate what THIS
   * upload is based on.
   */
  const char *prev_upload;

  /**
   * Last upload, or NULL for none, usually same as @e prev_upload.
   * Used to check the response on #MHD_HTTP_CONFLICT.
   */
  const char *last_upload;

  /**
   * Payment order ID we got back, if any. Otherwise NULL.
   */
  char *payment_order_id;

  /**
   * Claim token we got back, if any. Otherwise all zeros.
   */
  struct TALER_ClaimTokenP token;

  /**
   * Payment order ID we are to provide in the request, may be NULL.
   */
  const char *payment_order_req;

  /**
   * The interpreter state.
   */
  struct TALER_TESTING_Interpreter *is;

  /**
   * The backup data we are uploading.
   */
  const void *backup;

  /**
   * Number of bytes in @e backup.
   */
  size_t backup_size;

  /**
   * Expected status code.
   */
  unsigned int http_status;

  /**
   * Options for how we are supposed to do the upload.
   */
  enum SYNC_TESTING_UploadOption uopt;

};


/**
 * Function called with the results of a #SYNC_upload().
 *
 * @param cls closure
 * @param ec Taler error code
 * @param http_status HTTP status of the request
 * @param ud details about the upload operation
 */
static void
backup_upload_cb (void *cls,
                  enum TALER_ErrorCode ec,
                  unsigned int http_status,
                  const struct SYNC_UploadDetails *ud)
{
  struct BackupUploadState *bus = cls;

  bus->uo = NULL;
  if (http_status != bus->http_status)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Unexpected response code %u to command %s in %s:%u\n",
                http_status,
                bus->is->commands[bus->is->ip].label,
                __FILE__,
                __LINE__);
    TALER_TESTING_interpreter_fail (bus->is);
    return;
  }
  if (NULL != ud)
  {
    switch (ud->us)
    {
    case SYNC_US_SUCCESS:
      if (0 != GNUNET_memcmp (&bus->curr_hash,
                              ud->details.curr_backup_hash))
      {
        GNUNET_break (0);
        TALER_TESTING_interpreter_fail (bus->is);
        return;
      }
      break;
    case SYNC_US_PAYMENT_REQUIRED:
      {
        struct TALER_MERCHANT_PayUriData pd;

        if (GNUNET_OK !=
            TALER_MERCHANT_parse_pay_uri (ud->details.payment_request,
                                          &pd))
        {
          GNUNET_break (0);
          TALER_TESTING_interpreter_fail (bus->is);
          return;
        }
        bus->payment_order_id = GNUNET_strdup (pd.order_id);
        if (NULL != pd.claim_token)
          bus->token = *pd.claim_token;
        TALER_MERCHANT_parse_pay_uri_free (&pd);
        GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                    "Order ID from Sync service is `%s'\n",
                    bus->payment_order_id);
        memset (&bus->curr_hash,
                0,
                sizeof (struct GNUNET_HashCode));
      }
      break;
    case SYNC_US_CONFLICTING_BACKUP:
      {
        const struct TALER_TESTING_Command *ref;
        const struct GNUNET_HashCode *h;

        ref = TALER_TESTING_interpreter_lookup_command
                (bus->is,
                bus->last_upload);
        GNUNET_assert (NULL != ref);
        GNUNET_assert (GNUNET_OK ==
                       SYNC_TESTING_get_trait_hash (ref,
                                                    SYNC_TESTING_TRAIT_HASH_CURRENT,
                                                    &h));
        if (0 != GNUNET_memcmp (h,
                                &ud->details.recovered_backup.
                                existing_backup_hash))
        {
          GNUNET_break (0);
          TALER_TESTING_interpreter_fail (bus->is);
          return;
        }
      }
    case SYNC_US_HTTP_ERROR:
      break;
    case SYNC_US_CLIENT_ERROR:
      GNUNET_break (0);
      TALER_TESTING_interpreter_fail (bus->is);
      return;
    case SYNC_US_SERVER_ERROR:
      GNUNET_break (0);
      TALER_TESTING_interpreter_fail (bus->is);
      return;
    }
  }
  TALER_TESTING_interpreter_next (bus->is);
}


/**
 * Run a "backup upload" CMD.
 *
 * @param cls closure.
 * @param cmd command currently being run.
 * @param is interpreter state.
 */
static void
backup_upload_run (void *cls,
                   const struct TALER_TESTING_Command *cmd,
                   struct TALER_TESTING_Interpreter *is)
{
  struct BackupUploadState *bus = cls;

  bus->is = is;
  if (NULL != bus->prev_upload)
  {
    const struct TALER_TESTING_Command *ref;

    ref = TALER_TESTING_interpreter_lookup_command (
      is,
      bus->prev_upload);
    if (NULL == ref)
    {
      GNUNET_break (0);
      TALER_TESTING_interpreter_fail (bus->is);
      return;
    }
    {
      const struct GNUNET_HashCode *h;

      if (GNUNET_OK ==
          SYNC_TESTING_get_trait_hash (ref,
                                       SYNC_TESTING_TRAIT_HASH_CURRENT,
                                       &h))
      {
        bus->prev_hash = *h;
      }
    }
    {
      const struct SYNC_AccountPrivateKeyP *priv;

      if (GNUNET_OK !=
          SYNC_TESTING_get_trait_account_priv (ref,
                                               0,
                                               &priv))
      {
        GNUNET_break (0);
        TALER_TESTING_interpreter_fail (bus->is);
        return;
      }
      bus->sync_priv = *priv;
    }
    {
      const struct SYNC_AccountPublicKeyP *pub;

      if (GNUNET_OK !=
          SYNC_TESTING_get_trait_account_pub (ref,
                                              0,
                                              &pub))
      {
        GNUNET_break (0);
        TALER_TESTING_interpreter_fail (bus->is);
        return;
      }
      bus->sync_pub = *pub;
    }
    if (0 != (SYNC_TESTING_UO_REFERENCE_ORDER_ID & bus->uopt))
    {
      const char *order_id;

      if (GNUNET_OK !=
          TALER_TESTING_get_trait_order_id (ref,
                                            0,
                                            &order_id))
      {
        GNUNET_break (0);
        TALER_TESTING_interpreter_fail (bus->is);
        return;
      }
      bus->payment_order_req = order_id;
      if (NULL == bus->payment_order_req)
      {
        GNUNET_break (0);
        TALER_TESTING_interpreter_fail (bus->is);
        return;
      }
    }
  }
  else
  {
    GNUNET_CRYPTO_eddsa_key_create (&bus->sync_priv.eddsa_priv);
    GNUNET_CRYPTO_eddsa_key_get_public (&bus->sync_priv.eddsa_priv,
                                        &bus->sync_pub.eddsa_pub);
  }
  if (0 != (SYNC_TESTING_UO_PREV_HASH_WRONG & bus->uopt))
    GNUNET_CRYPTO_random_block (GNUNET_CRYPTO_QUALITY_WEAK,
                                &bus->prev_hash,
                                sizeof (struct GNUNET_HashCode));
  GNUNET_CRYPTO_hash (bus->backup,
                      bus->backup_size,
                      &bus->curr_hash);
  bus->uo = SYNC_upload (is->ctx,
                         bus->sync_url,
                         &bus->sync_priv,
                         ( ( (NULL != bus->prev_upload) &&
                             (GNUNET_NO == GNUNET_is_zero (
                                &bus->prev_hash)) ) ||
                           (0 != (SYNC_TESTING_UO_PREV_HASH_WRONG
                                  & bus->uopt)) )
                         ? &bus->prev_hash
                         : NULL,
                         bus->backup_size,
                         bus->backup,
                         (0 != (SYNC_TESTING_UO_REQUEST_PAYMENT & bus->uopt)),
                         bus->payment_order_req,
                         &backup_upload_cb,
                         bus);
  if (NULL == bus->uo)
  {
    GNUNET_break (0);
    TALER_TESTING_interpreter_fail (bus->is);
    return;
  }
}


/**
 * Free the state of a "backup upload" CMD, and possibly
 * cancel it if it did not complete.
 *
 * @param cls closure.
 * @param cmd command being freed.
 */
static void
backup_upload_cleanup (void *cls,
                       const struct TALER_TESTING_Command *cmd)
{
  struct BackupUploadState *bus = cls;

  if (NULL != bus->uo)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                "Command '%s' did not complete (backup upload)\n",
                cmd->label);
    SYNC_upload_cancel (bus->uo);
    bus->uo = NULL;
  }
  GNUNET_free (bus->payment_order_id);
  GNUNET_free (bus);
}


/**
 * Offer internal data to other commands.
 *
 * @param cls closure
 * @param ret[out] result (could be anything)
 * @param trait name of the trait
 * @param index index number of the object to extract.
 * @return #GNUNET_OK on success
 */
static int
backup_upload_traits (void *cls,
                      const void **ret,
                      const char *trait,
                      unsigned int index)
{
  struct BackupUploadState *bus = cls;
  struct TALER_TESTING_Trait straits[] = {
    SYNC_TESTING_make_trait_hash (SYNC_TESTING_TRAIT_HASH_CURRENT,
                                  &bus->curr_hash),
    SYNC_TESTING_make_trait_hash (SYNC_TESTING_TRAIT_HASH_PREVIOUS,
                                  &bus->prev_hash),
    TALER_TESTING_make_trait_claim_token (0,
                                          &bus->token),
    SYNC_TESTING_make_trait_account_pub (0,
                                         &bus->sync_pub),
    SYNC_TESTING_make_trait_account_priv (0,
                                          &bus->sync_priv),
    TALER_TESTING_make_trait_order_id (0,
                                       bus->payment_order_id),
    TALER_TESTING_trait_end ()
  };
  struct TALER_TESTING_Trait ftraits[] = {
    TALER_TESTING_make_trait_claim_token (0,
                                          &bus->token),
    SYNC_TESTING_make_trait_account_pub (0,
                                         &bus->sync_pub),
    SYNC_TESTING_make_trait_account_priv (0,
                                          &bus->sync_priv),
    TALER_TESTING_make_trait_order_id (0,
                                       bus->payment_order_id),
    TALER_TESTING_trait_end ()
  };


  return TALER_TESTING_get_trait ((NULL != bus->payment_order_req)
                                  ? ftraits
                                  : straits,
                                  ret,
                                  trait,
                                  index);
}


/**
 * Make the "backup upload" command.
 *
 * @param label command label
 * @param sync_url base URL of the sync serving
 *        the policy store request.
 * @param prev_upload reference to a previous upload we are
 *        supposed to update, NULL for none
 * @param last_upload reference to the last upload for the
 *          same account, used to check result on MHD_HTTP_CONFLICT
 * @param uo upload options
 * @param http_status expected HTTP status.
 * @param backup_data data to upload
 * @param backup_data_size number of bytes in @a backup_data
 * @return the command
 */
struct TALER_TESTING_Command
SYNC_TESTING_cmd_backup_upload (const char *label,
                                const char *sync_url,
                                const char *prev_upload,
                                const char *last_upload,
                                enum SYNC_TESTING_UploadOption uo,
                                unsigned int http_status,
                                const void *backup_data,
                                size_t backup_data_size)
{
  struct BackupUploadState *bus;

  bus = GNUNET_new (struct BackupUploadState);
  bus->http_status = http_status;
  bus->prev_upload = prev_upload;
  bus->last_upload = last_upload;
  bus->uopt = uo;
  bus->sync_url = sync_url;
  bus->backup = backup_data;
  bus->backup_size = backup_data_size;
  {
    struct TALER_TESTING_Command cmd = {
      .cls = bus,
      .label = label,
      .run = &backup_upload_run,
      .cleanup = &backup_upload_cleanup,
      .traits = &backup_upload_traits
    };

    return cmd;
  }
}
