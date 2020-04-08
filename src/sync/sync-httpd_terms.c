/*
  This file is part of TALER
  (C) 2019 Taler Systems SA

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
 * @file sync/sync-httpd_terms.c
 * @brief headers for /terms handler
 * @author Christian Grothoff
 */
#include "platform.h"
#include "sync-httpd_terms.h"
#include <taler/taler_json_lib.h>

/**
 * Manages a /terms call.
 *
 * @param rh context of the handler
 * @param connection the MHD connection to handle
 * @param[in,out] connection_cls the connection's closure (can be updated)
 * @param upload_data upload data
 * @param[in,out] upload_data_size number of bytes (left) in @a upload_data
 * @param mi merchant backend instance, never NULL
 * @return MHD result code
 */
MHD_RESULT
SH_handler_terms (struct SH_RequestHandler *rh,
                  struct MHD_Connection *connection,
                  void **connection_cls,
                  const char *upload_data,
                  size_t *upload_data_size)
{
  return TALER_MHD_reply_json_pack (connection,
                                    MHD_HTTP_OK,
                                    "{s:I, s:o, s:s}",
                                    "storage_limit_in_megabytes",
                                    (json_int_t) SH_upload_limit_mb,
                                    "annual_fee",
                                    TALER_JSON_from_amount (&SH_annual_fee),
                                    "version",
                                    "0.0");
}
