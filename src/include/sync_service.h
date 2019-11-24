/*
  This file is part of TALER
  Copyright (C) 2019 Taler Systems SA

  Anastasis is free software; you can redistribute it and/or modify it under the
  terms of the GNU Affero General Public License as published by the Free Software
  Foundation; either version 3, or (at your option) any later version.

  Anastasis is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License along with
  Anastasis; see the file COPYING.LIB.  If not, see <http://www.gnu.org/licenses/>
*/
/**
 * @file include/sync_service.h
 * @brief C interface of libsync, a C library to use sync's HTTP API
 * @author Christian Grothoff
 */
#ifndef SYNC_SERVICE_H
#define SYNC_SERVICE_H

#include <gnunet/gnunet_util_lib.h>
#include <taler/taler_error_codes.h>
#include <gnunet/gnunet_curl_lib.h>
#include <jansson.h>


GNUNET_NETWORK_STRUCT_BEGIN


/**
 * Private key identifying an account.
 */
struct SYNC_AccountPrivateKeyP
{
  /**
   * We use EdDSA.
   */
  struct GNUNET_CRYPTO_EddsaPrivateKey eddsa_priv;
};


/**
 * Public key identifying an account.
 */
struct SYNC_AccountPublicKeyP
{
  /**
   * We use EdDSA.
   */
  struct GNUNET_CRYPTO_EddsaPublicKey eddsa_pub;
};


/**
 * Data signed by the account public key of a sync client to
 * authorize the upload of the backup.
 */
struct SYNC_UploadSignaturePS
{
  /**
   * Set to #TALER_SIGNATURE_SYNC_BACKUP_UPLOAD.
   */
  struct GNUNET_CRYPTO_EccSignaturePurpose purpose;

  /**
   * Hash of the previous backup, all zeros for none.
   */
  struct GNUNET_HashCode old_backup_hash GNUNET_PACKED;

  /**
   * Hash of the new backup.
   */
  struct GNUNET_HashCode new_backup_hash GNUNET_PACKED;

};


/**
 * Signature made with an account's public key.
 */
struct SYNC_AccountSignatureP
{
  /**
   * We use EdDSA.
   */
  struct GNUNET_CRYPTO_EddsaSignature eddsa_sig;
};

GNUNET_NETWORK_STRUCT_END


/**
 * High-level ways how an upload may conclude.
 */
enum SYNC_UploadStatus
{
  /**
   * Backup was successfully made.
   */
  SYNC_US_SUCCESS = 0,

  /**
   * Account expired or payment was explicitly requested
   * by the client.
   */
  SYNC_US_PAYMENT_REQUIRED = 1,

  /**
   * Conflicting backup existed on server. Client should
   * reconcile and try again with (using the provided
   * recovered backup as the previous backup).
   */
  SYNC_US_CONFLICTING_BACKUP = 2,

  /**
   * HTTP interaction failed, see HTTP status.
   */
  SYNC_US_HTTP_ERROR = 3,

  /**
   * We had an internal error (not sure this can happen,
   * but reserved for HTTP 400 status codes).
   */
  SYNC_US_CLIENT_ERROR = 4,

  /**
   * Server had an internal error.
   */
  SYNC_US_SERVER_ERROR = 5
};


/**
 * Result of an upload.
 */
struct SYNC_UploadDetails
{
  /**
   * High level status of the upload operation.
   */
  enum SYNC_UploadStatus us;

  union
  {

    /**
     * Hash of the synchronized backup, returned if
     * @e us is #SYNC_US_SUCCESS.
     */
    const struct GNUNET_HashCode *curr_backup_hash;

    /**
     * Previous backup. Returned if @e us is
     * #SYNC_US_CONFLICTING_BACKUP
     */
    struct
    {
      /**
       * Hash over @e existing_backup.
       */
      const struct GNUNET_HashCode *existing_backup_hash;

      /**
       * Number of bytes in @e existing_backup.
       */
      size_t existing_backup_size;

      /**
       * The backup on the server, which does not match the
       * "previous" backup expected by the client and thus
       * needs to be decrypted, reconciled and re-uploaded.
       */
      const void *existing_backup;

    } recovered_backup;

    /**
     * A taler://pay/-URI with a request to pay the annual fee for
     * the service.  Returned if @e us is #SYNC_US_PAYMENT_REQUIRED.
     */
    const char *payment_request;

  } details;

};


