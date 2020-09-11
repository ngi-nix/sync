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
 * @file backend/sync-httpd_config.h
 * @brief headers for /config handler
 * @author Christian Grothoff
 */
#ifndef SYNC_HTTPD_CONFIG_H
#define SYNC_HTTPD_CONFIG_H
#include <microhttpd.h>
#include "sync-httpd.h"

/**
 * Manages a /config call.
 *
 * @param rh context of the handler
 * @param connection the MHD connection to handle
 * @param[in,out] connection_cls the connection's closure (can be updated)
 * @param upload_data upload data
 * @param[in,out] upload_data_size number of bytes (left) in @a upload_data
 * @return MHD result code
 */
MHD_RESULT
SH_handler_config (struct SH_RequestHandler *rh,
                   struct MHD_Connection *connection,
                   void **connection_cls,
                   const char *upload_data,
                   size_t *upload_data_size);

#endif

/* end of sync-httpd_config.h */
