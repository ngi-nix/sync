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
#include <taler/taler_json_lib.h>
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

  /**
   * Payment URI we received from the service, or NULL.
   */
  char *pay_uri;

  /**
   * Hash of the data we are uploading.
   */
  struct GNUNET_HashCode new_upload_hash;
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
  struct SYNC_UploadDetails ud;
  struct SYNC_UploadDetails *udp;

  uo->job = NULL;
  udp = NULL;
  memset (&ud, 0, sizeof (ud));
  switch (response_code)
  {
  case 0:
    break;
  case MHD_HTTP_NO_CONTENT:
    ud.us = SYNC_US_SUCCESS;
    ud.details.curr_backup_hash = &uo->new_upload_hash;
    udp = &ud;
    ec = TALER_EC_NONE;
    break;
  case MHD_HTTP_NOT_MODIFIED:
    ud.us = SYNC_US_SUCCESS;
    ud.details.curr_backup_hash = &uo->new_upload_hash;
    udp = &ud;
    ec = TALER_EC_NONE;
    break;
  case MHD_HTTP_BAD_REQUEST:
    GNUNET_break (0);
    ec = TALER_JSON_get_error_code2 (data,
                                     data_size);
    break;
  case MHD_HTTP_PAYMENT_REQUIRED:
    ud.us = SYNC_US_PAYMENT_REQUIRED;
    ud.details.payment_request = uo->pay_uri;
    udp = &ud;
    ec = TALER_EC_NONE;
    break;
  case MHD_HTTP_FORBIDDEN:
    GNUNET_break (0);
    ec = TALER_JSON_get_error_code2 (data,
                                     data_size);
    break;
  case MHD_HTTP_CONFLICT:
    ud.us = SYNC_US_CONFLICTING_BACKUP;
    GNUNET_CRYPTO_hash (data,
                        data_size,
                        &ud.details.recovered_backup.existing_backup_hash);
    ud.details.recovered_backup.existing_backup_size
      = data_size;
    ud.details.recovered_backup.existing_backup
      = data;
    udp = &ud;
    ec = TALER_EC_NONE;
    break;
  case MHD_HTTP_GONE:
    ec = TALER_JSON_get_error_code2 (data,
                                     data_size);
    break;
  case MHD_HTTP_LENGTH_REQUIRED:
    GNUNET_break (0);
    break;
  case MHD_HTTP_REQUEST_ENTITY_TOO_LARGE:
    ec = TALER_JSON_get_error_code2 (data,
                                     data_size);
    break;
  case MHD_HTTP_TOO_MANY_REQUESTS:
    ec = TALER_JSON_get_error_code2 (data,
                                     data_size);
    break;
  case MHD_HTTP_INTERNAL_SERVER_ERROR:
    GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                "Internal server error: `%.*s\n",
                (int) data_size,
                (const char *) data);
    break;
  }
  if (NULL != uo->cb)
  {
    uo->cb (uo->cb_cls,
            ec,
            response_code,
            udp);
    uo->cb = NULL;
  }
  SYNC_upload_cancel (uo);
}


/**
 * Handle HTTP header received by curl.
 *
 * @param buffer one line of HTTP header data
 * @param size size of an item
 * @param nitems number of items passed
 * @param userdata our `struct SYNC_DownloadOperation *`
 * @return `size * nitems`
 */