/**
 * Function called with the results of a #SYNC_upload().
 *
 * @param cls closure
 * @param ec Taler error code
 * @param http_status HTTP status of the request
 * @param ud details about the upload operation
 */
typedef void
(*SYNC_UploadCallback)(void *cls,
                       enum TALER_ErrorCode ec,
                       unsigned int http_status,
                       const struct SYNC_UploadDetails *ud);


/**
 * Handle for an upload operatoin.
 */
struct SYNC_UploadOperation;


/**
 * Upload a @a backup to a Sync server. Note that @a backup must
 * have already been compressed, padded and encrypted by the
 * client.
 *
 * While @a pub is theoretically protected by the HTTPS protocol and
 * required to access the backup, it should be assumed that an
 * adversary might be able to download the backups from the Sync
 * server -- or even run the Sync server. Thus, strong encryption
 * is essential and NOT implemented by this function.
 *
 * The use of Anastasis to safely store the Sync encryption keys and
 * @a pub is recommended.  Storing @a priv in Anastasis depends on
 * your priorities: without @a priv, further updates to the backup are
 * not possible, and the user would have to pay for another
 * account. OTOH, without @a priv an adversary that compromised
 * Anastasis can only read the backups, but not alter or destroy them.
 *
 * @param ctx for HTTP client request processing
 * @param base_url base URL of the Sync server
 * @param priv private key of an account with the server
 * @param prev_backup_hash hash of the previous backup, NULL for the first upload ever
 * @param backup_size number of bytes in @a backup
 * @param payment_requested #GNUNET_YES if the client wants to pay more for the account now
 * @param paid_order_id order ID of a recent payment made, or NULL for none
 * @param cb function to call with the result
 * @param cb_cls closure for @a cb
 * @return handle for the operation
 */
struct SYNC_UploadOperation *
SYNC_upload (struct GNUNET_CURL_Context *ctx,
             const char *base_url,
             struct SYNC_AccountPrivateKeyP *priv,
             const struct GNUNET_HashCode *prev_backup_hash,
             size_t backup_size,
             const void *backup,
             int payment_requested,
             const char *paid_order_id,
             SYNC_UploadCallback cb,
             void *cb_cls);


/**
 * Cancel the upload.  Note that aborting an upload does NOT guarantee
 * that it did not complete, it is possible that the server did
 * receive the full request before the upload is aborted.
 *
 * @param uo operation to cancel.
 */
void
SYNC_upload_cancel (struct SYNC_UploadOperation *uo);


/**
 * Function called with the results of a #SYNC_download().
 *
 * @param cls closure
 * @param sig signature of the account owner, affirming the
 *            integrity of the backup (already verified)
 * @param prev_backup_hash hash of the previous backup (used
 *            to verify the signature, could be used by clients
 *            to verify backup chains)
 * @param curr_backup_hash hash over @a backup (verified)
 * @param backup_size number of bytes in @a backup
 * @param backup the lastest backup as downloaded from the
 *        server and affirmed by @a sig
 */
typedef void
(*SYNC_DownloadCallback)(void *cls,
                         const struct SYNC_AccountPublicKeyP *sig,
                         const struct GNUNET_HashCode *prev_backup_hash,
                         const struct GNUNET_HashCode *curr_backup_hash,
                         size_t backup_size,
                         const void *backup);


/**
 * Handle for a download operation.
 */
struct SYNC_DownloadOperation;


/**
 * Download the latest version of a backup for account @a pub.
 *
 * @param ctx for HTTP client request processing
 * @param base_url base URL of the Sync server
 * @param pub account public key
 * @param cb function to call with the backup
 * @param cb_cls closure for @a cb
 * @return handle for the operation
 */
struct SYNC_DownloadOperation *
SYNC_download (struct GNUNET_CURL_Context *ctx,
               const char *base_url,
               const struct SYNC_AccountPublicKeyP *pub,
               SYNC_DownloadCallback cb,
               void *cb_cls);


/**
 * Cancel the download.
 *
 * @param do operation to cancel.
 */
void
SYNC_download_cancel (struct SYNC_DownloadOperation *download);


/* TODO: API for /terms! */


#endif  /* SYNC_SERVICE_H */
