/*
  This file is part of TALER
  Copyright (C) 2019 Taler Systems SA

  TALER is free software; you can redistribute it and/or modify it under the
  terms of the GNU General Public License as published by the Free Software
  Foundation; either version 3, or (at your option) any later version.

  TALER is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along with
  TALER; see the file COPYING.  If not, see <http://www.gnu.org/licenses/>
*/
/**
 * @file sync/sync-httpd.h
 * @brief HTTP serving layer
 * @author Christian Grothoff
 */
#ifndef sync_HTTPD_H
#define sync_HTTPD_H

#include "platform.h"
#include <microhttpd.h>
#include <taler/taler_mhd_lib.h>
#include "sync_database_lib.h"

/**
 * @brief Struct describing an URL and the handler for it.
 */
struct SH_RequestHandler
{

  /**
   * URL the handler is for.
   */
  const char *url;

  /**
   * Method the handler is for, NULL for "all".
   */
  const char *method;

  /**
   * Mime type to use in reply (hint, can be NULL).
   */
  const char *mime_type;

  /**
   * Raw data for the @e handler
   */
  const void *data;

  /**
   * Number of bytes in @e data, 0 for 0-terminated.
   */
  size_t data_size;

  /**
   * Function to call to handle the request.
   *
   * @param rh this struct
   * @param mime_type the @e mime_type for the reply (hint, can be NULL)
   * @param connection the MHD connection to handle
   * @param[in,out] connection_cls the connection's closure (can be updated)
   * @param upload_data upload data
   * @param[in,out] upload_data_size number of bytes (left) in @a upload_data
   * @return MHD result code
   */
  int (*handler)(struct SH_RequestHandler *rh,
                 struct MHD_Connection *connection,
                 void **connection_cls,
                 const char *upload_data,
                 size_t *upload_data_size);

  /**
   * Default response code.
   */
  int response_code;
};


/**
 * Each MHD response handler that sets the "connection_cls" to a
 * non-NULL value must use a struct that has this struct as its first
 * member.  This struct contains a single callback, which will be
 * invoked to clean up the memory when the contection is completed.
 */
struct TM_HandlerContext;

/**
 * Signature of a function used to clean up the context
 * we keep in the "connection_cls" of MHD when handling
 * a request.
 *
 * @param hc header of the context to clean up.
 */
typedef void
(*TM_ContextCleanup)(struct TM_HandlerContext *hc);


/**
 * Each MHD response handler that sets the "connection_cls" to a
 * non-NULL value must use a struct that has this struct as its first
 * member.  This struct contains a single callback, which will be
 * invoked to clean up the memory when the connection is completed.
 */
struct TM_HandlerContext
{

  /**
   * Function to execute the handler-specific cleanup of the
   * (typically larger) context.
   */
  TM_ContextCleanup cc;

  /**
   * Asynchronous request context id.
   */
  struct GNUNET_AsyncScopeId async_scope_id;
};


/**
 * Handle to the database backend.
 */
extern struct SYNC_DatabasePlugin *db;

/**
 * Upload limit to the service, in megabytes.
 */
extern unsigned long long SH_upload_limit_mb;

/**
 * Annual fee for the backup account.
 */
extern struct TALER_Amount SH_annual_fee;

/**
 * Our Taler backend to process payments.
 */
extern char *SH_backend_url;

/**
 * Our fulfillment URL
 */
extern char *SH_fulfillment_url;

/**
 * Our context for making HTTP requests.
 */
extern struct GNUNET_CURL_Context *SH_ctx;

/**
 * Kick MHD to run now, to be called after MHD_resume_connection().
 * Basically, we need to explicitly resume MHD's event loop whenever
 * we made progress serving a request.  This function re-schedules
 * the task processing MHD's activities to run immediately.
 */
void
SH_trigger_daemon (void);


#endif