static size_t
handle_header (char *buffer,
               size_t size,
               size_t nitems,
               void *userdata)
{
  struct SYNC_UploadOperation *uo = userdata;
  size_t total = size * nitems;
  char *ndup;
  const char *hdr_type;
  char *hdr_val;
  char *sp;

  ndup = GNUNET_strndup (buffer,
                         total);
  hdr_type = strtok_r (ndup,
                       ":",
                       &sp);
  if (NULL == hdr_type)
  {
    GNUNET_free (ndup);
    return total;
  }
  hdr_val = strtok_r (NULL,
                      "",
                      &sp);
  if (NULL == hdr_val)
  {
    GNUNET_free (ndup);
    return total;
  }
  if (' ' == *hdr_val)
    hdr_val++;
  if (0 == strcasecmp (hdr_type,
                       "Taler"))
  {
    size_t len;

    /* found payment URI we care about! */
    uo->pay_uri = GNUNET_strdup (hdr_val);
    len = strlen (uo->pay_uri);
    while ( (len > 0) &&
            ( ('\n' == uo->pay_uri[len - 1]) ||
              ('\r' == uo->pay_uri[len - 1]) ) )
    {
      len--;
      uo->pay_uri[len] = '\0';
    }
  }
  GNUNET_free (ndup);
  return total;
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
  struct SYNC_AccountSignatureP account_sig;
  struct SYNC_UploadOperation *uo;
  CURL *eh;
  struct curl_slist *job_headers;
  struct SYNC_UploadSignaturePS usp = {
    .purpose.purpose = htonl (TALER_SIGNATURE_SYNC_BACKUP_UPLOAD),
    .purpose.size = htonl (sizeof (usp))
  };

  if (NULL != prev_backup_hash)
    usp.old_backup_hash = *prev_backup_hash;
  GNUNET_CRYPTO_hash (backup,
                      backup_size,
                      &usp.new_backup_hash);
  GNUNET_CRYPTO_eddsa_sign (&priv->eddsa_priv,
                            &usp,
                            &account_sig.eddsa_sig);

  /* setup our HTTP headers */
  job_headers = NULL;
  {
    struct curl_slist *ext;
    char *val;
    char *hdr;

    /* Set Sync-Signature header */
    val = GNUNET_STRINGS_data_to_string_alloc (&account_sig,
                                               sizeof (account_sig));
    GNUNET_asprintf (&hdr,
                     "Sync-Signature: %s",
                     val);
    GNUNET_free (val);
    ext = curl_slist_append (job_headers,
                             hdr);
    GNUNET_free (hdr);
    if (NULL == ext)
    {
      GNUNET_break (0);
      curl_slist_free_all (job_headers);
      return NULL;
    }
    job_headers = ext;

    /* set If-None-Match header */
    val = GNUNET_STRINGS_data_to_string_alloc (&usp.new_backup_hash,
                                               sizeof (struct GNUNET_HashCode));
    GNUNET_asprintf (&hdr,
                     "%s: %s",
                     MHD_HTTP_HEADER_IF_NONE_MATCH,
                     val);
    GNUNET_free (val);
    ext = curl_slist_append (job_headers,
                             hdr);
    GNUNET_free (hdr);
    if (NULL == ext)
    {
      GNUNET_break (0);
      curl_slist_free_all (job_headers);
      return NULL;
    }
    job_headers = ext;

    /* Setup If-Match header */
    if (NULL != prev_backup_hash)
    {
      val = GNUNET_STRINGS_data_to_string_alloc (&usp.old_backup_hash,
                                                 sizeof (struct
                                                         GNUNET_HashCode));
      GNUNET_asprintf (&hdr,
                       "If-Match: %s",
                       val);
      GNUNET_free (val);
      ext = curl_slist_append (job_headers,
                               hdr);
      GNUNET_free (hdr);
      if (NULL == ext)
      {
        GNUNET_break (0);
        curl_slist_free_all (job_headers);
        return NULL;
      }
      job_headers = ext;
    }
  }
  /* Finished setting up headers */

  uo = GNUNET_new (struct SYNC_UploadOperation);
  uo->new_upload_hash = usp.new_backup_hash;
  {
    char *path;
    char *account_s;
    struct SYNC_AccountPublicKeyP pub;

    GNUNET_CRYPTO_eddsa_key_get_public (&priv->eddsa_priv,
                                        &pub.eddsa_pub);
    account_s = GNUNET_STRINGS_data_to_string_alloc (&pub,
                                                     sizeof (pub));
    GNUNET_asprintf (&path,
                     "backups/%s",
                     account_s);
    GNUNET_free (account_s);
    uo->url = (GNUNET_YES == payment_requested)
              ? TALER_url_join (base_url,
                                path,
                                "pay",
                                "y",
                                (NULL != paid_order_id)
                                ? "paying"
                                : NULL,
                                paid_order_id,
                                NULL)
              : TALER_url_join (base_url,
                                path,
                                (NULL != paid_order_id)
                                ? "paying"
                                : NULL,
                                paid_order_id,
                                NULL);
    GNUNET_free (path);
  }
  uo->ctx = ctx;
  uo->cb = cb;
  uo->cb_cls = cb_cls;
  eh = SYNC_curl_easy_get_ (uo->url);
  GNUNET_assert (CURLE_OK ==
                 curl_easy_setopt (eh,
                                   CURLOPT_POSTFIELDS,
                                   backup));
  GNUNET_assert (CURLE_OK ==
                 curl_easy_setopt (eh,
                                   CURLOPT_POSTFIELDSIZE,
                                   (long) backup_size));
  GNUNET_assert (CURLE_OK ==
                 curl_easy_setopt (eh,
                                   CURLOPT_HEADERFUNCTION,
                                   &handle_header));
  GNUNET_assert (CURLE_OK ==
                 curl_easy_setopt (eh,
                                   CURLOPT_HEADERDATA,
                                   uo));
  uo->job = GNUNET_CURL_job_add_raw (ctx,
                                     eh,
                                     job_headers,
                                     &handle_upload_finished,
                                     uo);
  curl_slist_free_all (job_headers);
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
  GNUNET_free (uo->pay_uri);
  GNUNET_free (uo->url);
  GNUNET_free (uo);
}


/* end of sync_api_upload.c */
