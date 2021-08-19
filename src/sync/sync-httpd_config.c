/*
  This file is part of Sync
  Copyright (C) 2020 Taler Systems SA

  Sync is free software; you can redistribute it and/or modify it under the
  terms of the GNU Lesser General Public License as published by the Free Software
  Foundation; either version 3, or (at your option) any later version.

  Sync is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along with
  Sync; see the file COPYING.GPL.  If not, see <http://www.gnu.org/licenses/>
*/
/**
 * @file backend/sync-httpd_config.c
 * @brief headers for /config handler
 * @author Christian Grothoff
 */
#include "platform.h"
#include "sync-httpd_config.h"
#include <taler/taler_json_lib.h>


/*
 * Protocol version history:
 *
 * 0: original design
 * 1: adds ?fresh=y to POST backup operation to force fresh contract
 *    to be created
 */

/**
 * Manages a /config call.
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
SH_handler_config (struct SH_RequestHandler *rh,
                   struct MHD_Connection *connection,
                   void **connection_cls,
                   const char *upload_data,
                   size_t *upload_data_size)
{
  return TALER_MHD_REPLY_JSON_PACK (
    connection,
    MHD_HTTP_OK,
    GNUNET_JSON_pack_string ("name",
                             "sync"),
    GNUNET_JSON_pack_uint64 ("storage_limit_in_megabytes",
                             SH_upload_limit_mb),
    TALER_JSON_pack_amount ("annual_fee",
                            &SH_annual_fee),
    GNUNET_JSON_pack_string ("version",
                             "1:0:1"));
}


/* end of sync-httpd_config.c */
