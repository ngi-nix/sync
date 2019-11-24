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
#include "sync_service.h"


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

};


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
  GNUNET_break (0);
  return NULL;
}


/**
 * Cancel the download.
 *
 * @param do operation to cancel.
 */
void
SYNC_download_cancel (struct SYNC_DownloadOperation *download)
{
}


/* end of sync_api_download.c */
