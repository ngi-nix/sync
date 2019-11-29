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
 * @file lib/sync_api_download.c
 * @brief Implementation of the download
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
 * @brief Handle for a download operation.
 */
struct SYNC_DownloadOperation
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
  SYNC_DownloadCallback cb;

  /**
   * Closure for @e cb.
   */
  void *cb_cls;

  /**
   * Public key of the account we are downloading from.
   */
  struct SYNC_AccountPublicKeyP account_pub;

  /**
   * Signature returned in the "Sync-Signature"
   * header, or all zeros for none.
   */
  struct SYNC_AccountSignatureP account_sig;

  /**
   * Hash code returned by the server in the
   * "Sync-Previous" header, or all zeros for
   * none.
   */
  struct GNUNET_HashCode sync_previous;

};


/**
 * Function called when we're done processing the
 * HTTP /backup request.
 *
 * @param cls the `struct SYNC_DownloadOperation`
 * @param response_code HTTP response code, 0 on error
 * @param response
 */
static void
handle_download_finished (void *cls,
                          long response_code,
                          const void *data,
                          size_t data_size)
{
  struct SYNC_DownloadOperation *download = cls;

  download->job = NULL;
  switch (response_code)
  {
  case 0:
    break;
  case MHD_HTTP_OK:
    {
      struct SYNC_DownloadDetails dd;
      struct SYNC_UploadSignaturePS usp;

      usp.purpose.purpose = htonl (TALER_SIGNATURE_SYNC_BACKUP_UPLOAD);
      usp.purpose.size = htonl (sizeof (usp));
      usp.old_backup_hash = download->sync_previous;
      GNUNET_CRYPTO_hash (data,
                          data_size,
                          &usp.new_backup_hash);
      if (GNUNET_OK !=
          GNUNET_CRYPTO_eddsa_verify (TALER_SIGNATURE_SYNC_BACKUP_UPLOAD,
                                      &usp.purpose,
                                      &download->account_sig.eddsa_sig,
                                      &download->account_pub.eddsa_pub))
      {
        GNUNET_break_op (0);
        response_code = 0;
        break;
      }
      /* Success, call callback with all details! */
      memset (&dd, 0, sizeof (dd));
      dd.sig = download->account_sig;
      dd.prev_backup_hash = download->sync_previous;
      dd.curr_backup_hash = usp.new_backup_hash;
      dd.backup = data;
      dd.backup_size = data_size;
      download->cb (download->cb_cls,
                    response_code,
                    &dd);
      download->cb = NULL;
      SYNC_download_cancel (download);
      return;
    }
  case MHD_HTTP_BAD_REQUEST:
    /* This should never happen, either us or the sync server is buggy
       (or API version conflict); just pass JSON reply to the application */
    break;
  case MHD_HTTP_NOT_FOUND:
    /* Nothing really to verify */
    break;
  case MHD_HTTP_INTERNAL_SERVER_ERROR:
    /* Server had an internal issue; we should retry, but this API
       leaves this to the application */
    break;
  default:
    /* unexpected response code */
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Unexpected response code %u\n",
                (unsigned int) response_code);
    GNUNET_break (0);
    response_code = 0;
    break;
  }
  if (NULL != download->cb)
  {
    download->cb (download->cb_cls,
                  response_code,
                  NULL);
    download->cb = NULL;
  }
  SYNC_download_cancel (download);
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
  struct SYNC_DownloadOperation *download = userdata;
  size_t total = size * nitems;
  char *ndup;
  const char *hdr_type;
  char *hdr_val;

  ndup = GNUNET_strndup (buffer,
                         total);
  hdr_type = strtok (ndup,
                     ":");
  if (NULL == hdr_type)
  {
    GNUNET_free (ndup);
    return total;
  }
  hdr_val = strtok (NULL,
                    "\n\r");
  if (NULL == hdr_val)
  {
    GNUNET_free (ndup);
    return total;
  }
  if (' ' == *hdr_val)
    hdr_val++;
  if (0 == strcasecmp (hdr_type,
                       "Sync-Signature"))
  {
    if (GNUNET_OK !=
        GNUNET_STRINGS_string_to_data (hdr_val,
                                       strlen (hdr_val),
                                       &download->account_sig,
                                       sizeof (struct SYNC_AccountSignatureP)))
    {
      GNUNET_break_op (0);
      GNUNET_free (ndup);
      return 0;
    }
  }
  if (0 == strcasecmp (hdr_type,
                       "Sync-Previous"))
  {
    if (GNUNET_OK !=
        GNUNET_STRINGS_string_to_data (hdr_val,
                                       strlen (hdr_val),
                                       &download->sync_previous,
                                       sizeof (struct GNUNET_HashCode)))
    {
      GNUNET_break_op (0);
      GNUNET_free (ndup);
      return 0;
    }
  }
  GNUNET_free (ndup);
  return total;
}


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
               void *cb_cls)
{
  struct SYNC_DownloadOperation *download;
  char *pub_str;
  CURL *eh;

  download = GNUNET_new (struct SYNC_DownloadOperation);
  download->account_pub = *pub;
  pub_str = GNUNET_STRINGS_data_to_string_alloc (pub,
                                                 sizeof (*pub));
  GNUNET_asprintf (&download->url,
                   "%s%sbackups/%s",
                   base_url,
                   '/' == base_url[strlen (base_url) - 1]
                   ? ""
                   : "/",
                   pub_str);
  GNUNET_free (pub_str);
  eh = SYNC_curl_easy_get_ (download->url);
  GNUNET_assert (CURLE_OK ==
                 curl_easy_setopt (eh,
                                   CURLOPT_HEADERFUNCTION,
                                   &handle_header));
  GNUNET_assert (CURLE_OK ==
                 curl_easy_setopt (eh,
                                   CURLOPT_HEADERDATA,
                                   download));
  download->cb = cb;
  download->cb_cls = cb_cls;
  download->job = GNUNET_CURL_job_add_raw (ctx,
                                           eh,
                                           GNUNET_NO,
                                           &handle_download_finished,
                                           download);
  return download;
}


/**
 * Cancel the download.
 *
 * @param do operation to cancel.
 */
void
SYNC_download_cancel (struct SYNC_DownloadOperation *download)
{
  if (NULL != download->job)
  {
    GNUNET_CURL_job_cancel (download->job);
    download->job = NULL;
  }
  GNUNET_free (download->url);
  GNUNET_free (download);
}


/* end of sync_api_download.c */
