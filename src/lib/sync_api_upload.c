/*
  This file is part of TALER
  Copyright (C) 2014-2019 Taler Systems SA

  TALER is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1,
  or (at your option) any later version.

  TALER is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with TALER; see the file COPYING.LGPL.  If not,
  see <http://www.gnu.org/licenses/>
*/

/**
 * @file lib/sync_api_upload.c
 * @brief Implementation of the upload POST
 * @author Christian Grothoff
 */
#include "platform.h"
#include <curl/curl.h>
#include <jansson.h>
#include <microhttpd.h> /* just for HTTP status codes */
#include <gnunet/gnunet_util_lib.h>
#include <gnunet/gnunet_curl_lib.h>
#include <taler/taler_signatures.h>
#include "sync_service.h"
#include "sync_api_curl_defaults.h"


/**
 * @brief Handle for an upload operation.
 */
struct SYNC_UploadOperation
{

  /**
   * The url for this request.
   */
  char *url;

  /**
   * Handle for the request.
   */
  struct GNUNET_CURL_Job *job;

  /**
   * Reference to the execution context.
   */
  struct GNUNET_CURL_Context *ctx;

  /**
   * Function to call with the result.
   */
  SYNC_UploadCallback cb;

  /**
   * Closure for @e cb.
   */
  void *cb_cls;

};


/**
 * Function called when we're done processing the
 * HTTP /backup request.
 *
 * @param cls the `struct SYNC_UploadOperation`
 * @param response_code HTTP response code, 0 on error
 * @param response
 */
static void
handle_upload_finished (void *cls,
                        long response_code,
                        const void *data,
                        size_t data_size)
{
  struct SYNC_UploadOperation *uo = cls;
  enum TALER_ErrorCode ec = TALER_EC_INVALID;

  uo->job = NULL;
  switch (response_code)
  {
  case 0:
    break;
  case MHD_HTTP_OK:
    break;
    // FIXME: handle all cases...
  }

  if (NULL != uo->cb)
  {
    uo->cb (uo->cb_cls,
            ec,
            response_code,
            NULL);
    uo->cb = NULL;
  }
  SYNC_upload_cancel (uo);
}



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
             void *cb_cls)
{
  struct SYNC_UploadSignaturePS usp;
  struct SYNC_AccountSignatureP account_sig;
  struct SYNC_UploadOperation *uo;
  CURL *eh;

  usp.purpose.purpose = htonl (TALER_SIGNATURE_SYNC_BACKUP_UPLOAD);
  usp.purpose.size = htonl (sizeof (usp));
  usp.old_backup_hash = *prev_backup_hash;
  GNUNET_CRYPTO_hash (backup,
                      backup_size,
                      &usp.new_backup_hash);
  if (GNUNET_OK !=
      GNUNET_CRYPTO_eddsa_sign (&priv->eddsa_priv,
                                &usp.purpose,
                                &account_sig.eddsa_sig))
  {
    GNUNET_break (0);
    return NULL;
  }
  uo = GNUNET_new (struct SYNC_UploadOperation);
  // FIXME: build uo->url
  uo->ctx = ctx;
  uo->cb = cb;
  uo->cb_cls = cb_cls;
  eh = SYNC_curl_easy_get_ (uo->url);
  // FIXME: set headers!
  GNUNET_assert (CURLE_OK ==
                 curl_easy_setopt (eh,
                                   CURLOPT_POSTFIELDS,
                                   backup));
  GNUNET_assert (CURLE_OK ==
                 curl_easy_setopt (eh,
                                   CURLOPT_POSTFIELDSIZE,
                                   (long) backup_size));
  uo->job = GNUNET_CURL_job_add_raw (ctx,
                                     eh,
                                     GNUNET_NO,
                                     &handle_upload_finished,
                                     uo);
  return uo;
}


/**
 * Cancel the upload.  Note that aborting an upload does NOT guarantee
 * that it did not complete, it is possible that the server did
 * receive the full request before the upload is aborted.
 *
 * @param uo operation to cancel.
 */
void
SYNC_upload_cancel (struct SYNC_UploadOperation *uo)
{
  if (NULL != uo->job)
  {
    GNUNET_CURL_job_cancel (uo->job);
    uo->job = NULL;
  }
  GNUNET_free (uo->url);
  GNUNET_free (uo);
}


/* end of sync_api_upload.c */
